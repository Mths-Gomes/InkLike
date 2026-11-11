#include <ArduinoJson.h>
#include <CompletedBooksStore.h>
#include <HalStorage.h>
#include <ReaderTitles.h>

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void check(bool passed, const char* description) {
  if (!passed) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}
}  // namespace

bool ReadingStatsDate::isValid() const { return year >= 2000 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }
void ReadingStatsDate::clear() { year = month = day = 0; }
int compareReadingStatsDate(const ReadingStatsDate& a, const ReadingStatsDate& b) {
  const int lhs = a.year * 10000 + a.month * 100 + a.day;
  const int rhs = b.year * 10000 + b.month * 100 + b.day;
  return (lhs > rhs) - (lhs < rhs);
}

int main() {
  using namespace CompletedBooksStore;
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
  const std::string displayTitle = std::string(190, 'a') + "\xc3\xa9";
  check(update("/.crosspoint/epub_utf8", displayTitle, stats), "UTF-8 display metadata");
  loadPage(0, 2026, page);
  check(strlen(page.entries[0].title) == 190, "display title ends before split codepoint");
  check(ReaderTitles::indexFor(0) == 0, "zero books title");
  for (size_t i = 1; i < ReaderTitles::count; ++i) {
    const auto minimum = ReaderTitles::levels[i].minimumBooks;
    check(ReaderTitles::indexFor(minimum - 1) == i - 1, "before milestone");
    check(ReaderTitles::indexFor(minimum) == i, "at milestone");
  }
  check(ReaderTitles::indexFor(std::numeric_limits<uint32_t>::max()) == 9, "largest total");
  std::puts("PASS: completion history, corrections, years, moves, recovery, pagination, and title milestones");
}
