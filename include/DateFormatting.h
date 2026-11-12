#pragma once

#include <cstddef>
#include <cstdint>

// Format a known date using translated month names and the persisted date-format values.
bool formatDateParts(char* buf, size_t bufSize, uint16_t year, uint8_t month, uint8_t day, uint8_t dateFormat,
                     char numericSeparator = '/');

// Compact localized month/year, e.g. Nov/2026 or nov/2026 (no trailing month dot).
bool formatMonthYear(char* buf, size_t bufSize, uint16_t year, uint8_t month);
