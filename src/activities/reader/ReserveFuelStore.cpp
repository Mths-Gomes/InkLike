#include "ReserveFuelStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "CrossPointSettings.h"
#include "GlobalReadingStats.h"
#include "util/FileContentEquals.h"

namespace {
constexpr char kPath[] = "/.crosspoint/reserve_fuel.bin";
constexpr char kTemp[] = "/.crosspoint/reserve_fuel.bin.tmp";
constexpr char kBackup[] = "/.crosspoint/reserve_fuel.bin.bak";
constexpr size_t kSize = 29;
constexpr uint8_t kVersion = 1;
std::mutex fuelMutex;

void put32(uint8_t* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t get32(const uint8_t* in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (i * 8);
  return value;
}
uint32_t checksum(const uint8_t* data) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < kSize - 4; ++i) hash = (hash ^ data[i]) * 16777619u;
  return hash;
}
std::array<uint8_t, kSize> encode(const ReserveFuelState& state) {
  std::array<uint8_t, kSize> out{};
  out[0] = kVersion;
  put32(out.data() + 1, state.day);
  put32(out.data() + 5, state.streak);
  put32(out.data() + 9, state.longest);
  put32(out.data() + 13, state.readingSeconds);
  out[17] = state.balance;
  out[18] = state.progress;
  out[19] = state.capacity;
  out[20] = state.interval;
  out[21] = state.enabled;
  out[22] = state.readToday;
  put32(out.data() + 25, checksum(out.data()));
  return out;
}
enum class LoadResult : uint8_t { Missing, Valid, Invalid, Newer };
LoadResult read(const char* path, ReserveFuelState& state) {
  if (!Storage.exists(path)) return LoadResult::Missing;
  HalFile file;
  if (!Storage.openFileForRead("RF", path, file)) return LoadResult::Invalid;
  std::array<uint8_t, kSize> data{};
  const size_t size = file.fileSize();
  const int count = file.read(data.data(), data.size());
  file.close();
  if (count > 0 && data[0] > kVersion) return LoadResult::Newer;
  if (size != kSize || count != static_cast<int>(kSize) || data[0] != kVersion ||
      get32(data.data() + 25) != checksum(data.data()) || data[17] > 5 || !isValidReserveFuelCapacity(data[19]) ||
      !isValidReserveFuelInterval(data[20]) || data[18] >= data[20] || data[21] > 1 || data[22] > 1 ||
      (data[19] != 0 && data[17] > data[19]))
    return LoadResult::Invalid;
  state.day = get32(data.data() + 1);
  state.streak = get32(data.data() + 5);
  state.longest = get32(data.data() + 9);
  state.readingSeconds = get32(data.data() + 13);
  state.balance = data[17];
  state.progress = data[18];
  state.capacity = data[19];
  state.interval = data[20];
  state.enabled = data[21] != 0;
  state.readToday = data[22] != 0;
  return LoadResult::Valid;
}
bool loadUnlocked(ReserveFuelState& state) {
  const auto primary = read(kPath, state);
  if (primary == LoadResult::Valid) return true;
  if (primary == LoadResult::Newer) {
    LOG_ERR("RF", "Newer reserve fuel state; refusing to overwrite");
    return false;
  }
  const auto backup = read(kBackup, state);
  if (backup == LoadResult::Valid) {
    LOG_INF("RF", "Recovered reserve fuel backup");
    return true;
  }
  if (backup == LoadResult::Newer) return false;
  if (primary == LoadResult::Invalid || backup == LoadResult::Invalid) {
    LOG_ERR("RF", "Invalid reserve fuel state; refusing to overwrite");
    return false;
  }
  return true;
}
bool write(const char* path, const uint8_t* data, const size_t size) {
  HalFile file;
  if (!Storage.openFileForWrite("RF", path, file)) return false;
  bool ok = file.write(data, size) == size;
  file.flush();
  ok = file.sync() && ok;
  ok = file.close() && ok;
  if (!ok) LOG_ERR("RF", "Failed to persist reserve fuel: %s", path);
  return ok;
}
bool save(const ReserveFuelState& state) {
  const auto data = encode(state);
  if (fileContentEquals("RF", kPath, data.data(), data.size())) return true;
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  if (Storage.exists(kTemp) && !Storage.remove(kTemp)) return false;
  if (!write(kTemp, data.data(), data.size())) return false;
  // Keep a valid fallback even when the primary was recovered from its backup.
  ReserveFuelState primary;
  if (read(kPath, primary) == LoadResult::Valid) {
    if (Storage.exists(kBackup) && !Storage.remove(kBackup)) return false;
    if (!Storage.rename(kPath, kBackup)) return false;
  } else if (Storage.exists(kPath) && !Storage.remove(kPath)) {
    return false;
  }
  if (!Storage.rename(kTemp, kPath)) {
    LOG_ERR("RF", "Failed to replace reserve fuel state");
    return false;
  }
  return true;
}
}  // namespace

