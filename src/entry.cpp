#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <string>
#include <filesystem>
#include <thread>
#include <mutex>
#include <queue>
#include <functional>
#include <optional>

#include <fmt/core.h>
#include <mio/mmap.hpp>

struct Match {
  size_t offStart;
  size_t offEnd;

  size_t idxLine;
  size_t idxColumn;
};

struct MatchThreadResult {
  std::string path;
  mio::mmap_source content;
  std::vector<Match> matches;
};

struct MatchThreadInput {
  std::string path;
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
  fmt::print("Thread startup\n");

  while (!shutdown) {
    std::unique_lock L(constants->lockInputs);
    while (constants->inputs.empty()) {
      constants->cvInputs.wait(L);
    }

    auto input = std::move(constants->inputs.front());
    constants->inputs.pop();
    L.unlock();
    fmt::print("Thread received path '{}'\n", input.path);

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
    auto *matchData = pcre2_match_data_create_from_pattern(constants->pattern, nullptr);
    size_t offset = 0;
    int rc;
    std::vector<Match> matches;

    do {
      rc = pcre2_match(constants->pattern, (PCRE2_SPTR8)mmap.data(), mmap.size(), offset,
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
        auto ovector = pcre2_get_ovector_pointer(matchData);

        if (constants->aborted) {
          shutdown = true;
          break;
        }

        for (int i = 0; i < rc; i++) {
          PCRE2_SPTR substring_start =
              (PCRE2_SPTR8)mmap.data() + ovector[2 * i];
          PCRE2_SIZE substring_length = ovector[2 * i + 1] - ovector[2 * i];

          auto s = std::string((const char *)substring_start, substring_length);

          // TODO(danielm): compute line/col
          Match m;
          m.offStart = ovector[0];
          m.offEnd = ovector[1];
          matches.push_back(m);
        }

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
      std::lock_guard G(constants->lockResults);
      constants->results.push_back(std::move(result));
    }
  }

  fmt::print("Thread exiting\n");
}

int main(int argc, char **argv) {
  MatchThreadConstants constants;
  std::vector<std::thread> threads;

  int rc;
  size_t offError;
  constants.pattern = pcre2_compile(
      (PCRE2_SPTR8) "class", PCRE2_ZERO_TERMINATED, 0, &rc, &offError, nullptr);
  if (!constants.pattern) {
    fmt::print("pcre2_compile failed rc={} offset={}\n", rc, offError);
    return -1;
  }

  constants.aborted = false;

  for (int i = 0; i < 8; i++) {
    threads.push_back(std::thread(threadprocMatch, &constants));
  }

  fmt::print("Enumerating paths\n");
  auto cwd = std::filesystem::current_path();
  std::queue<std::filesystem::path> paths;

  paths.push(cwd);

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

  fmt::print("pushing poison pills\n");
  {
    std::lock_guard G(constants.lockInputs);
    for (auto &thread : threads) {
      constants.inputs.push({});
    }
  }

  fmt::print("waiting on threads\n");
  for (auto &thread : threads) {
    thread.join();
  }

  pcre2_code_free(constants.pattern);

  for (const auto &result : constants.results) {
      fmt::print("{}\n", result.path);
    for (auto &match : result.matches) {
      fmt::print("  from {} to {}\n", match.offStart, match.offEnd);
    }
  
  }

  return 0;
}