#include "ui.hpp"

#include <cassert>

#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

static bool gUiInited = false;

static std::thread gUiThread;

static void threadprocUi(UI_DataSource *dataSource, void *user) {
  InitWindow(1280, 720, "boringrep");
  SetTargetFPS(30);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

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