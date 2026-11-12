#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "GlobalReadingStats.h"
#include "HalClock.h"
#include "HalStorage.h"
#include "ReserveFuelStore.h"

#define CHECK(condition)                                           \
  do {                                                             \
    if (!(condition)) {                                            \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
      std::exit(1);                                                \
    }                                                              \
  } while (false)

namespace {
ReadingStatsDateTime current;
ReadingStatsDateTime now() {
  ReadingStatsDateTime dt;
  CHECK(getCurrentLocalReadingStatsDateTime(dt));
  return dt;
}
void skip(unsigned days) {
  auto dt = now();
  addDaysToReadingStatsDate(dt.date, static_cast<int>(days));
  halClock.year = dt.date.year;
  halClock.month = dt.date.month;
  halClock.day = dt.date.day;
}
void read(GlobalReadingStats& stats) {
  stats.totalReadingSeconds += 60;
  stats.recordReadingSpan(now(), 60);
  stats.save();
}
GlobalReadingStats fresh(uint8_t capacity = 5, uint8_t interval = 3) {
  Storage.reset();
  SETTINGS = {};
  SETTINGS.reserveFuelCapacity = capacity;
  SETTINGS.reserveFuelInterval = interval;
  halClock = {};
  auto stats = GlobalReadingStats::load();
  stats.refreshReserveFuel();
  return stats;
}

void persistedStreakAndWireFormat() {
  auto stats = fresh();
  for (unsigned day = 0; day < 15; ++day) {
    read(stats);
    skip(1);
  }
  stats = GlobalReadingStats::load();
  CHECK(stats.reserveFuel.balance == 5);
  skip(2);  // Days 16 and 17 finished without reading; day 18 is still open.
  stats.refreshReserveFuel();
  CHECK(stats.reserveFuel.balance == 3);
  CHECK(stats.currentReadingStreak(&(current = now()).date) == 15);
  stats = GlobalReadingStats::load();
  CHECK(stats.reserveFuel.balance == 3);
  read(stats);
  stats = GlobalReadingStats::load();
  CHECK(stats.currentReadingStreak(&(current = now()).date) == 16);
  CHECK(stats.reserveFuel.balance == 3);
  CHECK(stats.reserveFuel.progress == 1);

  HalFile file;
  CHECK(Storage.openFileForRead("test", "/.crosspoint/global_stats.bin", file));
  CHECK(file.fileSize() == 159);
  uint8_t version = 0;
  CHECK(file.read(&version, 1) == 1);
  CHECK(version == 3);
  file.close();

  auto aggregated = GlobalReadingStats::loadAggregated(stats);
  CHECK(!aggregated.reserveFuelLocalHistory);
  const auto localBalance = stats.reserveFuel.balance;
  skip(1);
  aggregated.totalReadingSeconds += 60;
  aggregated.recordReadingSpan(now(), 60);  // Simulates additional remote history in the view.
  CHECK(aggregated.reserveFuelView(&(current = now()).date).balance == localBalance);
  CHECK(ReserveFuelStore::load().balance == localBalance);
}

void completionAcrossMidnight() {
  auto stats = fresh(3, 3);
  for (unsigned day = 0; day < 6; ++day) {
    read(stats);
    skip(1);
  }
  const auto pendingStart = now();  // Day 7 has real reading, still owned by the reader.
  skip(1);
  ++stats.completedBooks;
  stats.save();  // A metadata-only save must not classify day 7 as missed.
  CHECK(ReserveFuelStore::load().balance == 2);
  CHECK(ReserveFuelStore::load().streak == 6);
  stats.totalReadingSeconds += 60;
  stats.recordReadingSpan(pendingStart, 60);
  stats.save();
  stats.refreshReserveFuel();
  CHECK(stats.reserveFuel.balance == 2);
  CHECK(stats.reserveFuel.progress == 1);
  CHECK(stats.currentReadingStreak(&(current = now()).date) == 7);
}

void policyChangesAndReset() {
  auto stats = fresh(3, 3);
  for (unsigned day = 0; day < 6; ++day) {
    read(stats);
    skip(1);
  }
  SETTINGS.tracking = false;
  ReserveFuelStore::settingsSaved();
  CHECK(!ReserveFuelStore::load().enabled);
  CHECK(ReserveFuelStore::load().progress == 0);
  skip(2);
  SETTINGS.tracking = true;
  ReserveFuelStore::settingsSaved();
  stats = GlobalReadingStats::load();
  CHECK(stats.currentReadingStreak(&(current = now()).date) == 0);
  CHECK(stats.reserveFuel.balance == 2);
  CHECK(GlobalReadingStats::resetLocal());
  stats = GlobalReadingStats::load();
  CHECK(stats.reserveFuel.balance == 0);
  CHECK(stats.currentReadingStreak(&(current = now()).date) == 0);
}

void calendarBoundaryAndUnavailableClock() {
  auto stats = fresh(1, 3);
  halClock.month = 12;
  halClock.day = 30;
  stats.refreshReserveFuel();
  read(stats);
  skip(1);
  read(stats);
  skip(1);
  read(stats);
  CHECK(halClock.year == 2027 && halClock.month == 1 && halClock.day == 1);
  CHECK(ReserveFuelStore::load().balance == 1);
  CHECK(GlobalReadingStats::load().currentReadingStreak(&(current = now()).date) == 3);
  halClock.valid = false;
  SETTINGS.reserveFuelCapacity = 0;
  ReserveFuelStore::settingsSaved();
  CHECK(ReserveFuelStore::load().progress == 0);
  CHECK(ReserveFuelStore::load().balance == 1);
}
}  // namespace

