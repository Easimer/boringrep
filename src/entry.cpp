
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

#include <fmt/core.h>
#include <mio/mmap.hpp>

#include "data.hpp"
#include "ui.hpp"

#include <Tracy.hpp>

enum {
  SIZ_INPUT_BACKLOG = 8,
};

struct MatchThreadInput {
  std::string path;
};

struct MatchThreadResult {
  std::string path;
  mio::mmap_source content;
  std::vector<Match> matches;
  std::vector<LineInfo> lineInfo;
};

struct MatchRequestStateAndContent {
  UI_MatchRequestState state;
  std::unordered_map<std::string, mio::mmap_source> sources;
};

template <typename T>
struct Pipe {
  TracyLockable(std::mutex, mtx);
  std::condition_variable_any cv;
  std::queue<std::optional<T>> queue;

  auto lock() { return std::unique_lock(mtx); }
  auto empty() const { return queue.empty(); }
  auto wait(std::unique_lock<LockableBase(std::mutex)> &L) { cv.wait(L); }
  auto &front() { return queue.front(); }
  auto pop() { queue.pop(); }
  auto notify_one() { cv.notify_one(); }
  auto push(const T &t) { queue.push(t); }
  auto push(T &&t) { queue.push(std::move(t)); }
  auto push() { queue.push(std::nullopt); }
  auto notify_all() { cv.notify_all(); }
};

struct MatchThreadConstants {
  pcre2_code *pattern;
  bool aborted;

  Pipe<MatchThreadInput> inputs;
  Pipe<MatchThreadResult> results;
};

static int pcre2_match_w(pcre2_code_8 *code,
                         const void *contents,
                         size_t size,
                         size_t offset,
                         unsigned flags,
                         pcre2_match_data_8 *matchData) {
  ZoneScoped;
  return pcre2_match(code, (PCRE2_SPTR8)contents, size, offset, flags,
                     matchData, nullptr);
}

static void threadprocMatch(MatchThreadConstants *constants, uint32_t id) {
  ZoneScoped;
  bool shutdown = false;
  auto threadName = fmt::format("Thread-Match#{}", id);
  tracy::SetThreadName(threadName.c_str());

  std::queue<std::optional<MatchThreadInput>> localInputQueue;

  while (!shutdown) {
    ZoneScopedN("MapFileAndMatch");

    if (localInputQueue.empty()) {
      ZoneScopedN("Fetch input");
      auto L = constants->inputs.lock();
      while (constants->inputs.empty()) {
        constants->inputs.wait(L);
      }

      while (localInputQueue.size() < 2 && !constants->inputs.empty()) {
        auto &input = constants->inputs.front();
        localInputQueue.push(std::move(input));
        constants->inputs.pop();
        if (!input) {
          // Found shutdown signal in shared queue
          break;
        }
      }
    }

    if (shutdown) {
      break;
    }

    auto input = std::move(localInputQueue.front());
    localInputQueue.pop();

    if (!input) {
      shutdown = true;
      break;
    }

    std::error_code error;
    mio::mmap_source mmap;

    {
      ZoneScopedN("mmap'ing");
      mmap.map(input->path, error);
      if (error) {
        fmt::print("mmap failure {} {}\n", input->path, error.message());
        continue;
      }
    }

    auto *matchData =
        pcre2_match_data_create_from_pattern(constants->pattern, nullptr);
    size_t offset = 0;
    int rc;
    std::vector<Match> matches;

    std::vector<LineInfo> lineInfos;
    const auto *pContents = (uint8_t *)mmap.data();

    {
      ZoneScopedN("Match loop");
      ZoneText(input->path.c_str(), input->path.size());
      do {
        rc = pcre2_match_w(constants->pattern, pContents, mmap.size(), offset,
                           PCRE2_NOTBOL | PCRE2_NOTEOL | PCRE2_NOTEMPTY,
                           matchData);
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
            ZoneScopedN("Compute line info");
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

            // Last line
            currentLine.offEnd = offEnd;
            lineInfos.push_back(currentLine);
          }

          auto ovector = pcre2_get_ovector_pointer(matchData);

          if (constants->aborted) {
            shutdown = true;
            break;
          }

          Match m = {};
          m.offStart = ovector[0];
          m.offEnd = ovector[1];

          {
            ZoneScopedN("LookupLineIndex");
            // Lookup line index by binary search
            size_t idxLeft = 0;
            size_t idxRight = lineInfos.size() - 1;
            size_t idxMiddle;
            LineInfo *line;

            while (idxLeft <= idxRight) {
              idxMiddle = (idxLeft + idxRight) / 2;
              line = &lineInfos[idxMiddle];
              if (m.offStart < line->offStart) {
                idxRight = idxMiddle;
              } else if (line->offEnd < m.offStart) {
                idxLeft = idxMiddle;
              } else {
                break;
              }
            }

            m.idxLine = idxMiddle;
            m.idxColumn = m.offStart - line->offStart;
            assert(idxMiddle != lineInfos.size());
          }

          // TODO(danielm): groups
          for (int i = 0; i < rc; i++) {
            PCRE2_SPTR substring_start =
                (PCRE2_SPTR8)mmap.data() + ovector[2 * i];
            PCRE2_SIZE substring_length = ovector[2 * i + 1] - ovector[2 * i];

            auto s =
                std::string((const char *)substring_start, substring_length);
          }

          assert(m.idxLine < lineInfos.size());
          assert(m.offStart < mmap.size());
          assert(m.offEnd <= mmap.size());
          matches.push_back(m);

          offset = ovector[1];
        }

        if (constants->aborted) {
          shutdown = true;
          break;
        }
      } while (rc > 0);
    }

    pcre2_match_data_free(matchData);

    if (matches.size() > 0) {
      ZoneScopedN("Pushing results");
      MatchThreadResult result;
      result.content = std::move(mmap);
      result.path = std::move(input->path);
      result.matches = std::move(matches);
      result.lineInfo = std::move(lineInfos);
      auto L = constants->results.lock();
      constants->results.push(std::move(result));
      constants->results.notify_one();
    }
  }

  auto L = constants->results.lock();
  constants->results.push();
  constants->results.notify_one();
}

