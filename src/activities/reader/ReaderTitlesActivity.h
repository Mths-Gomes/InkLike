#pragma once

#include "../Activity.h"
#include "CompletedBooksStore.h"

class ReaderTitlesActivity final : public Activity {
  uint32_t completedBooks = 0;
  uint32_t pageIndex = 0;
  uint16_t currentYear = 0;
  // ~1 KB allocated once with the activity, never on the small task stack.
  CompletedBooksStore::Page history;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  bool ignoreConfirm = false;
  bool ignoreBack = false;
  bool ignorePower = false;
  uint32_t pageCount() const;
  void changePage(int delta);

 public:
  ReaderTitlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
