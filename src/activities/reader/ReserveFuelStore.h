#pragma once

#include "ReserveFuel.h"

struct GlobalReadingStats;

namespace ReserveFuelStore {
ReserveFuelState load();
enum class Horizon : uint8_t { CommittedHistory, Today };
// Main/settings tasks only; never called by a renderer. Serializes RF writers.
bool update(const GlobalReadingStats& stats, ReserveFuelState* snapshot = nullptr, Horizon horizon = Horizon::Today);
bool backup(const char* statsBackupPath);
void settingsSaved();
}  // namespace ReserveFuelStore
