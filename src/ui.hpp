#pragma once

#include <mutex>

#include "data.hpp"

struct UI_File {
  std::string path;
  std::vector<Match> matches;
  std::vector<LineInfo> lineInfo;

  const uint8_t *bufContent;
  size_t lenContent;
};

struct UI_State {
  bool discard;
  std::mutex lockFiles;
  std::vector<UI_File> files;
};

using UI_PfnExit = void (*)(void* user);
using UI_PfnGetCurrentState = const UI_State* (*)(void* user);
using UI_PfnDiscardOldestState = void (*)(void* user);

struct UI_DataSource {
  UI_PfnExit exit;
  UI_PfnGetCurrentState getCurrentState;
  UI_PfnDiscardOldestState discardOldestState;
};

bool UI_Init(UI_DataSource* dataSource, void *user);
bool UI_Finish(void);