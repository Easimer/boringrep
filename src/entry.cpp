
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cassert>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

#include <fmt/core.h>
#include <mio/mmap.hpp>

#include "data.hpp"
#include "ui.hpp"

struct MatchThreadInput {
  std::string path;
};

struct MatchThreadResult {
  std::string path;
  mio::mmap_source content;
  std::vector<Match> matches;
  std::vector<LineInfo> lineInfo;
};

struct MatchThreadConstants {
  pcre2_code *pattern;
  bool aborted;

  std::mutex lockResults;
  std::vector<MatchThreadResult> results;

  std::mutex lockInputs;
  std::condition_variable cvInputs;
  std::queue<MatchThreadInput> inputs;
};

static void threadprocMatch(MatchThreadConstants *constants) {
  bool shutdown = false;

  while (!shutdown) {
    std::unique_lock L(constants->lockInputs);
    while (constants->inputs.empty()) {
      constants->cvInputs.wait(L);
    }

    auto input = std::move(constants->inputs.front());
    constants->inputs.pop();
    L.unlock();

    if (input.path.empty()) {
      shutdown = true;
      continue;
    }

    std::error_code error;
    mio::mmap_source mmap;
    mmap.map(input.path, error);
    if (error) {
      fmt::print("mmap failure\n");
      continue;
    }
    auto *matchData =
        pcre2_match_data_create_from_pattern(constants->pattern, nullptr);
    size_t offset = 0;
    int rc;
    std::vector<Match> matches;

    std::vector<LineInfo> lineInfos;
    const auto *pContents = (uint8_t *)mmap.data();

    do {
      rc = pcre2_match(constants->pattern, pContents, mmap.size(), offset,
                       PCRE2_NOTBOL | PCRE2_NOTEOL, matchData, nullptr);
      if (rc < 0) {
        switch (rc) {
          case PCRE2_ERROR_NOMATCH:
            break;
          default:
            fmt::print("Match error {}\n", rc);
            break;
        }
      } else {
        if (lineInfos.empty()) {
          size_t offCursor = 0;
          const size_t offEnd = mmap.size();

          LineInfo currentLine;
          currentLine.offStart = offCursor;

          while (offCursor != offEnd) {
            if (pContents[offCursor] == '\n') {
              currentLine.offEnd = offCursor;
              lineInfos.push_back(currentLine);
              currentLine.offStart = offCursor + 1;
            }

            offCursor += 1;
          }
        }

        auto ovector = pcre2_get_ovector_pointer(matchData);

        if (constants->aborted) {
          shutdown = true;
          break;
        }

        // TODO(danielm): compute line/col
        Match m;
        m.offStart = ovector[0];
        m.offEnd = ovector[1];

        // Lookup line index
        for (size_t idxLine = 0; idxLine < lineInfos.size(); idxLine++) {
          auto &line = lineInfos[idxLine];
          if (line.offStart <= m.offStart && m.offStart < line.offEnd) {
            m.idxLine = idxLine;
            m.idxColumn = m.offStart - line.offStart;
            break;
          }
        }

        // TODO(danielm): groups
        for (int i = 0; i < rc; i++) {
          PCRE2_SPTR substring_start =
              (PCRE2_SPTR8)mmap.data() + ovector[2 * i];
          PCRE2_SIZE substring_length = ovector[2 * i + 1] - ovector[2 * i];

          auto s = std::string((const char *)substring_start, substring_length);
        }

        matches.push_back(m);

        offset = ovector[1];
      }

      if (constants->aborted) {
        shutdown = true;
        break;
      }
    } while (rc > 0);

    pcre2_match_data_free(matchData);

    if (matches.size() > 0) {
      MatchThreadResult result;
      result.content = std::move(mmap);
      result.path = std::move(input.path);
      result.matches = std::move(matches);
      result.lineInfo = std::move(lineInfos);
      std::lock_guard G(constants->lockResults);
      constants->results.push_back(std::move(result));
    }
  }
}

static bool DoGrep(GrepState &state,
                   const std::string &pathRoot,
                   const std::string &patternFilename,
                   const std::string &pattern) {
  MatchThreadConstants constants;
  std::vector<std::thread> threads;

  int rc;
  size_t offError;
  constants.pattern = pcre2_compile(
      (PCRE2_SPTR8) "class", PCRE2_ZERO_TERMINATED, 0, &rc, &offError, nullptr);
  if (!constants.pattern) {
    fmt::print("pcre2_compile failed rc={} offset={}\n", rc, offError);
    return false;
  }

  constants.aborted = false;

  for (int i = 0; i < 8; i++) {
    threads.push_back(std::thread(threadprocMatch, &constants));
  }

  std::queue<std::filesystem::path> paths;

  paths.push(std::filesystem::path(pathRoot));

  while (!paths.empty()) {
    auto &P = paths.front();
    for (auto &entry : std::filesystem::directory_iterator(P)) {
      if (entry.is_directory()) {
        paths.push(entry.path());
      }

      if (entry.is_regular_file()) {
        {
          std::lock_guard G(constants.lockInputs);
          constants.inputs.push({entry.path().string()});
        }
        constants.cvInputs.notify_one();
      }
    }
    paths.pop();
  }

  {
    std::lock_guard G(constants.lockInputs);
    for (auto &thread : threads) {
      constants.inputs.push({});
    }
  }

  for (auto &thread : threads) {
    thread.join();
  }

  pcre2_code_free(constants.pattern);

  for (const auto &result : constants.results) {
    fmt::print("{}\n", result.path);
    auto *pContent = result.content.data();
    for (auto &match : result.matches) {
      assert(match.idxLine < result.lineInfo.size());
      assert(match.offStart < result.content.size());
      assert(match.offEnd < result.content.size());
      auto &line = result.lineInfo[match.idxLine];
      fmt::string_view lineContent(pContent + line.offStart,
                                   line.offEnd - line.offStart - 1);
      fmt::print("  line {}: '{}'\n", match.idxLine + 1, lineContent);
    }
  }

  return true;
}

struct UI_DataSourceImpl : UI_DataSource {
  std::queue<UI_State> states;

  bool shutdown = false;
  std::condition_variable cv;
  std::mutex lock;
};

const UI_State *uiGetCurrentState(void *user) {
  auto *state = (UI_DataSourceImpl *)user;
  return nullptr;
}

void uiDiscardOldestState(void *user) {
  auto *state = (UI_DataSourceImpl *)user;
  state->states.pop();
}

void uiExit(void *user) {
  auto *state = (UI_DataSourceImpl *)user;

  std::unique_lock L(state->lock);
  state->shutdown = true;
  state->cv.notify_one();
}

int main(int argc, char **argv) {
  UI_DataSourceImpl dataSource;

  dataSource.exit = &uiExit;
  dataSource.discardOldestState = &uiDiscardOldestState;
  dataSource.getCurrentState = &uiGetCurrentState;

  UI_Init(&dataSource, &dataSource);

  while (!dataSource.shutdown) {
    std::unique_lock L(dataSource.lock);
    dataSource.cv.wait(L);
  }

  UI_Finish();
  return 0;
}