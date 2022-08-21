#include "ui.hpp"

#include <atomic>
#include <cassert>
#include <map>
#include <unordered_set>

#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

#include "BTracy.hpp"

#include "utf8.hpp"
#include "win32.hpp"

enum Action {
  ACTION_NONE,
  ACTION_APPLY,
  ACTION_PAGE_DOWN,
  ACTION_PAGE_UP,
};

enum Layer {
  LAYER_0 = 0,
  LAYER_1 = 1,
  LAYER_2 = 2,

  LAYER_INPUTBOXES = LAYER_1,
  LAYER_FINDER = LAYER_2,
};

static constexpr float INPUT_HEIGHT = 16.0f;
static constexpr float TEXT_HEIGHT = INPUT_HEIGHT - 2.0f;
static constexpr Vector2 TEXT_OFFSET = {2.0f, 2.0f};
static constexpr float VERT_GAP = 4.0f;
static constexpr float PADDING_HORI = 4.0f;

static bool gUiInited = false;

static std::thread gUiThread;

enum {
  LEN_BUF_PATH = 1024,
  LEN_BUF_FILENAME_PATTERN = 1024,
  LEN_BUF_PATTERN = 1024,
};

struct UI_Messages {
  Font font = {};

  void Push(const std::string &message,
            double time = 1.0,
            Color color = RAYWHITE) {
    if (messages.size() == 8) {
      messages.pop_front();
    }

    auto start = GetTime();
    messages.push_back({message, start, start + time, color});
  }

  void PushError(const std::string &message) { Push(message, 5.0f, RED); }

  void Draw() {
    auto cur = messages.begin();
    auto time = GetTime();
    while (cur != messages.end()) {
      if (cur->timeEnd <= time) {
        cur = messages.erase(cur);
      } else {
        ++cur;
      }
    }

    float y = 0;
    for (auto &msg : messages) {
      auto v = MeasureTextEx(font, msg.message.c_str(), TEXT_HEIGHT, 2);
      DrawRectangle(4, y, v.x + 8, v.y + 8, msg.color);
      DrawRectangleLines(4, y, v.x + 8, v.y + 4, BLACK);
      DrawTextEx(font, msg.message.c_str(), {8, y + 4}, TEXT_HEIGHT, 2, BLACK);
      y += v.y + PADDING_HORI;
    }
  }

  struct Message {
    std::string message;
    double timeStart;
    double timeEnd;
    Color color;
  };

  std::deque<Message> messages;
};

struct PreviewState {
  std::optional<std::string> contents;
  size_t idxMatch;
  std::string path;

  Vector2 position;
};

using UI_RenderFunction = std::function<void()>;
struct UI_RenderLayers {
  std::map<int, std::vector<UI_RenderFunction>> layers;

  void Push(int layer, UI_RenderFunction &&func) {
    if (layers.count(layer) == 0) {
      layers[layer] = {};
    }

    layers[layer].emplace_back(std::move(func));
  }

  void Execute() {
    for (auto &[layer, functions] : layers) {
      for (auto &func : functions) {
        func();
      }
    }

    for (auto &[layer, functions] : layers) {
      functions.clear();
    }
  }
};

struct DirectoryFilter {
  DirectoryFilter(std::filesystem::path root) {
    if (root.empty()) {
      auto drives = W32_GetLogicalDriveStrings();
      if (drives.empty()) {
        root = std::filesystem::path("/");
      } else {
        for (auto &drive : drives) {
          subDirectories.push_back(std::filesystem::directory_entry(drive));
        }
        return;
      }
    }

    for (auto &entry : std::filesystem::directory_iterator(root)) {
      if (entry.is_directory()) {
        subDirectories.push_back(entry);
      }
    }
  }

  void Update(const std::string &filter) {
    filteredEntries.clear();
    notFilteredEntries.clear();
    exactMatches.clear();
    for (size_t idxEntry = 0; idxEntry < subDirectories.size(); idxEntry++) {
      auto &entry = subDirectories[idxEntry];
      auto name = entry.path().filename().u8string();
      if (name.empty()) {
        // Filename is be empty when the entry refers to a Windows drive
        name = entry.path().u8string();
      }
      bool filtered = false;
      for (size_t i = 0; i < filter.size() && i < name.size(); i++) {
        if (name[i] != filter[i]) {
          filteredEntries.insert(idxEntry);
          filtered = true;
          break;
        }
      }
      if (!filtered) {
        if (name.size() == filter.size()) {
          exactMatches.insert(idxEntry);
        }
        notFilteredEntries.insert(idxEntry);
      }
    }
  }

