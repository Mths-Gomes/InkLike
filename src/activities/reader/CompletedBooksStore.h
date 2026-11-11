#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "BookReadingStats.h"

// Independent of disposable EPUB/XTC caches. One bounded JSON document per book;
// a page scan holds only one document at a time, never the entire library.
namespace CompletedBooksStore {
constexpr size_t PAGE_SIZE = 5;
struct Entry {
  char title[192]{};
  ReadingStatsDate date;
};
struct Page {
  Entry entries[PAGE_SIZE];
  uint32_t total = 0;
  uint32_t thisYear = 0;
  uint32_t unknownYear = 0;
  uint8_t size = 0;
  bool ok = true;
};
static_assert(sizeof(Page) <= 1024, "Completion history page must stay within the C3 memory budget");
bool update(const std::string& cachePath, const std::string& title, const BookReadingStats& stats);
bool migrate(const std::string& oldCachePath, const std::string& newCachePath);
void loadPage(uint32_t offset, uint16_t currentYear, Page& page);
}  // namespace CompletedBooksStore
