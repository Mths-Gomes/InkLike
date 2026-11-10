#pragma once

#include <cstdint>

#include "ReadingStatsUtils.h"

// Device-local state. Reading-history bits always mean real reading, never fuel.
struct ReserveFuelState {
  uint32_t day = 0;
  uint32_t streak = 0;
  uint32_t longest = 0;
  uint32_t readingSeconds = 0;
  uint8_t balance = 0;
  uint8_t progress = 0;
  uint8_t capacity = 1;
  uint8_t interval = 7;
  bool enabled = true;
  bool readToday = false;
};

struct ReserveFuelPolicy {
  uint8_t capacity = 1;
  uint8_t interval = 7;
  bool tracking = true;
  bool enabled() const { return tracking && capacity != 0; }
};

bool isValidReserveFuelCapacity(uint8_t capacity);
bool isValidReserveFuelInterval(uint8_t interval);

// Pure and replayable: callers persist the reading history before this state.
// Old days outside the bounded reading history cannot earn retrospective fuel.
void advanceReserveFuel(ReserveFuelState& state, uint32_t today, uint32_t historyAnchor,
                        const std::array<uint8_t, READING_HISTORY_BYTES>& history, uint32_t readingSeconds,
                        const ReserveFuelPolicy& policy);