  bool GetNextEntry(size_t &out, size_t i) const {
    if (filteredEntries.count(i) != 0) {
      return false;
    }

    out++;
    while (out < subDirectories.size()) {
      if (filteredEntries.count(out) == 0) {
        return true;
      }
    }

    return false;
  }

  bool GetPrevEntry(size_t &out, size_t i) const {
    if (filteredEntries.count(i) == 0) {
      return false;
    }

    out--;
    while (0 < out) {
      if (filteredEntries.count(out) == 0) {
        return true;
      }
    }

    assert(out == 0);

    return filteredEntries.count(0) == 0;
  }

  size_t NumRemains() const {
    return subDirectories.size() - filteredEntries.size();
  }
  std::filesystem::directory_entry GetRemainingEntry() const {
    assert(NumRemains() == 1);
    //
    for (auto idxEntry : notFilteredEntries) {
      return subDirectories[idxEntry];
    }

    std::abort();
  }

  bool TryGetExactMatch(std::filesystem::directory_entry &out) {
    for (auto idxEntry : exactMatches) {
      out = subDirectories[idxEntry];
      return true;
    }

    return false;
  }

  std::vector<std::filesystem::directory_entry> subDirectories;
  std::unordered_set<size_t> filteredEntries;
  std::unordered_set<size_t> notFilteredEntries;
  std::unordered_set<size_t> exactMatches;
};

struct BaseInputBox {
  bool isActive = false;
  bool isInvalid = false;

  virtual void Draw(UI_RenderLayers &layers,
                    Font font,
                    const Rectangle &rect) = 0;

  virtual bool OnCharPressed(int codepoint) = 0;
  virtual bool OnKeyPressed(int keycode) = 0;
  virtual std::string GetString() = 0;
  virtual Color GetBackgroundColor() {
    if (isInvalid) {
      return RED;
    }

    if (isActive) {
      return BLUE;
    } else {
      return SKYBLUE;
    }
  }

  void SetInvalid(bool isInvalid) { this->isInvalid = isInvalid; }

  virtual void Activate(bool v) { isActive = v; }

  virtual Color GetBorderColor() { return DARKBLUE; }

  void DrawBox(const Rectangle &rect) {
    DrawRectangle(rect.x, rect.y, rect.width, rect.height,
                  GetBackgroundColor());
    DrawRectangleLines(rect.x, rect.y, rect.width, rect.height,
                       GetBorderColor());
  }
};

struct InputBox : BaseInputBox {
  EditableUtf8String buf;

  std::string GetString() override { return buf.c_str(); }

  bool OnCharPressed(int codepoint) override {
    if (isActive) {
      buf.Append(codepoint);
    }

    return isActive;
  }

  bool OnKeyPressed(int keycode) override {
    if (!isActive) {
      return false;
    }

    switch (keycode) {
      case KEY_BACKSPACE: {
        buf.DeleteChar();
        return true;
      }
    }

    return false;
  }

  void Draw(UI_RenderLayers &layers,
            Font font,
            const Rectangle &rect) override {
    Color textColor = DARKBLUE;
    if (isActive) {
      textColor = WHITE;
    }

    DrawBox(rect);
    DrawTextEx(font, buf.c_str(),
               {rect.x + TEXT_OFFSET.x, rect.y + TEXT_OFFSET.y}, TEXT_HEIGHT, 2,
               textColor);
  }
};

struct FinderState {
  DirectoryFilter filter;
  size_t idxSelected = 0;

  FinderState(const std::filesystem::path &path) : filter(path) {}
};

struct PathInputBox : BaseInputBox {
  std::filesystem::path path;

  EditableUtf8String buf;

  std::optional<std::string> _fullPathCache;
  std::optional<FinderState> finder;

  bool dirDoesntExist = false;

  PathInputBox() {}
  PathInputBox(const std::filesystem::path &in_path) : path(in_path) {}

  Color GetBackgroundColor() override {
    if (dirDoesntExist) {
      return RED;
    } else {
      return BaseInputBox::GetBackgroundColor();
    }
  }

