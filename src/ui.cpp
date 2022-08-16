#include "ui.hpp"

#include <cassert>

#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

#include <Tracy.hpp>

static bool gUiInited = false;

static std::thread gUiThread;

enum {
  LEN_BUF_PATH = 1024,
  LEN_BUF_FILENAME_PATTERN = 1024,
  LEN_BUF_PATTERN = 1024,
};

static void threadprocUi(UI_DataSource *dataSource, void *user) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(1280, 720, "boringrep");
  SetTargetFPS(60);

  char bufPath[LEN_BUF_PATH] = {'\0'};
  char bufFilenamePattern[LEN_BUF_FILENAME_PATTERN] = {'\0'};
  char bufPattern[LEN_BUF_PATTERN] = {'\0'};
  bool editPath = false;
  bool editFilenamePattern = false;
  bool editPattern = false;

  auto cwd = std::filesystem::current_path().string();
  strncpy(bufPath, cwd.data(), LEN_BUF_PATH - 1);
  bufPath[LEN_BUF_PATH - 1] = '\0';

  float scrollY = 0;
  float scrollVel = 0.0f;

  bool wasFocused = IsWindowFocused();

  while (!WindowShouldClose()) {
    bool isFocused = IsWindowFocused();
    if (wasFocused && !isFocused) {
      SetTargetFPS(5);
    } else if (!wasFocused && isFocused) {
      SetTargetFPS(60);
    }
    wasFocused = isFocused;
    BeginDrawing();
    ClearBackground(RAYWHITE);

    {
      const UI_MatchRequestState *state = nullptr;
      bool aborted = false;
      do {
        state = dataSource->getCurrentState(user);
        if (state) {
          aborted = state->status == UI_MRSAborted;
          if (aborted) {
            dataSource->discardOldestState(user);
          }
        }
      } while (state && aborted);
    }

    bool inputChanged = false;

    if (GuiTextBox({10, 8, 256, 24}, bufPath, LEN_BUF_PATH, editPath)) {
      editPath = !editPath;
      inputChanged = true;
    }

    if (GuiTextBox({10, 80, 256, 24}, bufFilenamePattern,
                   LEN_BUF_FILENAME_PATTERN, editFilenamePattern)) {
      editFilenamePattern = !editFilenamePattern;
      inputChanged = true;
    }

    if (GuiTextBox({10, 152, 256, 24}, bufPattern, LEN_BUF_PATTERN,
                   editPattern)) {
      editPattern = !editPattern;
      inputChanged = true;
    }

    if (inputChanged) {
      ZoneScoped;
      const UI_MatchRequestState *state = nullptr;
      bool finished = false;
      do {
        ZoneScoped;
        state = dataSource->getCurrentState(user);
        if (state) {
          finished = state->status == UI_MRSFinished;
          if (finished) {
            dataSource->discardOldestState(user);
          }
        }
      } while (state && finished);

      GrepRequest request;
      request.pathRoot = bufPath;
      request.patternFilename = bufFilenamePattern;
      request.pattern = bufPattern;
      dataSource->putRequest(user, std::move(request));
    }

    auto *state = dataSource->getCurrentState(user);
    if (state) {
      ZoneScoped;

      const int top = 256;
      const int bottom = GetScreenHeight();

      scrollVel = scrollVel + 5 * GetMouseWheelMove();
      if (scrollVel != 0) {
        scrollVel -= GetFrameTime() * 0.5f * scrollVel;
      }

      scrollY += scrollVel * GetFrameTime();
      scrollY = std::max(0.0f, scrollY);

      if (scrollY == 0.0f) {
        scrollVel = 0.0f;
      }

      const int viewportTop = top + scrollY;
      const int viewportBottom = bottom + scrollY;

      int y = top;
      bool bottomRendered = true;

      DrawRectangleLines(10, top, GetScreenWidth() - 20, bottom - top - 10,
                         BLACK);

      auto L = std::unique_lock<std::mutex>(state->lockFiles);

      auto &files = state->files;
      for (auto &file : files) {
        ZoneScoped;
        auto width = MeasureText(file.path.c_str(), 10);
        if (viewportTop <= y && y <= viewportBottom) {
          DrawText(file.path.c_str(), 10, y - scrollY, 10, DARKGRAY);
        } else {
#if DEBUG_TEXT_CLIPPING
          DrawRectangleLines(10, y - scrollY, width, 10, BLACK);
#endif
        }
        y += 16;
        size_t idxMatch = 0;
        for (auto &match : file.matches) {
          ZoneScoped;
          if (viewportTop <= y && y <= viewportBottom) {
            if (idxMatch >= file.uiCache.size()) {
              assert(match.idxLine < file.lineInfo.size());
              assert(match.offStart < file.lenContent);
              assert(match.offEnd <= file.lenContent);
              auto &line = file.lineInfo[match.idxLine];
              fmt::string_view lineContent(
                  (char *)file.bufContent + line.offStart,
                  line.offEnd - line.offStart - 1);
              file.uiCache.resize(idxMatch + 1);
              assert(idxMatch < file.uiCache.size());
              file.uiCache[idxMatch] =
                  fmt::format("  L#{}: '{}'\n", match.idxLine + 1, lineContent);
            }
            auto width = MeasureText(file.uiCache[idxMatch].c_str(), 10);
            DrawText(file.uiCache[idxMatch].c_str(), 10, y - scrollY, 10,
                     BLACK);
          } else {
#if DEBUG_TEXT_CLIPPING
            DrawRectangleLines(10, y - scrollY, width, 10, BLACK);
#endif
          }
          y += 16;
          idxMatch++;

          if (y > viewportBottom) {
            bottomRendered = false;
            break;
          }
        }

        if (y > viewportBottom) {
          bottomRendered = false;
          break;
        }
      }

      if (bottomRendered) {
        float maxY = y;
        scrollY = std::min(scrollY, maxY);
      }
    }

    FrameMark;
    EndDrawing();
  }

  CloseWindow();

  dataSource->exit(user);
}

bool UI_Init(UI_DataSource *dataSource, void *user) {
  assert(dataSource);
  assert(!gUiInited);
  if (!dataSource || gUiInited) {
    return false;
  }

  gUiInited = true;
  gUiThread = std::thread(threadprocUi, dataSource, user);

  return true;
}

bool UI_Finish(void) {
  assert(gUiInited);
  gUiThread.join();
  gUiInited = false;
  return true;
}