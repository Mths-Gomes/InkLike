#include "ReaderTitlesActivity.h"

#include <DateFormatting.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BookStatsActivity.h"
#include "GlobalReadingStats.h"
#include "ReaderTitles.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

namespace {
using View = ReaderStatsNavigation::Screen;
constexpr freeink::ui::ActionId kOverviewAction = 1;
constexpr freeink::ui::ActionId kBookAction = 2;
constexpr int kTitleIconSize = 24;
constexpr int kTitleIconGap = 6;
constexpr int kOverviewIconSize = 32;
}  // namespace

ReaderTitlesActivity::ReaderTitlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ReaderStats", renderer, mappedInput), ui(renderer) {}

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
  ui.reset();
  const int secondaryFont = uiScaleSpec().bodyFontId == UI_10_FONT_ID ? SMALL_FONT_ID : UI_10_FONT_ID;
  ui.target.setFont(freeink::ui::GfxRendererTarget::FONT_LABEL, secondaryFont);
  ui.app.setScreen([](Screen& screen, void* context) { static_cast<ReaderTitlesActivity*>(context)->renderUi(screen); },
                   this);
  requestUpdate();
}

void ReaderTitlesActivity::onExit() {
  ui.closeRouting();
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void ReaderTitlesActivity::openSelection() {
  if (navigation.screen == View::FinishedBooks) {
    openSelectedBook();
    return;
  }
  if (navigation.screen != View::Overview) return;
  RenderLock lock(*this);
  navigation.open();
  statsUnavailable = false;
  ui.closeRouting();
  requestUpdate();
}

void ReaderTitlesActivity::moveSelection(const int delta) {
  if (navigation.screen == View::Titles) return;
  RenderLock lock(*this);
  navigation.move(delta, history.total);
  const uint32_t offset = navigation.pageOffset(CompletedBooksStore::PAGE_SIZE);
  if (navigation.screen == View::FinishedBooks && offset != historyOffset) {
    historyOffset = offset;
    CompletedBooksStore::loadPage(historyOffset, currentYear, history);
  }
  statsUnavailable = false;
  ui.closeRouting();
  requestUpdate();
}

void ReaderTitlesActivity::refreshHistory() {
  RenderLock lock(*this);
  char selectedId[32]{};
  if (navigation.selected >= historyOffset && navigation.selected - historyOffset < history.size)
    memcpy(selectedId, history.entries[navigation.selected - historyOffset].cacheId, sizeof(selectedId));
  completedBooks = GlobalReadingStats::load().completedBooks;
  uint32_t restoredIndex;
  CompletedBooksStore::loadPage(historyOffset, currentYear, history, selectedId, &restoredIndex);
  if (restoredIndex != UINT32_MAX) navigation.selected = restoredIndex;
  navigation.clamp(history.total);
  const uint32_t offset = navigation.pageOffset(CompletedBooksStore::PAGE_SIZE);
  if (offset != historyOffset) {
    historyOffset = offset;
    CompletedBooksStore::loadPage(historyOffset, currentYear, history);
  }
  ignoreConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreBack = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignorePower = mappedInput.isPressed(MappedInputManager::Button::Power);
  statsUnavailable = false;
  ui.closeRouting();
  requestUpdate();
}

void ReaderTitlesActivity::openSelectedBook() {
  if (navigation.screen != View::FinishedBooks || navigation.selected < historyOffset ||
      navigation.selected - historyOffset >= history.size)
    return;
  // Cold-path strings own the full metadata while the child is constructed;
  // the permanently resident page keeps only a bounded display title and ID.
  std::string cachePath, title;
  BookReadingStats stats;
  if (!CompletedBooksStore::loadDetails(history.entries[navigation.selected - historyOffset], cachePath, title)) {
    LOG_ERR("ReaderStats", "Statistics unavailable for selected completed book");
    RenderLock lock(*this);
    statsUnavailable = true;
    requestUpdate();
    return;
  }
  const bool available = BookReadingStats::tryLoad(cachePath, stats);
  const auto global = GlobalReadingStats::load();
  const float progress = stats.isCompleted ? 100.0f : -1.0f;
  std::unique_ptr<BookStatsActivity> child;
  if (GlobalReadingStats::hasSyncedStats()) {
    child = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, title, cachePath, stats, progress,
                                                 stats.estimatedTimeLeftSeconds != 0, stats.estimatedTimeLeftSeconds,
                                                 global, GlobalReadingStats::loadAggregated(global));
  } else {
    child = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, title, cachePath, stats, progress,
                                                 stats.estimatedTimeLeftSeconds != 0, stats.estimatedTimeLeftSeconds,
                                                 global);
  }
  if (!child) {
    LOG_ERR("ReaderStats", "Could not allocate book statistics activity");
    return;
  }
  child->enableHistoryActions(available);
  ui.closeRouting();
  startActivityForResult(std::move(child), [this](const ActivityResult& result) {
    const auto* changed = std::get_if<ReadingStatsResult>(&result.data);
    if (changed && changed->changed) {
      setResult(ReadingStatsResult{true});
      refreshHistory();
    } else {
      // Keep the cached page and exact selection when nothing was edited.
      ignoreConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
      ignoreBack = mappedInput.isPressed(MappedInputManager::Button::Back);
      ignorePower = mappedInput.isPressed(MappedInputManager::Button::Power);
      requestUpdate();
    }
  });
}

void ReaderTitlesActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm, ignoreConfirm) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Back, ignoreBack) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Power, ignorePower))
    return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    mappedInput.suppressNextBackRelease();
    RenderLock lock(*this);
    if (navigation.back()) {
      statsUnavailable = false;
      ui.closeRouting();
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  freeink::ui::ActionEvent event;
  if (ui.routeTouch(mappedInput, event) && event) {
    if (event.action == kOverviewAction && navigation.screen == View::Overview && event.value >= 0 && event.value < 2) {
      {
        RenderLock lock(*this);
        navigation.overviewSelection = event.value;
      }
      openSelection();
    } else if (event.action == kBookAction && navigation.screen == View::FinishedBooks && event.value >= 0 &&
               event.value < history.size) {
      {
        RenderLock lock(*this);
        navigation.selected = historyOffset + event.value;
      }
      openSelection();
    }
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasPressed(mappedInput.menuButton(MappedInputManager::Button::Up)) ||
      mappedInput.wasPressed(mappedInput.menuButton(MappedInputManager::Button::Left)) ||
      swipe == MappedInputManager::SwipeDir::Down) {
    moveSelection(-1);
  } else if (mappedInput.wasPressed(mappedInput.menuButton(MappedInputManager::Button::Down)) ||
             mappedInput.wasPressed(mappedInput.menuButton(MappedInputManager::Button::Right)) ||
             swipe == MappedInputManager::SwipeDir::Up) {
    moveSelection(1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelection();
  }
}

void ReaderTitlesActivity::historyRow(void* context, const uint16_t index, freeink::ui::ListItem& item) {
  auto& self = *static_cast<ReaderTitlesActivity*>(context);
  if (index >= self.history.size) return;
  const auto& entry = self.history.entries[index];
  if (entry.date.isValid()) {
    formatMonthYear(self.dateLabel, sizeof(self.dateLabel), entry.date.year, entry.date.month);
  } else {
    snprintf(self.dateLabel, sizeof(self.dateLabel), "%s", tr(STR_HISTORY_UNKNOWN_DATE));
  }
  item.label = entry.title;
  item.value = self.dateLabel;
  item.actionValue = index;
}

void ReaderTitlesActivity::renderUi(Screen& screen) {
  namespace fui = freeink::ui;
  if (navigation.screen == View::Overview) {
    renderOverview(screen);
    return;
  }
  if (navigation.screen == View::Titles) {
    renderTitles();
    return;
  }
  if (history.size == 0) return;
  fui::ListProps list;
  list.count = history.size;
  list.rowProvider = &ReaderTitlesActivity::historyRow;
  list.rowProviderCtx = this;
  list.selectedIndex = static_cast<int16_t>(navigation.selected - historyOffset);
  list.action = kBookAction;
  list.inputMask = fui::InputTouch;
  list.labelText = screen.theme().bodyText;
  list.labelText.bold = false;
  list.labelText.maxLines = 1;
  list.valueText = screen.theme().bodyText;
  list.rowHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  list.rowGap = 0;
  list.sidePadding = 4;
  list.rowInset = 0;
  list.scrollIndicator = false;
  list.partialTrailingRow = false;
  const int y = contentBounds.y;
  const int height = std::max(1, contentBounds.height - renderer.getLineHeight(UI_10_FONT_ID) - 42);
  fui::list(screen.frame(),
            fui::Rect{static_cast<int16_t>(contentBounds.x), static_cast<int16_t>(y),
                      static_cast<int16_t>(contentBounds.width), static_cast<int16_t>(height)},
            screen.resolveListProps(list));
}

void ReaderTitlesActivity::renderOverview(Screen& screen) {
  namespace fui = freeink::ui;
  const int x = contentBounds.x;
  const int width = contentBounds.width;
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int gap = std::max(12, std::min(24, contentBounds.height / 22));
  int y = contentBounds.y + gap;
  char buffer[128];
  renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_READER_CURRENT_TITLE));
  y += line + 8;

  // Reuse one fixed SDK row for each entry, so focus, inversion, icons, and
  // touch hit areas have the same geometry. No new activity or page buffer.
  fui::ListItem item;
  fui::ListProps props;
  props.items = &item;
  props.count = 1;
  props.action = kOverviewAction;
  props.inputMask = fui::InputTouch;
  props.rowGap = 0;
  props.sidePadding = 4;
  props.rowInset = 0;
  props.textGap = 6;
  props.scrollIndicator = false;
  props.labelText = screen.theme().bodyText;
  props.labelText.font = fui::GfxRendererTarget::FONT_TITLE;
  props.labelText.bold = false;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.font = fui::GfxRendererTarget::FONT_LABEL;
  props.subtitleText.bold = false;
  props.iconSize = kOverviewIconSize;
  props.rowHeight =
      screen.target().lineHeight(props.labelText.font) + screen.target().lineHeight(props.subtitleText.font) + 24;
  props.selectedIndex = navigation.overviewSelection == 0 ? 0 : -1;
  item.label = ReaderTitles::nameFor(completedBooks);
  item.subtitle = tr(STR_READER_VIEW_TITLES);
  item.icon = fui::bitmapFromIcon(ReaderTitles::iconFor(completedBooks, kOverviewIconSize));
  item.actionValue = 0;
  auto rect = fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width), props.rowHeight};
  fui::list(screen.frame(), rect, screen.resolveListProps(props));
  y += props.rowHeight + gap;

  snprintf(buffer, sizeof(buffer), tr(STR_READER_FINISHED_COUNT), static_cast<unsigned long>(history.total));
  item.label = tr(STR_READER_FINISHED_BOOKS);
  item.subtitle = buffer;
  item.icon = listIconFor(UIIcon::Book);
  item.actionValue = 1;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.iconSize = 24;
  props.selectedIndex = navigation.overviewSelection == 1 ? 0 : -1;
  props.rowHeight =
      screen.target().lineHeight(props.labelText.font) + screen.target().lineHeight(props.subtitleText.font) + 24;
  rect.y = static_cast<int16_t>(y);
  rect.height = props.rowHeight;
  fui::list(screen.frame(), rect, screen.resolveListProps(props));
  y += props.rowHeight + gap * 2;
  renderer.drawLine(x, y, x + width, y, true);
  y += gap;
  const size_t level = ReaderTitles::indexFor(completedBooks);
  if (level + 1 < ReaderTitles::count) {
    const auto& next = ReaderTitles::levels[level + 1];
    snprintf(buffer, sizeof(buffer), tr(STR_TITLE_NEXT_FORMAT), I18N.get(next.name));
    const auto visible = renderer.truncatedText(UI_10_FONT_ID, buffer, width);
    renderer.drawText(UI_10_FONT_ID, x, y, visible.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID) + 10;
    snprintf(buffer, sizeof(buffer), tr(STR_READER_XP_FORMAT), static_cast<unsigned long>(completedBooks),
             static_cast<unsigned long>(next.minimumBooks));
    renderer.drawText(SMALL_FONT_ID, x, y, buffer);
    y += renderer.getLineHeight(SMALL_FONT_ID) + 12;
    renderer.drawRect(x, y, width, 14, true);
    const uint32_t base = ReaderTitles::levels[level].minimumBooks;
    const int fill =
        static_cast<int>((static_cast<uint64_t>(completedBooks - base) * (width - 4)) / (next.minimumBooks - base));
    if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, 10, true);
    y += 14;
  } else {
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_TITLE_MAXIMUM));
    y += line;
  }
}