  Color GetBorderColor() override {
    if (dirDoesntExist) {
      return MAROON;
    } else {
      return BaseInputBox::GetBackgroundColor();
    }
  }

  std::string GetString() override {
    auto s = std::string(buf.c_str());
    return (path / s).u8string();
  }

  void Activate(bool v) override {
    BaseInputBox::Activate(v);

    if (v) {
      finder = FinderState(path);
    } else {
      finder.reset();
    }
  }

  bool OnKeyPressed(int keyCode) override {
    switch (keyCode) {
      case KEY_UP: {
        if (finder) {
        }
        break;
      }
      case KEY_BACKSPACE: {
        dirDoesntExist = false;
        if (buf.IsEmpty()) {
          if (path.has_parent_path()) {
            finder = FinderState(path);
            auto parentPath = path.parent_path();
            if (parentPath == path && path.has_root_name()) {
              auto elem = path.root_name();
              buf = EditableUtf8String(elem.u8string());
              path = std::filesystem::path();
              fmt::print("reached root, editing path: '{}' buf: '{}'\n",
                         path.u8string(), buf.c_str());
            } else {
              // Pop off last directory from the path
              auto elem = path.filename();
              buf = EditableUtf8String(elem.u8string());
              path = parentPath;
              fmt::print("editing path: '{}' buf: '{}'\n", path.u8string(),
                         buf.c_str());
            }

            finder = FinderState(path);
            finder->filter.Update(buf.c_str());
            return true;
          }
        } else {
          bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL);
          if (ctrlHeld) {
            buf.Clear();
          } else {
            buf.DeleteChar();
          }
          finder->filter.Update(buf.c_str());
          return true;
        }
        break;
      }
      case KEY_TAB: {
        if (buf.IsEmpty()) {
          return false;
        }

        if (finder) {
          std::filesystem::directory_entry entry;
          bool hasEntry = false;

          if (finder->filter.NumRemains() == 1) {
            entry = finder->filter.GetRemainingEntry();
            hasEntry = true;
          } else if (finder->filter.TryGetExactMatch(entry)) {
            hasEntry = true;
          }

          if (hasEntry) {
            auto newPath = entry.path();
            std::error_code ec;
            auto status = std::filesystem::status(newPath, ec);
            assert(!ec);
            assert(status.type() == std::filesystem::file_type::directory);
            path = newPath;
            finder = FinderState(path);
            buf.Clear();
          }
        } else {
          auto newPath = path / std::string(buf.c_str());
          std::error_code ec;
          auto status = std::filesystem::status(newPath, ec);
          if (!ec && status.type() == std::filesystem::file_type::directory) {
            path = newPath;
            finder = FinderState(path);
            buf.Clear();
          } else {
            dirDoesntExist = true;
          }
        }

        return true;
      }
    }

