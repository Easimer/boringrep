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

struct InputBoxString {
  std::vector<uint8_t> buf = {0};
  // Points at the null-terminator
  size_t idxEnd = 0;

  InputBoxString() {}
  InputBoxString(const std::string &s) {
    buf.resize(s.size() + 1);
    idxEnd = s.size();
    memcpy(buf.data(), s.data(), s.size());
  }

  void TryGrow(uint32_t n = 1) {
    if (buf.size() == idxEnd + 1) {
      buf.resize(buf.size() + n);
    }
  }

  void Append(uint32_t codepoint) {
    if (0 <= codepoint && codepoint < 0x00'0080) {
      TryGrow(1);
      buf[idxEnd++] = (uint8_t(codepoint & 0x7F));
    } else if (0x00'0080 <= codepoint && codepoint < 0x00'0800) {
      TryGrow(2);
      auto byte0 = ((codepoint >> 6) & 0x1F) | 0xC0;
      auto byte1 = ((codepoint >> 0) & 0x3F) | 0x80;
      buf[idxEnd++] = (uint8_t(byte0));
      buf[idxEnd++] = (uint8_t(byte1));
    } else if (0x00'0800 <= codepoint && codepoint < 0x01'0000) {
      TryGrow(3);
      auto byte0 = ((codepoint >> 12) & 0x0F) | 0xE0;
      auto byte1 = ((codepoint >> 6) & 0x1F) | 0x80;
      auto byte2 = ((codepoint >> 0) & 0x1F) | 0x80;
      buf[idxEnd++] = (uint8_t(byte0));
      buf[idxEnd++] = (uint8_t(byte1));
      buf[idxEnd++] = (uint8_t(byte2));
    } else if (0x01'0000 <= codepoint && codepoint < 0x11'0000) {
      TryGrow(4);
      auto byte0 = ((codepoint >> 18) & 0x0F) | 0xF0;
      auto byte1 = ((codepoint >> 12) & 0x1F) | 0x80;
      auto byte2 = ((codepoint >> 6) & 0x1F) | 0x80;
      auto byte3 = ((codepoint >> 0) & 0x1F) | 0x80;
      buf[idxEnd++] = (uint8_t(byte0));
      buf[idxEnd++] = (uint8_t(byte1));
      buf[idxEnd++] = (uint8_t(byte2));
      buf[idxEnd++] = (uint8_t(byte3));
    } else {
      assert(!"invalid codepoint");
    }

    buf[idxEnd] = 0;
  }

  const char *c_str() const { return (const char *)buf.data(); }
};

struct UI_InputBox {
  enum { BUF_PATH = 0, BUF_FILENAME_PATTERN, BUF_PATTERN, BUF_MAX };
  InputBoxString buffers[BUF_MAX];

  std::optional<size_t> idxEditedField = 0;
  Font font;

  static constexpr float VERT_GAP = 4.0f;
  static constexpr float INPUT_HEIGHT = 16.0f;
  static constexpr float TEXT_HEIGHT = INPUT_HEIGHT - 2.0f;
  static constexpr Vector2 TEXT_OFFSET = {2.0f, 2.0f};
  static constexpr float PADDING_HORI = 4.0f;

  enum Action {
    ACTION_NONE,
    ACTION_APPLY,
  };

  Action Draw(Vector2 pos, Vector2 size) {
    Action ret = ACTION_NONE;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      idxEditedField.reset();
      for (int i = 0; i < BUF_MAX; i++) {
        auto rect = GetButtonRect(pos, size, i);
        if (CheckCollisionPointRec(GetMousePosition(), rect)) {
          idxEditedField = i;
          break;
        }
      }
    }

    if (idxEditedField) {
      auto charEntered = GetCharPressed();
      while (charEntered > 0) {
        buffers[*idxEditedField].Append(*(uint32_t *)(&charEntered));
        charEntered = GetCharPressed();
      }

      auto keyPressed = GetKeyPressed();
      while (keyPressed != 0) {
        switch (keyPressed) {
          case KEY_ENTER:
          case KEY_KP_ENTER:
            ret = ACTION_APPLY;
            break;
          case KEY_TAB:
            if (idxEditedField) {
              idxEditedField = (*idxEditedField + 1) % BUF_MAX;
            } else {
              idxEditedField = 0;
            }
            break;
        }
        keyPressed = GetKeyPressed();
      }
    }

    for (int i = 0; i < BUF_MAX; i++) {
      auto rect = GetButtonRect(pos, size, i);
      Color bgColor = SKYBLUE;
      Color textColor = DARKBLUE;
      if (idxEditedField && *idxEditedField == i) {
        bgColor = BLUE;
        textColor = WHITE;
      }
      DrawRectangle(rect.x, rect.y, rect.width, rect.height, bgColor);
      DrawRectangleLines(rect.x, rect.y, rect.width, rect.height, DARKBLUE);
      DrawTextEx(font, buffers[i].c_str(),
                 {rect.x + TEXT_OFFSET.x, rect.y + TEXT_OFFSET.y}, TEXT_HEIGHT,
                 2, textColor);
    }

    return ACTION_NONE;
  }

  Rectangle GetButtonRect(const Vector2 &pos, const Vector2 &size, int idx) {
    Rectangle ret;
    ret.x = PADDING_HORI;
    ret.width = size.x - PADDING_HORI * 2;
    ret.y = pos.y + idx * (INPUT_HEIGHT + VERT_GAP);
    ret.height = INPUT_HEIGHT;
    return ret;
  }
};

static void threadprocUi(UI_DataSource *dataSource, void *user) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(1280, 720, "boringrep");
  SetTargetFPS(60);

  // char bufPath[LEN_BUF_PATH] = {'\0'};
  // char bufFilenamePattern[LEN_BUF_FILENAME_PATTERN] = {'\0'};
  // char bufPattern[LEN_BUF_PATTERN] = {'\0'};
  // bool editPath = false;
  // bool editFilenamePattern = false;
  // bool editPattern = false;

  UI_InputBox inputBox = {};

  inputBox.font = LoadFontEx("sarasa-mono-j-regular.ttf",
                             UI_InputBox::TEXT_HEIGHT, 0, 0x10000);

  auto cwd = std::filesystem::current_path().string();
  inputBox.buffers[UI_InputBox::BUF_PATH] = InputBoxString(cwd);

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

    inputBox.Draw({10, 10}, {(float)GetScreenWidth(), 256});

    // if (GuiTextBox({10, 8, 256, 24}, bufPath, LEN_BUF_PATH, editPath)) {
    //   editPath = !editPath;
    //   inputChanged = true;
    // }

    // if (GuiTextBox({10, 80, 256, 24}, bufFilenamePattern,
    //                LEN_BUF_FILENAME_PATTERN, editFilenamePattern)) {
    //   editFilenamePattern = !editFilenamePattern;
    //   inputChanged = true;
    // }

    // if (GuiTextBox({10, 152, 256, 24}, bufPattern, LEN_BUF_PATTERN,
    //                editPattern)) {
    //   editPattern = !editPattern;
    //   inputChanged = true;
    // }

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
      request.pathRoot = inputBox.buffers[UI_InputBox::BUF_PATH].c_str();
      request.patternFilename =
          inputBox.buffers[UI_InputBox::BUF_FILENAME_PATTERN].c_str();
      request.pattern = inputBox.buffers[UI_InputBox::BUF_PATTERN].c_str();
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