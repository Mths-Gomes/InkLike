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
  char title[160]{};
  // Cache directory basename, also the JSON filename stem; no book file needed.
  char cacheId[32]{};
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
// Optional selection lookup shares the existing directory scan. UINT32_MAX
// means the previously selected completion is no longer in the catalog.
void loadPage(uint32_t offset, uint16_t currentYear, Page& page, const char* selectedCacheId = nullptr,
              uint32_t* selectedIndex = nullptr);
// Cold path: retrieve the full stored title only when opening a selected book.
bool loadDetails(const Entry& entry, std::string& cachePath, std::string& title);
// One small journal on the SD card coordinates BIN removal, counter adjustment,
// and catalog removal. Recovery uses the original native stats schemas.
bool eraseHistory(const std::string& cachePath);
bool recoverPendingDeletion();
bool deletionPendingFor(const std::string& cachePath);
}  // namespace CompletedBooksStore