    return false;
  }

  bool OnCharPressed(int codepoint) override {
    //
    if (codepoint == '/' || codepoint == '\\') {
      return false;
    }

    if (!finder && buf.IsEmpty()) {
      finder = FinderState(path);
    }

    buf.Append(codepoint);
    finder->filter.Update(buf.c_str());

    return true;
  }

  Color GetFinderBackgroundColor() { return GRAY; }
  Color GetFinderBorderColor() { return DARKGRAY; }

  void DrawFinder(Font font, Rectangle rect) {
    assert(finder.has_value());
    DrawRectangle(rect.x, rect.y, rect.width, rect.height,
                  GetFinderBackgroundColor());
    DrawRectangleLines(rect.x, rect.y, rect.width, rect.height,
                       GetFinderBorderColor());

    float y = rect.y + 2;
    auto &filter = finder->filter;
    for (size_t i = 0; i < filter.subDirectories.size(); i++) {
      if (filter.filteredEntries.count(i) != 0)
        continue;
      auto &entry = filter.subDirectories[i];
      auto path = entry.path().filename().u8string();
      if (path.empty()) {
        // Filename is be empty when the entry refers to a Windows drive
        path = entry.path().u8string();
      }
      auto t = MeasureTextEx(font, path.c_str(), TEXT_HEIGHT, 2);

      if (y + t.y >= rect.y + rect.height) {
        break;
      }

      DrawTextEx(font, path.c_str(), {rect.x + 2, y}, TEXT_HEIGHT, 2, BLACK);
      y += t.y;
    }
  }

  void Draw(UI_RenderLayers &layers,
            Font font,
            const Rectangle &rect) override {
    Color textColor = DARKBLUE;
    if (isActive) {
      textColor = WHITE;
    }

    auto strPath = path.u8string();
    auto t0 = MeasureTextEx(font, strPath.c_str(), TEXT_HEIGHT, 2);

    layers.Push(LAYER_INPUTBOXES,
                [this, font, rect, strPath = move(strPath), textColor]() {
                  DrawBox(rect);
                  DrawTextEx(font, strPath.c_str(),
                             {rect.x + TEXT_OFFSET.x, rect.y + TEXT_OFFSET.y},
                             TEXT_HEIGHT, 2, textColor);
                });

    float x = rect.x + TEXT_OFFSET.x + t0.x + 2;
    float y = rect.y + TEXT_OFFSET.y;
    Vector2 t1 = t0;

    if (!buf.IsEmpty()) {
#if _WIN32
      // preferred_separator is a wchar_t, blegh
      auto strEditedElem = "\\" + std::string(buf.c_str());
#else
      auto strEditedElem = "/" + std::string(buf.c_str());
#endif
      t1 = MeasureTextEx(font, strEditedElem.c_str(), TEXT_HEIGHT, 2);
      layers.Push(LAYER_INPUTBOXES, [this, font, rect,
                                     strEditedElem = move(strEditedElem),
                                     textColor, x, y, t1]() {
        DrawTextEx(font, strEditedElem.c_str(), {x, y}, TEXT_HEIGHT, 2,
                   textColor);
        DrawLine(x, y + t1.y - 2, x + t1.x, y + t1.y - 2, textColor);
      });
    }

    if (finder) {
      auto fx = x + t1.x + 1;
      auto fy = y + t1.y + 1;
      auto width = GetScreenWidth() / 4;
      auto height = GetScreenHeight() / 3;
      if (fx + width >= GetScreenWidth()) {
        width = GetScreenWidth() - fx;
      }

      if (fy + height >= GetScreenHeight()) {
        height = GetScreenHeight() - fy;
      }

      Rectangle rectFinder;
      rectFinder.x = fx;
      rectFinder.y = fy;
      rectFinder.width = width;
      rectFinder.height = height;
      layers.Push(LAYER_INPUTBOXES,
                  [this, font, rectFinder]() { DrawFinder(font, rectFinder); });
    }
  }
};

struct UI_InputBox {
  enum { BUF_PATH = 0, BUF_FILENAME_PATTERN, BUF_PATTERN, BUF_MAX };
  std::unique_ptr<BaseInputBox> inputBoxes[BUF_MAX];

  std::optional<size_t> idxEditedField;
  Font font;
  UI_RenderLayers *layers;

  UI_InputBox() {
    inputBoxes[BUF_PATH] = std::make_unique<PathInputBox>();
    inputBoxes[BUF_FILENAME_PATTERN] = std::make_unique<InputBox>();
    inputBoxes[BUF_PATTERN] = std::make_unique<InputBox>();
  }

  void SetEditedField(std::optional<size_t> idx) {
    if (idxEditedField) {
      inputBoxes[*idxEditedField]->Activate(false);
      idxEditedField.reset();
    }

    idxEditedField = idx;

    if (idxEditedField) {
      inputBoxes[*idxEditedField]->Activate(true);
    }
  }

  Action Draw(UI_MatchRequestState *state, Vector2 pos, Vector2 size) {
    Action ret = ACTION_NONE;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      if (idxEditedField) {
        inputBoxes[*idxEditedField]->Activate(false);
        idxEditedField.reset();
      }
      for (int i = 0; i < BUF_MAX; i++) {
        auto rect = GetButtonRect(pos, size, i);
        if (CheckCollisionPointRec(GetMousePosition(), rect)) {
          SetEditedField(i);
          break;
        }
      }
    }

    if (state) {
      inputBoxes[BUF_PATTERN]->SetInvalid(state->status == UI_MRSBadPattern);
      inputBoxes[BUF_FILENAME_PATTERN]->SetInvalid(state->status ==
                                                   UI_MRSBadFilenamePattern);
    } else {
      inputBoxes[BUF_PATTERN]->SetInvalid(false);
      inputBoxes[BUF_FILENAME_PATTERN]->SetInvalid(false);
    }

