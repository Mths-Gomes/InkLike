#include "CompletedBooksStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstring>

#include "GlobalReadingStats.h"

namespace {
constexpr char kDirectory[] = "/.crosspoint/completed_books";
constexpr size_t kMaxTitleBytes = 512;
constexpr size_t kMaxRecordBytes = 4096;
constexpr char kDeletionJournal[] = "/.crosspoint/completed_books/.deleting.json";

bool validCacheId(const char* id) {
  if (strncmp(id, "epub_", 5) != 0 && strncmp(id, "xtc_", 4) != 0) return false;
  const size_t length = strlen(id);
  if (length >= sizeof(CompletedBooksStore::Entry::cacheId)) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!(c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;
}

size_t utf8PrefixLength(const char* text, const size_t length, const size_t maximum) {
  size_t bytes = length < maximum ? length : maximum;
  while (bytes < length && bytes > 0 && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) --bytes;
  return bytes;
}

bool recordExists(const std::string& path) {
  return Storage.exists(path.c_str()) || Storage.exists((path + ".bak").c_str());
}

bool readRecord(const std::string& path, JsonDocument& doc, const bool recover = true) {
  const std::string source = Storage.exists(path.c_str()) ? path : path + ".bak";
  if (!Storage.exists(source.c_str())) return false;
  FsFile file = Storage.open(source.c_str());
  const bool bounded = file && !file.isDirectory() && file.fileSize() <= kMaxRecordBytes;
  file.close();
  if (!bounded) {
    LOG_ERR("BOOKLOG", "Unreadable or oversized history: %s", source.c_str());
    return false;
  }
  // Directory scans must not rename entries underneath their own iterator.
  return PersistableStoreBase::readDocFromFile(recover ? path.c_str() : source.c_str(), doc);
}

std::string recordPath(const std::string& cachePath) {
  const size_t slash = cachePath.find_last_of('/');
  return std::string(kDirectory) + "/" + cachePath.substr(slash == std::string::npos ? 0 : slash + 1) + ".json";
}

ReadingStatsDate dateFrom(const JsonDocument& doc) {
  ReadingStatsDate date{doc["year"] | uint16_t{0}, doc["month"] | uint8_t{0}, doc["day"] | uint8_t{0}};
  if (!date.isValid()) date.clear();
  return date;
}

bool removeRecordFiles(const std::string& path) {
  // Recovery sources first; leave the canonical file if any cleanup fails.
  for (const char* suffix : {".tmp", ".bak", ""}) {
    const std::string source = path + suffix;
    if (Storage.exists(source.c_str()) && !Storage.remove(source.c_str())) {
      LOG_ERR("BOOKLOG", "Could not remove history file: %s", source.c_str());
      return false;
    }
  }
  return true;
}
}  // namespace

bool CompletedBooksStore::recoverPendingDeletion() {
  if (!recordExists(kDeletionJournal)) return GlobalReadingStats::clearHistoryRemovalReceipt();
  JsonDocument journal;
  if (!readRecord(kDeletionJournal, journal) || (journal["version"] | 0) != 1 ||
      !validCacheId(journal["cacheId"] | "")) {
    LOG_ERR("BOOKLOG", "Could not recover history deletion journal");
    return false;
  }
  const std::string cachePath = std::string("/.crosspoint/") + (journal["cacheId"] | "");
  if (!BookReadingStats::remove(cachePath)) return false;
  if ((journal["counted"] | false) && !GlobalReadingStats::commitHistoryRemoval()) return false;
  if (!removeRecordFiles(recordPath(cachePath))) return false;
  // Remove the journal before its receipt. A leftover receipt can be discarded
  // next time, whereas a remaining journal must still see the consumed AFTER file.
  if (!removeRecordFiles(kDeletionJournal)) return false;
  return GlobalReadingStats::clearHistoryRemovalReceipt();
}

bool CompletedBooksStore::eraseHistory(const std::string& cachePath) {
  if (!recoverPendingDeletion()) return false;
  const std::string path = recordPath(cachePath);
  JsonDocument record;
  if (!readRecord(path, record)) return !recordExists(path);
  if ((record["version"] | 0) != 1) {
    LOG_ERR("BOOKLOG", "Unsupported history version during deletion: %s", path.c_str());
    return false;
  }
  const size_t slash = cachePath.find_last_of('/');
  const std::string id = cachePath.substr(slash == std::string::npos ? 0 : slash + 1);
  if (cachePath != std::string("/.crosspoint/") + id || !validCacheId(id.c_str())) {
    LOG_ERR("BOOKLOG", "Invalid history deletion reference");
    return false;
  }
  JsonDocument journal;
  journal["version"] = 1;
  journal["cacheId"] = id;
  journal["counted"] = record["completed"] | false;
  if (journal.overflowed() || !PersistableStoreBase::writeDocToFileAtomically(kDeletionJournal, journal)) {
    LOG_ERR("BOOKLOG", "Could not prepare history deletion journal");
    return false;
  }
  return recoverPendingDeletion();
}

bool CompletedBooksStore::deletionPendingFor(const std::string& cachePath) {
  if (!recordExists(kDeletionJournal)) return false;
  JsonDocument journal;
  if (!readRecord(kDeletionJournal, journal)) return true;
  return cachePath == std::string("/.crosspoint/") + (journal["cacheId"] | "");
}

bool CompletedBooksStore::update(const std::string& cachePath, const std::string& title,
                                 const BookReadingStats& stats) {
  if (cachePath.empty()) return true;
  if (deletionPendingFor(cachePath)) {
    LOG_ERR("BOOKLOG", "Deferring history update until pending deletion completes");
    return false;
  }
  const std::string path = recordPath(cachePath);
  // Cold-path temporary documents contain one title (at most 512 bytes) and
  // scalar fields, rather than a growing library-wide array on the C3 heap.
  JsonDocument previous;
  const bool existed = readRecord(path, previous);
  if (!existed && recordExists(path)) {
    LOG_ERR("BOOKLOG", "Keeping unreadable history record: %s", path.c_str());
    return false;
  }
  if (!existed && !stats.isCompleted) return true;
  if (existed && (previous["version"] | 0) != 1) {
    LOG_ERR("BOOKLOG", "Unsupported history version: %s", path.c_str());
    return false;
  }
  const ReadingStatsDate date = stats.finishedDate.isValid() ? stats.finishedDate : ReadingStatsDate{};
  const ReadingStatsDate oldDate = dateFrom(previous);
  // Truncate only unusually long metadata, at a UTF-8 codepoint boundary.
  const size_t titleBytes = utf8PrefixLength(title.c_str(), title.size(), kMaxTitleBytes);
  const std::string boundedTitle = title.substr(0, titleBytes);
  if (existed && (previous["completed"] | false) == stats.isCompleted &&
      strcmp(previous["title"] | "", boundedTitle.c_str()) == 0 && compareReadingStatsDate(oldDate, date) == 0)
    return true;
  if (!Storage.mkdir(kDirectory) && !Storage.exists(kDirectory)) {
    LOG_ERR("BOOKLOG", "Could not create history directory");
    return false;
  }
  previous.clear();
  previous["version"] = 1;
  previous["title"] = boundedTitle.c_str();
  previous["completed"] = stats.isCompleted;
  if (date.isValid()) {
    previous["year"] = date.year;
    previous["month"] = date.month;
    previous["day"] = date.day;
  } else {
    previous["year"] = nullptr;
    previous["month"] = nullptr;
    previous["day"] = nullptr;
  }
  if (previous.overflowed()) {
    LOG_ERR("BOOKLOG", "Could not allocate history document");
    return false;
  }
  return PersistableStoreBase::writeDocToFileAtomically(path.c_str(), previous);
}

bool CompletedBooksStore::migrate(const std::string& oldCachePath, const std::string& newCachePath) {
  const std::string oldPath = recordPath(oldCachePath);
  const std::string newPath = recordPath(newCachePath);
  if (oldPath == newPath) return true;
  JsonDocument doc;
  if (!readRecord(oldPath, doc)) return !recordExists(oldPath);
  // A stale backup must not become a second live record after the canonical
  // file moves. Only remove it while the canonical source is still present.
  const std::string backup = oldPath + ".bak";
  if (Storage.exists(oldPath.c_str()) && Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) {
    LOG_ERR("BOOKLOG", "Could not remove stale history backup: %s", backup.c_str());
    return false;
  }
  if (recordExists(newPath) || !Storage.rename(oldPath.c_str(), newPath.c_str())) {
    LOG_ERR("BOOKLOG", "Could not migrate history: %s -> %s", oldPath.c_str(), newPath.c_str());
    return false;
  }
  return true;
}

void CompletedBooksStore::loadPage(const uint32_t offset, const uint16_t currentYear, Page& page,
                                   const char* selectedCacheId, uint32_t* selectedIndex) {
  if (selectedIndex) *selectedIndex = UINT32_MAX;
  page.size = 0;
  page.total = page.thisYear = page.unknownYear = 0;
  page.ok = true;
  if (!Storage.exists(kDirectory)) return;
  FsFile dir = Storage.open(kDirectory);
  if (!dir || !dir.isDirectory()) {
    dir.close();
    LOG_ERR("BOOKLOG", "Could not open history directory");
    page.ok = false;
    return;
  }
  JsonDocument doc;
  char name[96];
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool directory = file.isDirectory();
    const size_t length = file.getName(name, sizeof(name));
    file.close();  // Never reopen a record while its directory entry is open.
    if (directory || length == 0 || length >= sizeof(name)) continue;
    // Include recoverable backups if a replacement was interrupted; ignore
    // backups whose canonical record still exists and unfinished .tmp files.
    std::string path = std::string(kDirectory) + "/" + name;
    if (path.size() > 9 && path.compare(path.size() - 9, 9, ".json.bak") == 0) {
      path.resize(path.size() - 4);
      if (Storage.exists(path.c_str())) continue;
    } else if (path.size() < 5 || path.compare(path.size() - 5, 5, ".json") != 0) {
      continue;
    }
    doc.clear();
    if (!readRecord(path, doc, false) || (doc["version"] | 0) != 1) {
      LOG_ERR("BOOKLOG", "Could not read history record: %s", path.c_str());
      page.ok = false;
      continue;
    }
    if (!(doc["completed"] | false)) continue;
    const size_t idStart = path.find_last_of('/') + 1;
    const size_t idLength = path.size() - idStart - 5;
    if (selectedCacheId && selectedIndex && strlen(selectedCacheId) == idLength &&
        path.compare(idStart, idLength, selectedCacheId) == 0)
      *selectedIndex = page.total;
    const ReadingStatsDate date = dateFrom(doc);
    if (!date.isValid())
      ++page.unknownYear;
    else if (currentYear != 0 && date.year == currentYear)
      ++page.thisYear;
    if (page.total >= offset && page.size < PAGE_SIZE) {
      auto& entry = page.entries[page.size++];
      const char* title = doc["title"] | "";
      const size_t bytes = utf8PrefixLength(title, strlen(title), sizeof(entry.title) - 1);
      memcpy(entry.title, title, bytes);
      entry.title[bytes] = '\0';
      entry.date = date;
      const std::string id = path.substr(idStart, idLength);
      entry.cacheId[0] = '\0';
      if (validCacheId(id.c_str())) memcpy(entry.cacheId, id.c_str(), id.size() + 1);
    }
    ++page.total;
  }
  dir.close();
}

bool CompletedBooksStore::loadDetails(const Entry& entry, std::string& cachePath, std::string& title) {
  if (!validCacheId(entry.cacheId)) {
    LOG_ERR("BOOKLOG", "Invalid history cache reference");
    return false;
  }
  JsonDocument doc;
  const std::string path = std::string(kDirectory) + "/" + entry.cacheId + ".json";
  if (!readRecord(path, doc) || (doc["version"] | 0) != 1 || !(doc["completed"] | false)) {
    LOG_ERR("BOOKLOG", "Could not open selected history record: %s", path.c_str());
    return false;
  }
  cachePath = std::string("/.crosspoint/") + entry.cacheId;
  const char* storedTitle = doc["title"] | "";
  title.assign(storedTitle, utf8PrefixLength(storedTitle, strlen(storedTitle), kMaxTitleBytes));
  return true;
}
