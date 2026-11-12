#include <ArduinoJson.h>
#include <CompletedBooksStore.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReaderStatsNavigation.h>
#include <ReaderTitles.h>

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

namespace {
void check(bool passed, const char* description) {
  if (!passed) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}
uint32_t completedCounter = 0;
bool failCounter = false;
constexpr char receipt[] = "/counter-receipt";
constexpr char journal[] = "/.crosspoint/completed_books/.deleting.json";
}  // namespace

// Real counter publication/recovery is exercised by ReserveFuelIntegrationTest;
// these fakes isolate the catalog/BIN/counter orchestration and failure ordering.
bool BookReadingStats::remove(const std::string& cachePath) {
  const std::string prefix = cachePath + "/stats";
  for (auto it = Storage.files.begin(); it != Storage.files.end();) {
    const std::string path = it++->first;
    if (path.starts_with(prefix) && !Storage.remove(path.c_str())) return false;
  }
  return true;
}
bool GlobalReadingStats::commitHistoryRemoval() {
  if (failCounter) {
    failCounter = false;
    return false;
  }
  if (!Storage.exists(receipt)) {
    if (completedCounter > 0) --completedCounter;
    Storage.files[receipt] = "committed";
  }
  return true;
}
bool GlobalReadingStats::clearHistoryRemovalReceipt() { return !Storage.exists(receipt) || Storage.remove(receipt); }

void historyDeletionTests() {
  using namespace CompletedBooksStore;
  constexpr char cache[] = "/.crosspoint/epub_delete";
  const std::string bin = std::string(cache) + "/stats_v5.bin";
  const std::string record = "/.crosspoint/completed_books/epub_delete.json";
  auto prepare = [&] {
    Storage.files.clear();
    Storage.failRemovePath.clear();
    Storage.failWrites = 0;
    completedCounter = 10;
    failCounter = false;
    BookReadingStats completed;
    completed.isCompleted = true;
    check(update(cache, "Deleted book", completed), "prepare history deletion");
    Storage.files[bin] = "sessions and time";
    Storage.files[std::string(cache) + "/progress.bin"] = "position";
    Storage.files["/books/book.epub"] = "book content";
  };
  prepare();
  check(eraseHistory(cache), "delete history and book statistics");
  check(completedCounter == 9 && !Storage.exists(bin.c_str()) && !Storage.exists(record.c_str()),
        "one completion removed along with JSON and BIN");
  check(Storage.exists("/books/book.epub") && Storage.exists((std::string(cache) + "/progress.bin").c_str()),
        "book file and reading position preserved");
  check(ReaderTitles::indexFor(completedCounter) < ReaderTitles::indexFor(10), "title recalculates below milestone");
  check(eraseHistory(cache) && completedCounter == 9, "repeated deletion never decrements twice");
  BookReadingStats reread;
  reread.isCompleted = true;
  ++completedCounter;
  check(update(cache, "Read again later", reread), "new completion can be recorded after deletion");
  check(eraseHistory(cache) && completedCounter == 9, "separate later completion is independently removable");

  prepare();
  BookReadingStats unfinished;
  check(update(cache, "Completion already undone", unfinished), "prepare uncounted completion");
  check(eraseHistory(cache) && completedCounter == 10, "already-unmarked entry never decrements again");

  prepare();
  Storage.failWrites = 1;
  check(!eraseHistory(cache) && completedCounter == 10 && Storage.exists(bin.c_str()),
        "failed journal preparation changes no book data");
  prepare();
  Storage.failRemovePath = bin;
  check(!eraseHistory(cache) && completedCounter == 10 && Storage.exists(journal),
        "failed BIN removal retains journal without decrement");
  BookReadingStats stale;
  stale.isCompleted = true;
  check(!update(cache, "stale snapshot", stale), "pending deletion blocks history reimport");
  check(deletionPendingFor(cache) && !deletionPendingFor("/.crosspoint/epub_other"),
        "pending deletion remains attached to its own book");
  check(recoverPendingDeletion() && completedCounter == 9, "retry completes failed BIN removal");

  prepare();
  failCounter = true;
  check(!eraseHistory(cache) && completedCounter == 10 && Storage.exists(record.c_str()),
        "failed counter update leaves catalog retryable after BIN deletion");
  check(recoverPendingDeletion() && completedCounter == 9, "counter update retried once");

  prepare();
  Storage.files[record + ".bak"] = Storage.files[record];
  Storage.failRemovePath = record + ".bak";
  check(!eraseHistory(cache) && completedCounter == 9 && Storage.exists(record.c_str()),
        "failed catalog cleanup preserves canonical record and counter receipt");
  ++completedCounter;  // Another book completes before the user retries.
  check(recoverPendingDeletion() && completedCounter == 10 && !Storage.exists(record.c_str()),
        "later completions cannot cause a second decrement during retry");

  prepare();
  Storage.failRemovePath = journal;
  check(!eraseHistory(cache) && completedCounter == 9 && Storage.exists(receipt),
        "journal cleanup failure retains counter receipt");
  check(recoverPendingDeletion() && completedCounter == 9 && !Storage.exists(receipt),
        "interrupted deletion recovery is idempotent");
  prepare();
  Storage.files.erase(bin);
  check(eraseHistory(cache) && completedCounter == 9, "missing statistics do not trap a completion in history");
  prepare();
  completedCounter = 0;
  check(eraseHistory(cache) && completedCounter == 0, "completion counter cannot underflow");
}

