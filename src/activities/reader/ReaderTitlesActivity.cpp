#include "ReaderTitlesActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "GlobalReadingStats.h"
#include "ReaderTitles.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

namespace {
constexpr uint32_t kHistoryFirstPage = 2;  // Overview, then all title milestones.
constexpr int kTitleIconSize = 24;
constexpr int kTitleIconGap = 6;
}  // namespace

ReaderTitlesActivity::ReaderTitlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ReaderTitles", renderer, mappedInput) {}

uint32_t ReaderTitlesActivity::pageCount() const {
  constexpr uint32_t pageSize = static_cast<uint32_t>(CompletedBooksStore::PAGE_SIZE);
  return kHistoryFirstPage + (history.total == 0 ? 1 : 1 + (history.total - 1) / pageSize);
}

void ReaderTitlesActivity::onEnter() {
  Activity::onEnter();
  ignoreConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreBack = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignorePower = mappedInput.isPressed(MappedInputManager::Button::Power);
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  completedBooks = GlobalReadingStats::load().completedBooks;
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) currentYear = now.date.year;
  CompletedBooksStore::loadPage(0, currentYear, history);
  requestUpdate();
}

void ReaderTitlesActivity::onExit() {
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void ReaderTitlesActivity::changePage(const int delta) {
  const uint32_t next = delta < 0 ? (pageIndex == 0 ? 0 : pageIndex - 1) : std::min(pageIndex + 1, pageCount() - 1);
  if (next == pageIndex) return;
  RenderLock lock(*this);
  pageIndex = next;
  if (pageIndex >= kHistoryFirstPage)
    CompletedBooksStore::loadPage((pageIndex - kHistoryFirstPage) * CompletedBooksStore::PAGE_SIZE, currentYear,
                                  history);
  requestUpdate();
}

void ReaderTitlesActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm, ignoreConfirm) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Back, ignoreBack) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Power, ignorePower))
    return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    mappedInput.suppressNextBackRelease();
    finish();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up) || swipe == MappedInputManager::SwipeDir::Right) {
    changePage(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
             mappedInput.wasPressed(MappedInputManager::Button::Down) || swipe == MappedInputManager::SwipeDir::Left) {
    changePage(1);
  }
}

void ReaderTitlesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  int top, right, bottomInset, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottomInset, &left);
  const int safeBottom = std::min(safe.y + safe.height, renderer.getScreenHeight() - bottomInset);
  safe.x = std::max(safe.x, left);
  safe.y = std::max(safe.y, top);
  safe.width = std::max(1, renderer.getScreenWidth() - right - safe.x);
  safe.height = std::max(1, safeBottom - safe.y);
  const int x = safe.x + metrics.contentSidePadding;
  const int width = std::max(1, safe.width - metrics.contentSidePadding * 2);
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  int y = std::max(safe.y, CompactHeader::contentTop(metrics)) + metrics.verticalSpacing;
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READER_TITLES));
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READER_TITLES));
  }
  auto text = [&](const char* value, bool bold = false) {
    const auto visible =
        renderer.truncatedText(UI_10_FONT_ID, value, width, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, x, y, visible.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += line + 10;
  };
  char buffer[160];
  if (pageIndex == 0) {
    text(ReaderTitles::nameFor(completedBooks), true);
    snprintf(buffer, sizeof(buffer), "%lu %s", static_cast<unsigned long>(completedBooks), tr(STR_STATS_COMPLETED_LBL));
    text(buffer);
    const size_t level = ReaderTitles::indexFor(completedBooks);
    if (level + 1 < ReaderTitles::count) {
      const auto& next = ReaderTitles::levels[level + 1];
      snprintf(buffer, sizeof(buffer), tr(STR_TITLE_NEXT_FORMAT), I18N.get(next.name));
      text(buffer, true);
      snprintf(buffer, sizeof(buffer), tr(STR_TITLE_PROGRESS_FORMAT), static_cast<unsigned long>(completedBooks),
               static_cast<unsigned long>(next.minimumBooks),
               static_cast<unsigned long>(next.minimumBooks - completedBooks));
      text(buffer);
      renderer.drawRect(x, y, width, 12, true);
      const uint32_t base = ReaderTitles::levels[level].minimumBooks;
      const int fill =
          static_cast<int>((static_cast<uint64_t>(completedBooks - base) * (width - 4)) / (next.minimumBooks - base));
      if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, 8, true);
      y += 32;
    } else {
      text(tr(STR_TITLE_MAXIMUM));
    }
    text(tr(STR_TITLES_LIFETIME));
    text(tr(STR_HISTORY_PARTIAL));
    if (currentYear != 0) {
      snprintf(buffer, sizeof(buffer), tr(STR_HISTORY_YEAR_FORMAT), static_cast<unsigned>(currentYear),
               static_cast<unsigned long>(history.thisYear));
      text(buffer);
    }
  } else if (pageIndex == 1) {
    text(tr(STR_TITLE_LEVELS), true);
    snprintf(buffer, sizeof(buffer), "%lu+",
             static_cast<unsigned long>(ReaderTitles::levels[ReaderTitles::count - 1].minimumBooks));
    const int countWidth = renderer.getTextWidth(UI_10_FONT_ID, buffer, EpdFontFamily::BOLD);
    const int iconX = x + countWidth + 12;
    const int labelX = iconX + kTitleIconSize + kTitleIconGap;
    const int labelWidth = std::max(1, width - (labelX - x));
    const int rowHeight = std::max(line, kTitleIconSize) + 8;
    const size_t currentLevel = ReaderTitles::indexFor(completedBooks);
    for (size_t i = 0; i < ReaderTitles::count; ++i) {
      const auto style = i == currentLevel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const int textY = y + (rowHeight - line) / 2;
      snprintf(buffer, sizeof(buffer), "%lu+", static_cast<unsigned long>(ReaderTitles::levels[i].minimumBooks));
      renderer.drawText(UI_10_FONT_ID, x + countWidth - renderer.getTextWidth(UI_10_FONT_ID, buffer, style), textY,
                        buffer, true, style);
      drawLucideIcon(renderer, ReaderTitles::iconFor(ReaderTitles::levels[i].minimumBooks), iconX,
                     y + (rowHeight - kTitleIconSize) / 2);
      const auto visibleTitle =
          renderer.truncatedText(UI_10_FONT_ID, I18N.get(ReaderTitles::levels[i].name), labelWidth, style);
      renderer.drawText(UI_10_FONT_ID, labelX, textY, visibleTitle.c_str(), true, style);
      y += rowHeight;
    }
  } else {
    text(tr(STR_COMPLETED_HISTORY), true);
    if (!history.ok) text(tr(STR_HISTORY_READ_ERROR));
    if (history.total == 0) text(tr(STR_HISTORY_EMPTY));
    const int rowHeight = line + 8;
    for (uint8_t i = 0; i < history.size; ++i) {
      const auto& entry = history.entries[i];
      if (entry.date.isValid()) {
        snprintf(buffer, sizeof(buffer), " - %04u/%02u", static_cast<unsigned>(entry.date.year),
                 static_cast<unsigned>(entry.date.month));
      } else {
        snprintf(buffer, sizeof(buffer), " - %s", tr(STR_HISTORY_UNKNOWN_DATE));
      }
      // Reserve the date suffix before shortening a long title. Unknown-date
      // translations use at most half the row, leaving the book name visible.
      const auto visibleDate = renderer.truncatedText(UI_10_FONT_ID, buffer, std::max(1, width / 2));
      const int dateWidth = renderer.getTextWidth(UI_10_FONT_ID, visibleDate.c_str());
      const auto visibleTitle =
          renderer.truncatedText(UI_10_FONT_ID, entry.title, std::max(1, width - dateWidth), EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x, y, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID,
                        x + renderer.getTextWidth(UI_10_FONT_ID, visibleTitle.c_str(), EpdFontFamily::BOLD), y,
                        visibleDate.c_str());
      y += rowHeight;
    }
  }
  constexpr int squareSize = 8;
  constexpr int squareGap = 6;
  constexpr int squarePitch = squareSize + squareGap;
  const uint32_t totalPages = pageCount();
  const uint32_t visiblePages =
      std::min(totalPages, static_cast<uint32_t>(std::max(1, (width + squareGap) / squarePitch)));
  // Keep the current page visible even when a large history cannot fit in one row.
  const uint32_t firstPage =
      std::min(pageIndex > visiblePages / 2 ? pageIndex - visiblePages / 2 : 0, totalPages - visiblePages);
  const int squaresWidth = static_cast<int>(visiblePages) * squarePitch - squareGap;
  const int squaresX = x + (width - squaresWidth) / 2;
  const int squaresY = safeBottom - metrics.verticalSpacing - squareSize;
  for (uint32_t i = 0; i < visiblePages; ++i) {
    const int squareX = squaresX + static_cast<int>(i) * squarePitch;
    if (firstPage + i == pageIndex) {
      renderer.fillRect(squareX, squaresY, squareSize, squareSize, true);
    } else {
      renderer.drawRect(squareX, squaresY, squareSize, squareSize, true);
    }
  }
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", tr(STR_PREVIOUS), tr(STR_NEXT));
  // Empty labels draw an inactive outline in some themes; nullptr hides the
  // hint completely. Filter after mapping so custom button layouts still work.
  const auto visibleHint = [](const char* label) -> const char* { return label[0] != '\0' ? label : nullptr; };
  GUI.drawButtonHints(renderer, visibleHint(labels.btn1), visibleHint(labels.btn2), visibleHint(labels.btn3),
                      visibleHint(labels.btn4), true);
  renderer.displayBuffer();
}
