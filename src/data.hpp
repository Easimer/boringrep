#pragma once

#include <string>

struct LineInfo {
  size_t offStart;
  size_t offEnd;
};

struct Match {
  size_t offStart;
  size_t offEnd;

  size_t idxLine;
  size_t idxColumn;
};

struct FileResult {
  std::string path;
  std::vector<Match> matches;
  std::vector<LineInfo> lineInfo;
};

struct GrepState {
};

