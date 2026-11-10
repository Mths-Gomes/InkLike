#pragma once
#include <cstdint>
struct TestClock {
  uint16_t year = 2026;
  uint8_t month = 11, day = 1, hour = 12, minute = 0;
  bool valid = true;
  bool getDateTime(uint16_t& y, uint8_t& m, uint8_t& d, uint8_t& h, uint8_t& min) const {
    y = year;
    m = month;
    d = day;
    h = hour;
    min = minute;
    return valid;
  }
};
inline TestClock halClock;
