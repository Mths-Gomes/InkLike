#pragma once

#include <I18n.h>

#include <cstddef>
#include <cstdint>

namespace freeink {
struct Icon;
}

namespace ReaderTitles {
struct Level {
  uint32_t minimumBooks;
  StrId name;
};
inline constexpr Level levels[] = {
    {0, StrId::STR_TITLE_FIRST_CHAPTER}, {1, StrId::STR_TITLE_BOOKLING},      {3, StrId::STR_TITLE_EXPLORER},
    {5, StrId::STR_TITLE_PAGE_TURNER},   {10, StrId::STR_TITLE_BOOKLOVER},    {25, StrId::STR_TITLE_BOOK_DEVOURER},
    {50, StrId::STR_TITLE_BOOKWORM},     {100, StrId::STR_TITLE_STORYKEEPER}, {200, StrId::STR_TITLE_PAGE_MASTER},
    {500, StrId::STR_TITLE_LEGEND},
};
inline constexpr size_t count = sizeof(levels) / sizeof(levels[0]);
constexpr size_t indexFor(uint32_t books) {
  size_t index = 0;
  while (index + 1 < count && books >= levels[index + 1].minimumBooks) ++index;
  return index;
}
inline const char* nameFor(uint32_t books) { return I18N.get(levels[indexFor(books)].name); }
// Shared by the milestone page and Dashboard; bitmap assets are defined once.
const freeink::Icon& iconFor(uint32_t books);
}  // namespace ReaderTitles