void ReaderTitlesActivity::renderTitles() {
  const int x = contentBounds.x;
  const int width = contentBounds.width;
  const int line = renderer.getLineHeight(UI_12_FONT_ID);
  int y = contentBounds.y + 8;
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu+",
           static_cast<unsigned long>(ReaderTitles::levels[ReaderTitles::count - 1].minimumBooks));
  const int countWidth = renderer.getTextWidth(UI_12_FONT_ID, buffer, EpdFontFamily::BOLD);
  const int iconX = x + countWidth + 12;
  const int labelX = iconX + kTitleIconSize + kTitleIconGap;
  const int labelWidth = std::max(1, width - (labelX - x));
  const int rowHeight = std::max(line, kTitleIconSize) + 8;
  const size_t currentLevel = ReaderTitles::indexFor(completedBooks);
  for (size_t i = 0; i < ReaderTitles::count; ++i) {
    const auto style = i == currentLevel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textY = y + (rowHeight - line) / 2;
    snprintf(buffer, sizeof(buffer), "%lu+", static_cast<unsigned long>(ReaderTitles::levels[i].minimumBooks));
    renderer.drawText(UI_12_FONT_ID, x + countWidth - renderer.getTextWidth(UI_12_FONT_ID, buffer, style), textY,
                      buffer, true, style);
    drawLucideIcon(renderer, ReaderTitles::iconFor(ReaderTitles::levels[i].minimumBooks, kTitleIconSize), iconX,
                   y + (rowHeight - kTitleIconSize) / 2);
    const auto visible =
        renderer.truncatedText(UI_12_FONT_ID, I18N.get(ReaderTitles::levels[i].name), labelWidth, style);
    renderer.drawText(UI_12_FONT_ID, labelX, textY, visible.c_str(), true, style);
    y += rowHeight;
  }
}

