#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "GlobalReadingStats.h"
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
uint32_t clockDay = 100;
bool clockValid = true;
GlobalReadingStats diskStats;

struct Scenario {
  ReserveFuelState state;
  ReserveFuelPolicy policy;
  std::array<uint8_t, READING_HISTORY_BYTES> history{};
  uint32_t anchor = 100;
  uint32_t seconds = 0;
  uint32_t day = 100;

  Scenario(uint8_t capacity = 1, uint8_t interval = 7) : policy{capacity, interval, true} { advance(); }
  void advance() { advanceReserveFuel(state, day, anchor, history, seconds, policy); }
  void read() {
    if (day > anchor) {
      auto old = history;
      history.fill(0);
      const uint32_t shift = day - anchor;
      for (size_t bit = 0; bit + shift < READING_HISTORY_DAYS; ++bit) {
        if (old[bit / 8] & (1u << (bit % 8))) history[(bit + shift) / 8] |= 1u << ((bit + shift) % 8);
      }
      anchor = day;
    }
    const auto bit = anchor - day;
    history[bit / 8] |= 1u << (bit % 8);
    seconds += 60;
    advance();
  }
  void tomorrow() {
    ++day;
    advance();
  }
  void earn(unsigned count = 1) {
    for (unsigned i = 0; i < count * policy.interval; ++i) {
      read();
      tomorrow();
    }
  }
};

void settingsAndEarning() {
  for (uint8_t capacity : {1, 3, 5}) {
    for (uint8_t interval : {3, 7, 15, 30}) {
      Scenario s(capacity, interval);
      s.earn(capacity);
      CHECK(s.state.balance == capacity);
      CHECK(s.state.progress == 0);
      CHECK(s.state.streak == capacity * interval);
      const auto balance = s.state.balance;
      for (int replay = 0; replay < 5; ++replay) s.advance();
      CHECK(s.state.balance == balance);
      s.read();
      CHECK(s.state.progress == 0);  // No hidden credit while full.
    }
  }
  CHECK(!isValidReserveFuelCapacity(2));
  CHECK(!isValidReserveFuelInterval(1));
}

void absencesAndDisable() {
  Scenario s(5, 3);
  s.earn(5);
  const auto streak = s.state.streak;
  s.day += 2;
  s.advance();
  CHECK(s.state.balance == 3);
  CHECK(s.state.streak == streak);
  CHECK(s.state.progress == 0);
  s.read();
  CHECK(s.state.streak == streak + 1);
  CHECK(s.state.progress == 1);
  s.tomorrow();
  s.tomorrow();  // Missing yesterday resets earning, but not the streak.
  CHECK(s.state.progress == 0);
  CHECK(s.state.streak == streak + 1);
  s.policy.capacity = 0;
  s.advance();
  const auto savedBalance = s.state.balance;
  s.day += 2;
  s.advance();
  CHECK(s.state.streak == 0);
  CHECK(s.state.balance == savedBalance);
  s.policy.capacity = 5;
  s.advance();
  CHECK(s.state.streak == 0);
  CHECK(s.state.progress == 0);

  Scenario tracking(3, 3);
  tracking.earn(2);
  tracking.read();
  tracking.policy.tracking = false;
  tracking.advance();
  CHECK(tracking.state.progress == 0);
  tracking.day += 3;
  tracking.policy.tracking = true;
  tracking.advance();
  CHECK(tracking.state.streak == 0);
  CHECK(tracking.state.balance == 2);
}

void capacityAndClock() {
  Scenario s(5, 3);
  s.earn(5);
  s.policy.capacity = 1;
  s.advance();
  CHECK(s.state.balance == 1);
  s.day += 3;
  s.advance();
  CHECK(s.state.balance == 0);
  CHECK(s.state.streak == 0);
  const auto highWater = s.state.day;
  s.day -= 10;
  s.advance();
  CHECK(s.state.day == highWater);
  s.day = highWater + 2000;
  s.advance();
  CHECK(s.state.balance == 0);
  CHECK(s.state.streak == 0);
  CHECK(s.state.day == s.day);
}

void migrationAndReset() {
  Scenario old;
  old.earn();
  ReserveFuelState fresh;
  advanceReserveFuel(fresh, old.day, old.anchor, old.history, old.seconds, old.policy);
  CHECK(fresh.balance == 0);
  CHECK(fresh.progress == 0);
  CHECK(fresh.streak == 7);
  advanceReserveFuel(old.state, old.day, 0, {}, 0, old.policy);
  CHECK(old.state.balance == 0);
  CHECK(old.state.streak == 0);
}

void saveReadDay(uint32_t day) {
  clockDay = day;
  diskStats.readingHistoryAnchorDay = day;
  // Fixture reads every day after initialization.
  diskStats.readingHistoryBits.fill(0xff);
  diskStats.totalReadingSeconds += 60;
}

void persistence() {
  constexpr const char* path = "/.crosspoint/reserve_fuel.bin";
  constexpr const char* temp = "/.crosspoint/reserve_fuel.bin.tmp";
  Storage.reset();
  SETTINGS = {};
  diskStats = {};
  clockDay = 100;
  CHECK(ReserveFuelStore::update(diskStats));
  for (uint32_t day = 100; day < 107; ++day) {
    saveReadDay(day);
    CHECK(ReserveFuelStore::update(diskStats));
  }
  CHECK(ReserveFuelStore::load().balance == 1);
  CHECK(ReserveFuelStore::update(diskStats));
  CHECK(ReserveFuelStore::load().balance == 1);

  clockDay = 108;
  Storage.failNextRenameFrom(temp);
  CHECK(!ReserveFuelStore::update(diskStats));
  CHECK(ReserveFuelStore::load().balance == 1);  // Recover the previous transaction.
  CHECK(ReserveFuelStore::update(diskStats));
  CHECK(ReserveFuelStore::load().balance == 0);
  CHECK(ReserveFuelStore::update(diskStats));
  CHECK(ReserveFuelStore::load().balance == 0);

  CHECK(ReserveFuelStore::backup("/.crossink-stats-backup/stats_test.bin"));
  CHECK(Storage.exists("/.crossink-stats-backup/stats_test.bin.rf"));
  clockValid = false;
  SETTINGS.reserveFuelCapacity = 0;
  CHECK(ReserveFuelStore::update(diskStats));
  CHECK(!ReserveFuelStore::load().enabled);
  clockValid = true;

  Storage.reset();
  HalFile newer;
  CHECK(Storage.openFileForWrite("test", path, newer));
  const uint8_t version = 2;
  newer.write(&version, 1);
  newer.close();
  CHECK(!ReserveFuelStore::update(diskStats));
  CHECK(!Storage.exists(temp));
}
}  // namespace

bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& out) {
  out.date = {2026, 1, 1};
  return clockValid;
}
uint32_t readingStatsDayIndex(const ReadingStatsDate&) { return clockDay; }
GlobalReadingStats GlobalReadingStats::load() { return diskStats; }

int main() {
  settingsAndEarning();
  absencesAndDisable();
  capacityAndClock();
  migrationAndReset();
  persistence();
  std::printf("Reserve Fuel: all checks passed; state=%zu bytes\n", sizeof(ReserveFuelState));
}
