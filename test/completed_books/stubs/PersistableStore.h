#pragma once
#include <ArduinoJson.h>
#include <HalStorage.h>

// Store semantics are tested with the real ArduinoJson codec. Atomic helper
// failure recovery itself is covered by test/persistable_store_atomic.
class PersistableStoreBase {
 public:
  static bool readDocFromFile(const char* path, JsonDocument& doc) {
    const std::string backup = std::string(path) + ".bak";
    if (!Storage.exists(path) && Storage.exists(backup.c_str())) Storage.rename(backup.c_str(), path);
    const auto it = Storage.files.find(path);
    return it != Storage.files.end() && !deserializeJson(doc, it->second);
  }
  static bool writeDocToFileAtomically(const char* path, const JsonDocument& doc) {
    if (Storage.failWrites > 0) {
      --Storage.failWrites;
      return false;
    }
    std::string output;
    serializeJson(doc, output);
    Storage.files[path] = output;
    ++Storage.writes;
    return true;
  }
};