void historyCounterTransactions() {
  constexpr char primary[] = "/.crosspoint/global_stats.bin";
  constexpr char before[] = "/.crosspoint/global_stats.history-before.bin";
  constexpr char after[] = "/.crosspoint/global_stats.history-after.bin";
  auto prepare = [&] {
    auto stats = fresh();
    stats.completedBooks = 10;
    stats.totalSessions = 20;
    stats.totalReadingSeconds = 900;
    stats.save();
  };
  prepare();
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  auto stats = GlobalReadingStats::load();
  CHECK(stats.completedBooks == 9 && stats.totalSessions == 20 && stats.totalReadingSeconds == 900);
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 9);
  ++stats.completedBooks;
  ++stats.totalSessions;
  stats.save();
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 10);
  CHECK(GlobalReadingStats::clearHistoryRemovalReceipt());

  prepare();
  Storage.failNextRenameFrom(after);
  CHECK(!GlobalReadingStats::commitHistoryRemoval());
  CHECK(Storage.exists(before) && Storage.exists(after) && !Storage.exists(primary));
  CHECK(GlobalReadingStats::load().completedBooks == 9);  // Recovery publishes staged BIN before loading.
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 9);
  CHECK(GlobalReadingStats::clearHistoryRemovalReceipt());

  prepare();
  Storage.failNextRenameFrom(primary);
  CHECK(!GlobalReadingStats::commitHistoryRemoval());
  stats = GlobalReadingStats::load();
  ++stats.completedBooks;
  stats.save();
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 10);  // Uses latest totals before staging.
  Storage.failNextRemove(before);
  CHECK(!GlobalReadingStats::clearHistoryRemovalReceipt());
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 10);
  CHECK(GlobalReadingStats::clearHistoryRemovalReceipt());

  stats = fresh();
  stats.save();
  CHECK(GlobalReadingStats::commitHistoryRemoval());
  CHECK(GlobalReadingStats::load().completedBooks == 0);
  FsFile file;
  CHECK(Storage.openFileForRead("TEST", primary, file));
  CHECK(file.fileSize() == GlobalReadingStats::CURRENT_FILE_SIZE);
  file.close();
  CHECK(GlobalReadingStats::clearHistoryRemovalReceipt());
}

int main() {
  persistedStreakAndWireFormat();
  completionAcrossMidnight();
  policyChangesAndReset();
  calendarBoundaryAndUnavailableClock();
  historyCounterTransactions();
  std::puts("Reserve Fuel integration: all checks passed");
}