static bool DoGrep(MatchRequestStateAndContent &S,
                   const std::string &pathRoot,
                   const std::string &patternFilename,
                   const std::string &pattern) {
  ZoneScoped;
  MatchThreadConstants constants;
  std::vector<std::thread> threads;

  auto start = std::chrono::high_resolution_clock::now();

  int rc;
  size_t offError;
  constants.pattern = pcre2_compile((PCRE2_SPTR8)pattern.c_str(),
                                    pattern.size(), 0, &rc, &offError, nullptr);
  if (!constants.pattern) {
    fmt::print("pcre2_compile failed rc={} offset={}\n", rc, offError);
    return false;
  }

  constants.aborted = false;

  for (uint32_t i = 0; i < 8; i++) {
    threads.push_back(std::thread(threadprocMatch, &constants, i));
  }

  std::queue<std::filesystem::path> paths;

  paths.push(std::filesystem::path(pathRoot));

  {
    ZoneScopedN("Enumerate paths");
    std::vector<MatchThreadInput> inputBacklog;
    inputBacklog.reserve(threads.size());

    while (!paths.empty()) {
      auto &P = paths.front();
      for (auto &entry : std::filesystem::directory_iterator(P)) {
        if (entry.is_directory()) {
          paths.push(entry.path());
        }

        if (entry.is_regular_file()) {
          inputBacklog.push_back({entry.path().u8string()});
        }

        if (inputBacklog.size() == threads.size()) {
          auto L = constants.inputs.lock();
          while (!inputBacklog.empty()) {
            constants.inputs.push(std::move(inputBacklog.back()));
            inputBacklog.pop_back();
          }
          constants.inputs.notify_all();
        }
      }
      paths.pop();
    }

    auto L = constants.inputs.lock();
    if (inputBacklog.size() > 0) {
      while (!inputBacklog.empty()) {
        constants.inputs.push(std::move(inputBacklog.back()));
        inputBacklog.pop_back();
      }
    }
    for (auto &thread : threads) {
      constants.inputs.push();
    }
    constants.inputs.notify_all();
  }

  size_t numThreadsRemain = threads.size();

  {
    ZoneScopedN("Receiving results");
    while (numThreadsRemain != 0) {
      auto L = constants.results.lock();
      if (constants.results.empty()) {
        constants.results.wait(L);
      }
      if (!constants.results.empty()) {
        auto result = std::move(constants.results.front());
        constants.results.pop();
        L.unlock();
        if (!result.has_value()) {
          numThreadsRemain -= 1;
          continue;
        }

        for (auto &match : result->matches) {
          assert(match.idxLine < result->lineInfo.size());
          assert(match.offStart < result->content.size());
          assert(match.offEnd <= result->content.size());
        }

        UI_File file;
        file.path = std::move(result->path);
        file.bufContent = (uint8_t *)result->content.data();
        file.lenContent = result->content.size();
        file.lineInfo = std::move(result->lineInfo);
        file.matches = std::move(result->matches);
        S.sources[file.path] = std::move(result->content);
        std::lock_guard G(S.state.lockFiles);
        S.state.files.push_back(file);
      }
    }
  }

  assert(constants.results.empty());

  for (auto &thread : threads) {
    thread.join();
  }

  pcre2_code_free(constants.pattern);

  auto end = std::chrono::high_resolution_clock::now();

  auto duration = end - start;
  fmt::print(
      "DoGrep took {} ms",
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());

  // std::vector<UI_File> files;
  // for (auto &result : constants.results) {
  //   UI_File file;
  //   file.path = std::move(result.path);
  //   files.push_back(std::move(file));

  //   std::lock_guard G(state.lockFiles);
  //   state.files.push_back(std::move(file));
  //   auto *pContent = result.content.data();
  //   for (auto &match : result.matches) {
  //     assert(match.idxLine < result.lineInfo.size());
  //     assert(match.offStart < result.content.size());
  //     assert(match.offEnd <= result.content.size());
  //     auto &line = result.lineInfo[match.idxLine];
  //     fmt::string_view lineContent(pContent + line.offStart,
  //                                  line.offEnd - line.offStart - 1);
  //     fmt::print("  line {}: '{}'\n", match.idxLine + 1, lineContent);
  //   }
  // }

  return true;
}

