#include "ReserveFuel.h"

#include <algorithm>
#include <limits>

namespace {
bool hasRead(const uint32_t day, const uint32_t anchor, const std::array<uint8_t, READING_HISTORY_BYTES>& history) {
  if (day > anchor || anchor - day >= READING_HISTORY_DAYS) return false;
  const auto bit = anchor - day;
  return (history[bit / 8] & (1u << (bit % 8))) != 0;
}

void increment(uint32_t& value) {
  if (value != std::numeric_limits<uint32_t>::max()) ++value;
}

void recordDay(ReserveFuelState& state) {
  if (state.readToday) return;
  state.readToday = true;
  increment(state.streak);
  state.longest = std::max(state.longest, state.streak);
  if (!state.enabled || state.balance >= state.capacity) {
    state.progress = 0;
    return;
  }
  if (++state.progress >= state.interval) {
    ++state.balance;
    state.progress = 0;
  }
}
}  // namespace

bool isValidReserveFuelCapacity(const uint8_t capacity) {
  return capacity == 0 || capacity == 1 || capacity == 3 || capacity == 5;
}

bool isValidReserveFuelInterval(const uint8_t interval) {
  return interval == 3 || interval == 7 || interval == 15 || interval == 30;
}

void advanceReserveFuel(ReserveFuelState& state, const uint32_t today, const uint32_t historyAnchor,
                        const std::array<uint8_t, READING_HISTORY_BYTES>& history, const uint32_t readingSeconds,
                        const ReserveFuelPolicy& policy) {
  if (today == 0 || !isValidReserveFuelCapacity(policy.capacity) || !isValidReserveFuelInterval(policy.interval))
    return;
  if (state.day == 0 || readingSeconds < state.readingSeconds) {
    state = {};
    state.day = today;
    state.capacity = policy.capacity;
    state.interval = policy.interval;
    state.enabled = policy.enabled();
    // Preserve an existing real streak, but do not award fuel for old history.
    uint32_t cursor = hasRead(today, historyAnchor, history) ? today : today - 1;
    while (state.streak < READING_HISTORY_DAYS && hasRead(cursor, historyAnchor, history)) {
      ++state.streak;
      if (cursor == 0) break;
      --cursor;
    }
    state.longest = state.streak;
    state.readToday = hasRead(today, historyAnchor, history);
  }
  // A backwards clock cannot spend fuel or reward the same day twice.
  if (today < state.day) {
    if (state.capacity != policy.capacity || state.interval != policy.interval || state.enabled != policy.enabled()) {
      state.progress = 0;
      state.capacity = policy.capacity;
      state.interval = policy.interval;
      state.enabled = false;
      if (state.capacity != 0) state.balance = std::min(state.balance, state.capacity);
    }
    return;
  }

  // Bound replay after a long absence. No reserve can bridge more than five days.
  if (today - state.day > READING_HISTORY_DAYS) {
    state.balance = state.enabled ? 0 : state.balance;
    state.progress = 0;
    state.streak = 0;
    state.day = today - READING_HISTORY_DAYS;
    state.readToday = false;
  }
  while (true) {
    if (hasRead(state.day, historyAnchor, history)) recordDay(state);
    if (state.day == today) break;
    if (!state.readToday) {
      state.progress = 0;
      if (state.enabled && state.streak != 0 && state.balance != 0) {
        --state.balance;
      } else {
        state.streak = 0;
      }
    }
    ++state.day;
    state.readToday = false;
  }
  state.readingSeconds = readingSeconds;

  // Catch up under the previous policy first, so enabling never protects an
  // absence that happened while tracking/fuel was disabled.
  if (state.capacity != policy.capacity || state.interval != policy.interval || state.enabled != policy.enabled()) {
    state.progress = 0;
    state.capacity = policy.capacity;
    state.interval = policy.interval;
    state.enabled = policy.enabled();
    if (state.capacity != 0) state.balance = std::min(state.balance, state.capacity);
  }
}
