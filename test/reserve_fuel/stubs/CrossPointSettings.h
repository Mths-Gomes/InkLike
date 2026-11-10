#pragma once
#include <cstdint>
struct TestSettings {
  uint8_t reserveFuelCapacity = 1;
  uint8_t reserveFuelInterval = 7;
  bool tracking = true;
  uint8_t clockUtcOffsetQ = 48;
  bool shouldTrackReadingStats() const { return tracking; }
};
inline TestSettings testSettings;
#define SETTINGS testSettings
