#pragma once

#include <mutex>

#include "data.hpp"

struct UI_File {
  std::string path;
  std::vector<Match> matches;
  std::vector<LineInfo> lineInfo;

  const uint8_t *bufContent;
  size_t lenContent;

  std::vector<std::string> uiCache;
};

enum UI_MatchRequestStatus {
	UI_MRSPending = 0,
	UI_MRSFinished,
	UI_MRSAborted,
};

struct UI_MatchRequestState {
  UI_MatchRequestStatus status;
  std::mutex lockFiles;
  std::vector<UI_File> files;
};

using UI_PfnExit = void (*)(void* user);
using UI_PfnPutRequest = void (*)(void* user, GrepRequest &&request);
using UI_PfnGetCurrentState = UI_MatchRequestState* (*)(void* user);
using UI_PfnDiscardOldestState = void (*)(void* user);

struct UI_DataSource {
  UI_PfnExit exit;
  UI_PfnPutRequest putRequest;
  UI_PfnGetCurrentState getCurrentState;
  UI_PfnDiscardOldestState discardOldestState;
};

bool UI_Init(UI_DataSource* dataSource, void *user);
bool UI_Finish(void);