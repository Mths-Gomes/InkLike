#include "CompletedBooksStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstring>

namespace {
constexpr char kDirectory[] = "/.crosspoint/completed_books";
constexpr size_t kMaxTitleBytes = 512;
constexpr size_t kMaxRecordBytes = 4096;

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
}  // namespace

bool CompletedBooksStore::update(const std::string& cachePath, const std::string& title,
                                 const BookReadingStats& stats) {
  if (cachePath.empty()) return true;
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

void CompletedBooksStore::loadPage(const uint32_t offset, const uint16_t currentYear, Page& page) {
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
    }
    ++page.total;
  }
  dir.close();
}