bool ReadingStatsDate::isValid() const { return year >= 2000 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }
void ReadingStatsDate::clear() { year = month = day = 0; }
int compareReadingStatsDate(const ReadingStatsDate& a, const ReadingStatsDate& b) {
  const int lhs = a.year * 10000 + a.month * 100 + a.day;
  const int rhs = b.year * 10000 + b.month * 100 + b.day;
  return (lhs > rhs) - (lhs < rhs);
}

int main() {
  using namespace CompletedBooksStore;
  ReaderStatsNavigation navigation;
  using View = ReaderStatsNavigation::Screen;
  check(navigation.screen == View::Overview && navigation.overviewSelection == 0 && navigation.selected == 0,
        "fresh activity starts at current title and first book");
  navigation.open();
  check(navigation.screen == View::Titles, "current title opens title reference");
  check(navigation.back() && navigation.screen == View::Overview, "title reference returns to overview");
  navigation.move(1, 12);
  navigation.open();
  check(navigation.screen == View::FinishedBooks, "second overview entry opens finished books");
  navigation.move(-1, 12);
  check(navigation.selected == 11 && navigation.pageOffset(PAGE_SIZE) == 10, "up wraps to final partial page");
  navigation.move(1, 12);
  check(navigation.selected == 0 && navigation.pageOffset(PAGE_SIZE) == 0, "down wraps to first page");
  for (int i = 0; i < 5; ++i) navigation.move(1, 12);
  check(navigation.selected == 5 && navigation.pageOffset(PAGE_SIZE) == 5, "selection crosses page boundary");
  check(navigation.back() && navigation.screen == View::Overview && navigation.selected == 5,
        "back returns directly to overview from any list page");
  check(navigation.overviewSelection == 1, "back keeps finished books selected on overview");
  navigation.open();
  check(navigation.screen == View::FinishedBooks && navigation.selected == 5, "reopening list keeps position");
  navigation.clamp(3);
  check(navigation.selected == 2, "removed completion clamps selection");
  navigation.move(1, 0);
  check(navigation.selected == 0, "empty history has no selection underflow");
  navigation.move(-1, 1);
  navigation.move(1, 1);
  check(navigation.selected == 0, "single book wraps to itself");
  auto window = navigation.pageWindow(0, PAGE_SIZE);
  check(window.count == 0, "empty list has no page squares");
  window = navigation.pageWindow(1, PAGE_SIZE);
  check(window.count == 1 && !window.before && !window.after, "one-page list shows one square without overflow");
  window = navigation.pageWindow(15, PAGE_SIZE);
  check(window.count == 3 && !window.before && !window.after, "three pages fit without overflow dots");
  window = navigation.pageWindow(26, PAGE_SIZE);
  check(window.first == 0 && window.count == 3 && !window.before && window.after, "first page shows right overflow");
  navigation.selected = 15;
  window = navigation.pageWindow(26, PAGE_SIZE);
  check(window.first == 2 && window.count == 3 && window.before && window.after,
        "middle pages show both overflow dots");
  navigation.selected = 25;
  window = navigation.pageWindow(26, PAGE_SIZE);
  check(window.first == 3 && window.count == 3 && window.before && !window.after,
        "last partial page shows left overflow");
  navigation.move(1, 26);
  check(navigation.selected == 0 && navigation.pageWindow(26, PAGE_SIZE).first == 0, "wraparound resets page window");
  Page page;
  BookReadingStats stats;
  stats.isCompleted = true;
  stats.finishedDate = {2025, 12, 31};
  check(update("/.crosspoint/epub_one", "A \"book\" \\ test", stats), "record completion");
  const auto writes = Storage.writes;
  check(update("/.crosspoint/epub_one", "A \"book\" \\ test", stats), "repeat completion");
  check(Storage.writes == writes, "unchanged record avoids SD writes");
  loadPage(0, 2026, page);
  check(page.total == 1 && page.thisYear == 0 && page.size == 1, "new year preserves old completion");
  check(std::string(page.entries[0].title) == "A \"book\" \\ test", "JSON escaping round trip");
  std::string cachePath, fullTitle;
  check(loadDetails(page.entries[0], cachePath, fullTitle), "open existing catalog without a book file");
  check(cachePath == "/.crosspoint/epub_one" && fullTitle == "A \"book\" \\ test",
        "catalog links original BIN directory");
  stats.finishedDate = {2026, 1, 1};
  check(update("/.crosspoint/epub_one", "A book", stats), "date correction");
  loadPage(0, 2026, page);
  check(page.total == 1 && page.thisYear == 1, "corrected year counted once");
  stats.isCompleted = false;
  check(update("/.crosspoint/epub_one", "A book", stats), "undo completion");
  loadPage(0, 2026, page);
  check(page.total == 0 && page.thisYear == 0, "undone completion excluded");
  stats.isCompleted = true;
  stats.finishedDate.clear();
  check(update("/.crosspoint/epub_one", "A book", stats), "unknown date");
  loadPage(0, 2026, page);
  check(page.total == 1 && page.unknownYear == 1 && page.thisYear == 0, "unknown year never guessed");
  check(migrate("/.crosspoint/epub_one", "/.crosspoint/epub_moved"), "move history");
  check(update("/.crosspoint/epub_moved", "A book", stats), "update moved book");
  loadPage(0, 2026, page);
  check(page.total == 1, "move does not duplicate");
  check(loadDetails(page.entries[0], cachePath, fullTitle) && cachePath == "/.crosspoint/epub_moved",
        "moved catalog resolves new BIN directory");
  const std::string record = "/.crosspoint/completed_books/epub_moved.json";
  check(Storage.rename(record.c_str(), (record + ".bak").c_str()), "simulate interrupted replacement");
  loadPage(0, 2026, page);
  check(page.ok && page.total == 1 && Storage.exists((record + ".bak").c_str()),
        "scan reads lone backup without renaming");
  check(update("/.crosspoint/epub_moved", "A book", stats) && Storage.exists(record.c_str()),
        "next update recovers backup");
  Storage.files[record + ".bak"] = Storage.files[record];
  check(migrate("/.crosspoint/epub_moved", "/.crosspoint/epub_moved_twice"), "move with stale backup");
  loadPage(0, 2026, page);
  check(page.total == 1 && !Storage.exists((record + ".bak").c_str()), "stale backup cannot resurrect moved record");
  for (int i = 0; i < 8; ++i) {
    check(update("/.crosspoint/epub_" + std::to_string(i), "Book " + std::to_string(i), stats), "populate page");
  }
  loadPage(5, 2026, page);
  check(page.total == 9 && page.size == 4, "bounded second page");
  uint32_t selectedIndex;
  loadPage(0, 2026, page, "epub_moved_twice", &selectedIndex);
  check(selectedIndex != UINT32_MAX, "selection lookup scans beyond visible page");
  loadPage(selectedIndex / PAGE_SIZE * PAGE_SIZE, 2026, page);
  check(std::string(page.entries[selectedIndex % PAGE_SIZE].cacheId) == "epub_moved_twice",
        "selected book can be restored after catalog ordering changes");
  loadPage(0, 2026, page, "epub_missing", &selectedIndex);
  check(selectedIndex == UINT32_MAX, "removed completion has no stale selection reference");
  const std::string movedRecord = "/.crosspoint/completed_books/epub_moved_twice.json";
  Storage.files[movedRecord] = "{broken";
  check(!update("/.crosspoint/epub_moved_twice", "A book", stats), "corrupt history not overwritten");
  Storage.files[movedRecord] = std::string(4097, 'x');
  loadPage(0, 2026, page);
  check(!page.ok && page.total == 8, "oversized record skipped and reported");
  Storage.files.clear();
  const std::string longTitle = std::string(511, 'a') + "\xc3\xa9";
  check(update("/.crosspoint/epub_utf8", longTitle, stats), "bounded UTF-8 metadata");
  JsonDocument bounded;
  check(!deserializeJson(bounded, Storage.files["/.crosspoint/completed_books/epub_utf8.json"]), "parse bounded title");
  check(strlen(bounded["title"] | "") == 511, "stored title ends before split codepoint");
  const std::string displayTitle = std::string(158, 'a') + "\xc3\xa9";
  check(update("/.crosspoint/epub_utf8", displayTitle, stats), "UTF-8 display metadata");
  loadPage(0, 2026, page);
  check(strlen(page.entries[0].title) == 158, "display title ends before split codepoint");
  check(loadDetails(page.entries[0], cachePath, fullTitle) && fullTitle == displayTitle,
        "opening uses full title instead of truncated row metadata");
  const auto detailWrites = Storage.writes;
  check(loadDetails(page.entries[0], cachePath, fullTitle) && Storage.writes == detailWrites,
        "opening unchanged history does not rewrite catalog");
  std::snprintf(page.entries[0].cacheId, sizeof(page.entries[0].cacheId), "epub_../outside");
  check(!loadDetails(page.entries[0], cachePath, fullTitle), "unsafe cache reference rejected");
  Storage.files.clear();
  for (uint32_t i = 0; i < 26; ++i) {
    check(update("/.crosspoint/epub_" + std::to_string(i), "Pagination book " + std::to_string(i), stats),
          "populate six-page history fixture");
  }
  std::set<std::string> seen;
  for (uint32_t offset = 0; offset < 26; offset += PAGE_SIZE) {
    loadPage(offset, 2026, page);
    check(page.ok && page.total == 26 && page.size == (offset == 25 ? 1 : PAGE_SIZE),
          "all simulated history pages have expected bounds");
    for (uint8_t i = 0; i < page.size; ++i)
      check(seen.insert(page.entries[i].cacheId).second, "page crossings never repeat a book");
  }
  check(seen.size() == 26, "six-page traversal covers every completed book");
  check(ReaderTitles::indexFor(0) == 0, "zero books title");
  for (size_t i = 1; i < ReaderTitles::count; ++i) {
    const auto minimum = ReaderTitles::levels[i].minimumBooks;
    check(ReaderTitles::indexFor(minimum - 1) == i - 1, "before milestone");
    check(ReaderTitles::indexFor(minimum) == i, "at milestone");
  }
  check(ReaderTitles::indexFor(std::numeric_limits<uint32_t>::max()) == 9, "largest total");
  historyDeletionTests();
  std::puts("PASS: completion history, corrections, years, moves, recovery, pagination, and title milestones");
}