void ReaderTitlesActivity::drawHistoryPages() {
  const auto pages = navigation.pageWindow(history.total, CompletedBooksStore::PAGE_SIZE);
  if (pages.count == 0) return;
  constexpr int size = 8;
  constexpr int pitch = 16;
  // Always reserve a dot slot on both sides, keeping the three-square window
  // centered as overflow appears/disappears. The current square is black.
  const int width = (pages.count + 2) * pitch - (pitch - size);
  const int x = contentBounds.x + (contentBounds.width - width) / 2;
  const int y = contentBounds.y + contentBounds.height - size - 6;
  if (pages.before) renderer.fillRect(x + 3, y + 3, 2, 2, true);
  for (uint8_t i = 0; i < pages.count; ++i) {
    const int sx = x + (i + 1) * pitch;
    if (pages.first + i == navigation.selected / CompletedBooksStore::PAGE_SIZE)
      renderer.fillRect(sx, y, size, size, true);
    else
      renderer.drawRect(sx, y, size, size, true);
  }
  if (pages.after) renderer.fillRect(x + (pages.count + 1) * pitch + 3, y + 3, 2, 2, true);
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
  const int padding = navigation.screen == View::FinishedBooks ? 12 : metrics.contentSidePadding;
  const int x = safe.x + padding;
  const int width = std::max(1, safe.width - padding * 2);
  const int y = std::max(safe.y, CompactHeader::contentTop(metrics)) + metrics.verticalSpacing;
  contentBounds = Rect(x, y, width, std::max(1, safeBottom - y));
  ui.render();
  const char* title = navigation.screen == View::Overview ? tr(STR_READER_TITLES)
                      : navigation.screen == View::Titles ? tr(STR_TITLE_LEVELS)
                                                          : tr(STR_READER_FINISHED_BOOKS);
  if (mappedInput.hasTouchHardware())
    TouchHeaderBackButton::drawCompact(renderer, title);
  else
    CompactHeader::drawTitle(renderer, title);

  const bool listing = navigation.screen == View::FinishedBooks;
  if (listing) {
    if (history.total == 0) renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_HISTORY_EMPTY));
    if (statsUnavailable || !history.ok) {
      const auto warning = renderer.truncatedText(
          UI_10_FONT_ID, statsUnavailable ? tr(STR_HISTORY_STATS_UNAVAILABLE) : tr(STR_HISTORY_READ_ERROR), width);
      renderer.drawText(UI_10_FONT_ID, x, safeBottom - renderer.getLineHeight(UI_10_FONT_ID) - 26, warning.c_str());
    }
    drawHistoryPages();
  }
  const bool interactive = navigation.screen != View::Titles;
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), interactive ? tr(STR_OPEN) : "",
                                            interactive ? tr(STR_DIR_UP) : "", interactive ? tr(STR_DIR_DOWN) : "");
  const auto visibleHint = [](const char* label) -> const char* { return label[0] != '\0' ? label : nullptr; };
  GUI.drawButtonHints(renderer, visibleHint(labels.btn1), visibleHint(labels.btn2), visibleHint(labels.btn3),
                      visibleHint(labels.btn4), true);
  renderer.displayBuffer();
}