struct UI_DataSourceImpl : UI_DataSource {
  std::list<MatchRequestStateAndContent> states;

  bool shutdown = false;
  std::condition_variable cv;
  std::mutex lock;
  std::optional<GrepRequest> grepRequest;
};

UI_MatchRequestState *uiGetCurrentState(void *user) {
  auto *state = (UI_DataSourceImpl *)user;

  std::unique_lock L(state->lock);
  if (state->states.empty()) {
    return nullptr;
  }

  auto &uiState = state->states.front();
  L.unlock();
  return &uiState.state;
}

void uiDiscardOldestState(void *user) {
  auto *state = (UI_DataSourceImpl *)user;

  std::unique_lock L(state->lock);
  if (state->states.empty()) {
    return;
  }
  state->states.pop_front();
}

void uiPutRequest(void *user, GrepRequest &&request) {
  auto *state = (UI_DataSourceImpl *)user;
  std::unique_lock L(state->lock);
  state->grepRequest = std::move(request);
  state->cv.notify_one();
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
  dataSource.putRequest = &uiPutRequest;

  UI_Init(&dataSource, &dataSource);

  while (!dataSource.shutdown) {
    std::unique_lock L(dataSource.lock);
    dataSource.cv.wait(L);

    if (dataSource.grepRequest) {
      if (dataSource.grepRequest->pattern.empty()) {
        dataSource.grepRequest.reset();
        continue;
      }
      for (auto &S : dataSource.states) {
        S.state.status = UI_MRSAborted;
      }
      dataSource.states.emplace_back();
      auto &S = dataSource.states.back();
      auto request = std::move(dataSource.grepRequest.value());
      L.unlock();
      dataSource.grepRequest.reset();

      DoGrep(S, request.pathRoot, request.patternFilename, request.pattern);
      S.state.status = UI_MRSFinished;
    }
  }

  UI_Finish();
  return 0;
}