ReserveFuelState ReserveFuelStore::load() {
  std::lock_guard<std::mutex> lock(fuelMutex);
  ReserveFuelState state;
  loadUnlocked(state);
  return state;
}

bool ReserveFuelStore::update(const GlobalReadingStats& stats, ReserveFuelState* snapshot, const Horizon horizon) {
  std::lock_guard<std::mutex> lock(fuelMutex);
  ReserveFuelState state;
  if (!loadUnlocked(state)) return false;
  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) {
    // Policy changes must still clear earning progress when the clock is unavailable.
    state.progress = 0;
    state.capacity = SETTINGS.reserveFuelCapacity;
    state.interval = SETTINGS.reserveFuelInterval;
    state.enabled = false;  // Do not protect an interval whose policy boundary has no valid date.
    if (state.capacity != 0) state.balance = std::min(state.balance, state.capacity);
    return save(state);
  }
  uint32_t throughDay = readingStatsDayIndex(now.date);
  switch (horizon) {
    case Horizon::CommittedHistory:
      // Completion/date edits can save while the active reader still owns an
      // uncommitted session. Do not close days beyond the committed history.
      if (state.day != 0) throughDay = std::min(throughDay, std::max(state.day, stats.readingHistoryAnchorDay));
      break;
    case Horizon::Today:
      break;
  }
  advanceReserveFuel(state, throughDay, stats.readingHistoryAnchorDay, stats.readingHistoryBits,
                     stats.totalReadingSeconds,
                     {SETTINGS.reserveFuelCapacity, SETTINGS.reserveFuelInterval, SETTINGS.shouldTrackReadingStats()});
  if (!save(state)) return false;
  if (snapshot) *snapshot = state;
  return true;
}

void ReserveFuelStore::settingsSaved() {
  // Unrelated settings saves must not finalize days while a reader still owns
  // an uncommitted session (for example when changing font size at midnight).
  const auto state = load();
  if (state.capacity == SETTINGS.reserveFuelCapacity && state.interval == SETTINGS.reserveFuelInterval &&
      state.enabled == (SETTINGS.shouldTrackReadingStats() && SETTINGS.reserveFuelCapacity != 0))
    return;
  const auto stats = GlobalReadingStats::load();
  if (!update(stats)) LOG_ERR("RF", "Could not apply reserve fuel settings");
}

bool ReserveFuelStore::backup(const char* statsBackupPath) {
  std::lock_guard<std::mutex> lock(fuelMutex);
  ReserveFuelState state;
  if (!loadUnlocked(state)) return false;
  if (state.day == 0) return true;
  char path[160];
  const int count = snprintf(path, sizeof(path), "%s.rf", statsBackupPath);
  if (count <= 0 || static_cast<size_t>(count) >= sizeof(path)) return false;
  const auto data = encode(state);
  if (fileContentEquals("RF", path, data.data(), data.size())) return true;
  return write(path, data.data(), data.size());
}
