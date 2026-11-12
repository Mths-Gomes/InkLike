#pragma once

#include <cstdint>

// Owned by one Reader Stats activity: child screens preserve it; closing the
// activity destroys it. Overview and finished-list selections are independent.
struct ReaderStatsNavigation {
  enum class Screen : uint8_t { Overview, Titles, FinishedBooks };
  Screen screen = Screen::Overview;
  uint8_t overviewSelection = 0;
  uint32_t selected = 0;

  void open() {
    if (screen == Screen::Overview) screen = overviewSelection == 0 ? Screen::Titles : Screen::FinishedBooks;
  }

  bool back() {
    if (screen == Screen::Overview) return false;
    screen = Screen::Overview;
    return true;
  }

  void clamp(const uint32_t total) {
    if (total == 0)
      selected = 0;
    else if (selected >= total)
      selected = total - 1;
  }

  void move(const int delta, const uint32_t total) {
    if (screen == Screen::Overview) {
      overviewSelection = overviewSelection == 0 ? 1 : 0;
      return;
    }
    if (screen != Screen::FinishedBooks) return;
    clamp(total);
    if (total == 0) return;
    selected = delta < 0 ? (selected == 0 ? total - 1 : selected - 1) : (selected == total - 1 ? 0 : selected + 1);
  }

  uint32_t pageOffset(const uint32_t pageSize) const { return selected / pageSize * pageSize; }

  struct PageWindow {
    uint32_t first = 0;
    uint8_t count = 0;
    bool before = false;
    bool after = false;
  };

  PageWindow pageWindow(const uint32_t total, const uint32_t pageSize) const {
    if (total == 0) return {};
    const uint32_t pages = 1 + (total - 1) / pageSize;
    const uint8_t count = pages < 3 ? static_cast<uint8_t>(pages) : 3;
    const uint32_t current = selected / pageSize;
    uint32_t first = current == 0 ? 0 : current - 1;
    if (first > pages - count) first = pages - count;
    return {first, count, first != 0, first + count < pages};
  }
};
