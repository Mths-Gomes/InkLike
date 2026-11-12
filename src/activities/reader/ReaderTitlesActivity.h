#pragma once

#include "../Activity.h"
#include "CompletedBooksStore.h"
#include "ReaderStatsNavigation.h"
#include "components/UiAppHost.h"

class ReaderTitlesActivity final : public Activity {
  uint32_t completedBooks = 0;
  ReaderStatsNavigation navigation;
  uint32_t historyOffset = 0;
  uint16_t currentYear = 0;
  // ~1 KB allocated once with the activity, never on the small task stack.
  CompletedBooksStore::Page history;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  bool ignoreConfirm = false;
  bool ignoreBack = false;
  bool ignorePower = false;
  bool statsUnavailable = false;
  // Fixed-capacity SDK list routing; no library-sized interaction table.
  UiAppHost<8, 0> ui;
  Rect contentBounds{};
  char dateLabel[64]{};
  using Screen = UiAppHost<8, 0>::Screen;
  void openSelection();
  void moveSelection(int delta);
  void openSelectedBook();
  void refreshHistory();
  void renderUi(Screen& screen);
  void renderOverview(Screen& screen);
  void renderTitles();
  void drawHistoryPages();
  static void historyRow(void* context, uint16_t index, freeink::ui::ListItem& item);

 public:
  ReaderTitlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
