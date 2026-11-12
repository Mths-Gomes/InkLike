#include "ReaderTitles.h"

#include "components/icons/readerTitleIcons.h"

namespace {
constexpr const freeink::Icon* kTitleIcons[] = {
    &icon_title_first_chapter_24, &icon_title_bookling_24,      &icon_title_explorer_24, &icon_title_page_turner_24,
    &icon_title_booklover_24,     &icon_title_book_devourer_24, &icon_title_bookworm_24, &icon_title_storykeeper_24,
    &icon_title_page_master_24,   &icon_title_legend_24,
};
static_assert(sizeof(kTitleIcons) / sizeof(kTitleIcons[0]) == ReaderTitles::count);
constexpr const freeink::Icon* kLargeTitleIcons[] = {
    &icon_title_first_chapter_32, &icon_title_bookling_32,      &icon_title_explorer_32, &icon_title_page_turner_32,
    &icon_title_booklover_32,     &icon_title_book_devourer_32, &icon_title_bookworm_32, &icon_title_storykeeper_32,
    &icon_title_page_master_32,   &icon_title_legend_32,
};
static_assert(sizeof(kLargeTitleIcons) / sizeof(kLargeTitleIcons[0]) == ReaderTitles::count);
}  // namespace

const freeink::Icon& ReaderTitles::iconFor(const uint32_t books, const int size) {
  return *(size >= 32 ? kLargeTitleIcons[indexFor(books)] : kTitleIcons[indexFor(books)]);
}
