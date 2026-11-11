#include "ReaderTitles.h"

#include "components/icons/readerTitleIcons.h"

namespace {
constexpr const freeink::Icon* kTitleIcons[] = {
    &icon_title_first_chapter_24, &icon_title_bookling_24,      &icon_title_explorer_24, &icon_title_page_turner_24,
    &icon_title_booklover_24,     &icon_title_book_devourer_24, &icon_title_bookworm_24, &icon_title_storykeeper_24,
    &icon_title_page_master_24,   &icon_title_legend_24,
};
static_assert(sizeof(kTitleIcons) / sizeof(kTitleIcons[0]) == ReaderTitles::count);
}  // namespace

const freeink::Icon& ReaderTitles::iconFor(const uint32_t books) { return *kTitleIcons[indexFor(books)]; }
