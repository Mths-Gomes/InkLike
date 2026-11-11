#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class HalFile {
 public:
  bool valid = false;
  bool directory = false;
  size_t bytes = 0;
  std::string name;
  std::vector<HalFile> children;
  size_t cursor = 0;
  explicit operator bool() const { return valid; }
  bool isDirectory() const { return directory; }
  size_t fileSize() const { return bytes; }
  void close() { valid = false; }
  size_t getName(char* out, size_t capacity) const {
    if (capacity) {
      std::strncpy(out, name.c_str(), capacity - 1);
      out[capacity - 1] = 0;
    }
    return name.size();
  }
  HalFile openNextFile() { return cursor < children.size() ? children[cursor++] : HalFile{}; }
};
using FsFile = HalFile;

class HalStorage {
 public:
  std::map<std::string, std::string> files;
  unsigned writes = 0;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool mkdir(const char* path) {
    files.try_emplace(path, "<directory>");
    return true;
  }
  bool rename(const char* from, const char* to) {
    auto node = files.extract(from);
    if (node.empty() || exists(to)) return false;
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
  HalFile open(const char* path) {
    const auto found = files.find(path);
    if (found == files.end()) return {};
    HalFile file;
    file.valid = true;
    file.directory = found->second == "<directory>";
    file.bytes = found->second.size();
    file.name = found->first.substr(found->first.find_last_of('/') + 1);
    if (file.directory) {
      const std::string prefix = found->first + "/";
      for (const auto& [name, content] : files) {
        if (name.compare(0, prefix.size(), prefix) == 0 && name.find('/', prefix.size()) == std::string::npos)
          file.children.push_back(open(name.c_str()));
      }
    }
    return file;
  }
};
inline HalStorage Storage;