    if (idxEditedField) {
      auto charEntered = GetCharPressed();
      while (charEntered > 0) {
        inputBoxes[*idxEditedField]->OnCharPressed(charEntered);
        charEntered = GetCharPressed();
      }

      auto keyPressed = GetKeyPressed();
      while (keyPressed != 0) {
        if (inputBoxes[*idxEditedField]->OnKeyPressed(keyPressed)) {
          keyPressed = GetKeyPressed();
          continue;
        }

        switch (keyPressed) {
          case KEY_ENTER:
          case KEY_KP_ENTER:
            ret = ACTION_APPLY;
            break;
          case KEY_TAB:
            SetEditedField((*idxEditedField + 1) % BUF_MAX);
            break;
        }
        keyPressed = GetKeyPressed();
      }
    } else {
      auto keyPressed = GetKeyPressed();
      while (keyPressed != 0) {
        switch (keyPressed) {
          case KEY_TAB:
            SetEditedField(0);
            break;
          case KEY_PAGE_DOWN:
            ret = ACTION_PAGE_DOWN;
            break;
          case KEY_PAGE_UP:
            ret = ACTION_PAGE_UP;
            break;
        }
        keyPressed = GetKeyPressed();
      }
    }

    for (int i = 0; i < BUF_MAX; i++) {
      auto rect = GetButtonRect(pos, size, i);
      inputBoxes[i]->Draw(*layers, font, rect);
    }

    {
      auto rect = GetButtonRect(pos, size, BUF_MAX);
      rect.width = std::min(64, GetScreenWidth() / 4);
      if (GuiButton(rect, "Abort")) {
        if (state) {
          auto expected = UI_MRSPending;
          state->status.compare_exchange_strong(expected, UI_MRSAborted);
        }
      }
    }

    return ret;
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

static void DrawResults(UI_MatchRequestState *state,
                        const Font &font,
                        float &scrollY,
                        PreviewState &preview) {
  ZoneScoped;

  const int top = 256;
  const int bottom = GetScreenHeight();

  const int viewportTop = top + scrollY;
  const int viewportBottom = bottom + scrollY;

  int y = top;
  bool bottomRendered = true;

  DrawRectangleLines(0, top, GetScreenWidth(), bottom - top, BLACK);

  auto L = std::unique_lock<std::mutex>(state->lockFiles);
  bool mouseWasHoveringAboveALine = false;

  auto &files = state->files;
  for (auto &file : files) {
    ZoneScoped;
    auto width = MeasureText(file.path.c_str(), 10);
    if (viewportTop <= y && y <= viewportBottom) {
      DrawText(file.path.c_str(), 0, y - scrollY, 10, DARKGRAY);
    }
    y += 16;
    size_t idxMatch = 0;
    for (auto &match : file.matches) {
      ZoneScoped;
      if (viewportTop <= y && y <= viewportBottom) {
        if (idxMatch >= file.uiCache.size()) {
          if (file.mmap == nullptr) {
            Mmap_Open(file.mmap, file.path);
            // TODO(danielm): handle failure
          }
          assert(file.mmap != nullptr);
          size_t len;
          const void *contents = nullptr;
          assert(match.idxLine < file.lineInfo.size());
          auto &line = file.lineInfo[match.idxLine];
          Mmap_Map(contents, len, file.mmap, line.offStart,
                   line.offEnd - line.offStart);
          assert(contents != nullptr);
          fmt::string_view lineContent((const char *)contents,
                                       line.offEnd - line.offStart - 1);
          file.uiCache.resize(idxMatch + 1);
          assert(idxMatch < file.uiCache.size());
          file.uiCache[idxMatch] =
              fmt::format("  L#{}: '{}'\n", match.idxLine + 1, lineContent);
          Mmap_Unmap(file.mmap);
        }
        auto tm = MeasureTextEx(GetFontDefault(),
                                file.uiCache[idxMatch].c_str(), 10, 2);
        DrawText(file.uiCache[idxMatch].c_str(), 10, y - scrollY, 10, BLACK);

        // Test for cursor hover and draw the line context
        Rectangle rectLine;
        rectLine.x = 10;
        rectLine.width = tm.x;
        rectLine.y = y - scrollY;
        rectLine.height = 16;
        auto cursor = GetMousePosition();
        if (CheckCollisionPointRec(cursor, rectLine)) {
          mouseWasHoveringAboveALine = true;
          if (preview.path != file.path || preview.idxMatch != idxMatch) {
            preview.contents.reset();
          }

          if (!preview.contents) {
            if (file.mmap == nullptr) {
              Mmap_Open(file.mmap, file.path);
              // TODO(danielm): handle failure
            }
            assert(file.mmap != nullptr);
            size_t len;
            const void *contents = nullptr;
            size_t idxFirstLine =
                match.idxLine > 2 ? match.idxLine - 2 : match.idxLine;
            size_t idxLastLine =
                std::min(match.idxLine + 2, file.lineInfo.size() - 1);
            auto &firstLine = file.lineInfo[idxFirstLine];
            auto &lastLine = file.lineInfo[idxLastLine];
            assert(firstLine.offStart <= lastLine.offEnd);
            Mmap_Map(contents, len, file.mmap, firstLine.offStart,
                     lastLine.offEnd - firstLine.offStart);
            assert(contents != nullptr);
            std::string lineContent((const char *)contents,
                                    lastLine.offEnd - firstLine.offStart);
            std::replace(lineContent.begin(), lineContent.end(), '\r', ' ');
            preview.contents = lineContent;
            preview.idxMatch = idxMatch;
            preview.path = file.path;
            Mmap_Unmap(file.mmap);
          }

          preview.position = cursor;
        }
      }

      y += 16;
      idxMatch++;

      if (y > viewportBottom) {
        bottomRendered = false;
        break;
      }
    }

    if (file.mmap != nullptr) {
      Mmap_Close(file.mmap);
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

  if (!mouseWasHoveringAboveALine) {
    preview.contents.reset();
  }

  if (preview.contents) {
    auto pos = preview.position;
    auto tm = MeasureTextEx(font, preview.contents->c_str(), TEXT_HEIGHT, 2);
    DrawRectangle(pos.x, pos.y, tm.x, tm.y, GRAY);
    DrawTextEx(font, preview.contents->c_str(), pos, TEXT_HEIGHT, 2, BLACK);
  }
}

static void threadprocUi(UI_DataSource *dataSource, void *user) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(1280, 720, "boringrep");
  SetTargetFPS(60);

  UI_InputBox inputBox = {};
  UI_Messages messages;

  UI_RenderLayers layers;

  inputBox.layers = &layers;
  inputBox.font =
      LoadFontEx("sarasa-mono-j-regular.ttf", TEXT_HEIGHT, 0, 0x10000);
  messages.font = inputBox.font;

  PreviewState preview;

  auto cwd = std::filesystem::current_path().string();
  inputBox.inputBoxes[UI_InputBox::BUF_PATH] =
      std::make_unique<PathInputBox>(cwd);

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

    auto *state = dataSource->getCurrentState(user);
    auto action =
        inputBox.Draw(state, {10, 10}, {(float)GetScreenWidth(), 256});

    layers.Execute();

    switch (action) {
      case ACTION_APPLY: {
        ZoneScoped;
        const UI_MatchRequestState *state = nullptr;
        bool finished = false;
        do {
          ZoneScoped;
          state = dataSource->getCurrentState(user);
          if (state) {
            finished = state->status != UI_MRSPending;
            if (finished) {
              dataSource->discardOldestState(user);
            }
          }
        } while (state && finished);

        GrepRequest request;
        request.pathRoot =
            inputBox.inputBoxes[UI_InputBox::BUF_PATH]->GetString();
        request.patternFilename =
            inputBox.inputBoxes[UI_InputBox::BUF_FILENAME_PATTERN]->GetString();
        request.pattern =
            inputBox.inputBoxes[UI_InputBox::BUF_PATTERN]->GetString();
        dataSource->putRequest(user, std::move(request));
        break;
      }
      case ACTION_PAGE_DOWN: {
        scrollY += 200;
        break;
      }
      case ACTION_PAGE_UP: {
        scrollY -= 200;
        break;
      }
    }

    state = dataSource->getCurrentState(user);
    if (state) {
      scrollVel = scrollVel + 5 * GetMouseWheelMove();
      if (scrollVel != 0) {
        scrollVel -= GetFrameTime() * 0.5f * scrollVel;
      }

      scrollY += scrollVel * GetFrameTime();
      scrollY = std::max(0.0f, scrollY);

      if (scrollY == 0.0f) {
        scrollVel = 0.0f;
      }

      DrawResults(state, inputBox.font, scrollY, preview);
    }

    messages.Draw();
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