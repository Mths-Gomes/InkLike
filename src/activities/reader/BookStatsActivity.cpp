#include "BookStatsActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "BookStatsTracking.h"
#include "BookStatsView.h"
#include "CompletedBooksStore.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

namespace {

void drawPageIndicators(const GfxRenderer& renderer, const int currentPage, const int totalPages) {
  if (totalPages <= 1) {
    return;
  }

  constexpr int kDotSize = 8;
  constexpr int kDotSpacing = 6;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int totalDotWidth = totalPages * kDotSize + (totalPages - 1) * kDotSpacing;
  const int dotsStartX = (renderer.getScreenWidth() - totalDotWidth) / 2;
  const int dotY = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
  for (int pageIndex = 0; pageIndex < totalPages; ++pageIndex) {
    const int dotX = dotsStartX + pageIndex * (kDotSize + kDotSpacing);
    if (pageIndex == currentPage) {
      renderer.fillRect(dotX, dotY, kDotSize, kDotSize, true);
    } else {
      renderer.drawRect(dotX, dotY, kDotSize, kDotSize, true);
    }
  }
}

}  // namespace

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const bool returnToHomeOnExit)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds) {}

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const GlobalReadingStats& allDevicesStats, const bool returnToHomeOnExit)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      allDevicesStats(allDevicesStats),
      showAllDevicesStats(true),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds) {}

void BookStatsActivity::refreshAllDevicesStats() {
  if (showAllDevicesStats) {
    allDevicesStats = GlobalReadingStats::loadAggregated(globalStats);
  }
}

void BookStatsActivity::saveStats() {
  if (!didChangeStats || !hasEditableBook()) {
    return;
  }

  if (!stats.save(bookCachePath)) {
    LOG_ERR("BookStats", "Could not save book stats");
    return;
  }
  globalStats.save();
  if (!CompletedBooksStore::update(bookCachePath, bookTitle, stats)) {
    LOG_ERR("BookStats", "Could not update completion history");
  }
  refreshAllDevicesStats();
  didChangeStats = false;
}

void BookStatsActivity::beginDateEditing() {
  dateEditStatsSnapshot = stats;
  dateEditGlobalStatsSnapshot = globalStats;
  didChangeStatsBeforeDateEdit = didChangeStats;
  dateEditSnapshotValid = true;
  page = Page::EditDates;
  requestUpdate();
}

void BookStatsActivity::finishDateEditing(const bool saveChanges) {
  if (saveChanges) {
    saveStats();
  } else if (dateEditSnapshotValid) {
    stats = dateEditStatsSnapshot;
    globalStats = dateEditGlobalStatsSnapshot;
    didChangeStats = didChangeStatsBeforeDateEdit;
    setResult(ReadingStatsResult{didChangeStats});
  }

  dateEditSnapshotValid = false;
  page = Page::PerBook;
  requestUpdate();
}

void BookStatsActivity::cycleEditField() { selectedEditField = (selectedEditField + 1) % 6; }

ReadingStatsDate BookStatsActivity::defaultDateForField(const bool finishedField) const {
  if (finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }
  if (!finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (!finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }

  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    return now.date;
  }
  return {2000, 1, 1};
}

void BookStatsActivity::applyCompletedState(const bool completed) {
  if (stats.isCompleted == completed) {
    return;
  }

  stats.isCompleted = completed;
  if (completed) {
    globalStats.completedBooks++;
    if (!stats.finishedDateManual && !stats.finishedDate.isValid()) {
      ReadingStatsDateTime now;
      if (getCurrentLocalReadingStatsDateTime(now)) {
        stats.finishedDate = now.date;
      }
    }
  } else if (globalStats.completedBooks > 0) {
    globalStats.completedBooks--;
  }
}

void BookStatsActivity::normalizeEditedDates(const bool editedFinishedField) {
  if (!stats.startDate.isValid() || !stats.finishedDate.isValid()) {
    return;
  }
  if (compareReadingStatsDate(stats.finishedDate, stats.startDate) >= 0) {
    return;
  }

  if (editedFinishedField) {
    stats.startDate = stats.finishedDate;
  } else {
    stats.finishedDate = stats.startDate;
  }
}

void BookStatsActivity::clearEditedDate(const bool finishedField) {
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;
  date.clear();

  if (finishedField) {
    stats.finishedDateManual = false;
    applyCompletedState(false);
  } else {
    stats.startDateManual = false;
  }

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

bool BookStatsActivity::shouldClearDateOnAdjust(const ReadingStatsDate& date, const bool finishedField,
                                                const int fieldIndex, const int delta) const {
  if (!date.isValid()) {
    return false;
  }

  switch (fieldIndex) {
    case 0:
      return (date.month == 1 && delta < 0) || (date.month == 12 && delta > 0);
    case 1: {
      const uint8_t monthDays = daysInMonth(date.year, date.month);
      return (date.day == 1 && delta < 0) || (date.day == monthDays && delta > 0);
    }
    case 2:
      return (date.year == 2000 && delta < 0) || (date.year == 2099 && delta > 0);
    default:
      return false;
  }
}

void BookStatsActivity::adjustSelectedDateField(const int delta) {
  const bool finishedField = selectedEditField >= 3;
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;
  const int fieldIndex = selectedEditField % 3;

  if (shouldClearDateOnAdjust(date, finishedField, fieldIndex, delta)) {
    clearEditedDate(finishedField);
    return;
  }

  if (!date.isValid()) {
    date = defaultDateForField(finishedField);
  }

  switch (fieldIndex) {
    case 0: {
      int month = static_cast<int>(date.month) + delta;
      while (month < 1) {
        month += 12;
      }
      while (month > 12) {
        month -= 12;
      }
      date.month = static_cast<uint8_t>(month);
      break;
    }
    case 1: {
      const int monthDays = daysInMonth(date.year, date.month);
      int day = static_cast<int>(date.day) + delta;
      while (day < 1) {
        day += monthDays;
      }
      while (day > monthDays) {
        day -= monthDays;
      }
      date.day = static_cast<uint8_t>(day);
      break;
    }
    case 2: {
      int year = static_cast<int>(date.year) + delta;
      if (year < 2000) {
        year = 2099;
      } else if (year > 2099) {
        year = 2000;
      }
      date.year = static_cast<uint16_t>(year);
      break;
    }
  }

  const uint8_t monthDays = daysInMonth(date.year, date.month);
  if (date.day > monthDays) {
    date.day = monthDays;
  }

  if (finishedField) {
    stats.finishedDateManual = true;
    applyCompletedState(true);
  } else {
    stats.startDateManual = true;
  }
  normalizeEditedDates(finishedField);

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  if (historyActions) historyDeletionFailed = CompletedBooksStore::deletionPendingFor(bookCachePath);
  if (!historyActions && stats.isCompleted && !bookCachePath.empty() && BookStatsTracking::isEnabled(bookCachePath) &&
      !CompletedBooksStore::update(bookCachePath, bookTitle, stats)) {
    LOG_ERR("BookStats", "Could not import saved completion history");
  }
  ignoreInitialBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialPowerRelease = mappedInput.isPressed(MappedInputManager::Button::Power);
  if (bookCachePath.empty()) page = Page::ThisDevice;
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void BookStatsActivity::resetReleaseGuards() {
  ignoreInitialBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialPowerRelease = mappedInput.isPressed(MappedInputManager::Button::Power);
}

void BookStatsActivity::showBookOptions() {
  // Cold-path menu: two localized labels at most, using the existing SDK menu.
  std::vector<std::string> options;
  options.reserve(2);
  const bool editDates = hasEditableBook();
  if (editDates) options.emplace_back(tr(STR_BOOK_EDIT_DATES));
  options.emplace_back(tr(STR_BOOK_DELETE_HISTORY));
  auto activity = makeUniqueNoThrow<OptionSelectionActivity>(
      renderer, mappedInput, "BookHistoryOptions", StrId::STR_BOOK_OPTIONS, std::move(options), 0, false, true);
  if (!activity) {
    LOG_ERR("BookStats", "Could not allocate history options");
    return;
  }
  activity->showAsActionMenu();
  startActivityForResult(std::move(activity), [this, editDates](const ActivityResult& result) {
    resetReleaseGuards();
    if (result.isCancelled) return;
    const auto* selected = std::get_if<OptionSelectionResult>(&result.data);
    if (!selected) return;
    if (editDates && selected->index == 0)
      beginDateEditing();
    else
      confirmHistoryDeletion();
  });
}

void BookStatsActivity::confirmHistoryDeletion() {
  // The existing confirmation owns its text until it closes (one book title,
  // bounded to 512 bytes by the history store, plus a localized explanation).
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int titleWidth = std::max(1, renderer.getScreenWidth() - metrics.optionPopupDialogSideMargin * 2 -
                                         metrics.optionPopupInnerPadding * 2 -
                                         renderer.getTextWidth(UI_12_FONT_ID, "\"\":", EpdFontFamily::BOLD));
  const auto visibleTitle = renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), titleWidth, EpdFontFamily::BOLD);
  // ConfirmationActivity inserts a literal space between heading and body.
  // Its popup wraps spaces, so do not rely on newlines as text separators.
  const std::string heading = "\"" + visibleTitle + "\":";
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading,
                                                              tr(STR_BOOK_DELETE_HISTORY_CONFIRM), true);
  if (!confirmation) {
    LOG_ERR("BookStats", "Could not allocate history confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    resetReleaseGuards();
    if (result.isCancelled) return;
    RenderLock lock(*this);
    // Never let onExit() or a saved date-edit snapshot restore deleted BIN data.
    didChangeStats = false;
    dateEditSnapshotValid = false;
    const bool removed = CompletedBooksStore::eraseHistory(bookCachePath);
    setResult(ReadingStatsResult{true});
    if (removed) {
      finish();
      return;
    }
    LOG_ERR("BookStats", "History deletion incomplete; journal retained for retry");
    historyDeletionFailed = true;
    statsAvailable = BookReadingStats::tryLoad(bookCachePath, stats);
    globalStats = GlobalReadingStats::load();
    refreshAllDevicesStats();
    requestUpdate();
  });
}

void BookStatsActivity::onExit() {
  saveStats();
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void BookStatsActivity::exitStatsActivity() {
  if (returnToHomeOnExit) {
    onGoHome();
    return;
  }

  finish();
}

bool BookStatsActivity::showNextStatsPage() {
  if (page == Page::PerBook) {
    page = Page::ThisDevice;
    requestUpdate();
    return true;
  }

  if (page == Page::ThisDevice && showAllDevicesStats) {
    page = Page::AllDevices;
    requestUpdate();
    return true;
  }

  return false;
}

bool BookStatsActivity::showPreviousStatsPage() {
  if (page == Page::AllDevices) {
    page = Page::ThisDevice;
    requestUpdate();
    return true;
  }

  if (page == Page::ThisDevice) {
    page = Page::PerBook;
    requestUpdate();
    return true;
  }

  return false;
}

bool BookStatsActivity::selectEditFieldFromTouchTarget(const int touchTarget) {
  if (touchTarget < BookStatsTouchTarget::DateFieldBase ||
      touchTarget >= BookStatsTouchTarget::DateFieldBase + BookStatsTouchTarget::DateFieldCount) {
    return false;
  }

  const int newEditField = touchTarget - BookStatsTouchTarget::DateFieldBase;
  if (selectedEditField != newEditField) {
    selectedEditField = newEditField;
    requestUpdate();
  }
  return true;
}

void BookStatsActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Back,
                                               ignoreInitialBackRelease) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Power,
                                               ignoreInitialPowerRelease) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm,
                                               ignoreInitialConfirmRelease)) {
    return;
  }

  if (historyActions && (!statsAvailable || historyDeletionFailed)) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
      mappedInput.suppressNextBackRelease();
      finish();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showBookOptions();
    }
    return;
  }

  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    if (page == Page::EditDates) {
      finishDateEditing(true);
    } else {
      exitStatsActivity();
    }
    return;
  }
  if (usesNoRtcSingleScreenLayout()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mappedInput.suppressNextBackRelease();
      exitStatsActivity();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (historyActions) {
        showBookOptions();
        return;
      }
      mappedInput.suppressNextConfirmRelease();
      exitStatsActivity();
      return;
    }
    return;
  }

  const bool upOrLeftPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                               mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool downOrRightPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                                  mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (page == Page::EditDates) {
    int touchedTarget = -1;
    if (mappedInput.wasItemTouchedDown(touchedTarget) && selectEditFieldFromTouchTarget(touchedTarget)) {
      return;
    }
    int tappedTarget = -1;
    if (mappedInput.wasItemTapped(tappedTarget)) {
      if (selectEditFieldFromTouchTarget(tappedTarget)) {
        return;
      }
      if (tappedTarget == BookStatsTouchTarget::DateAdjustUp) {
        adjustSelectedDateField(1);
        return;
      }
      if (tappedTarget == BookStatsTouchTarget::DateAdjustDown) {
        adjustSelectedDateField(-1);
        return;
      }
      if (tappedTarget == BookStatsTouchTarget::DateSave) {
        finishDateEditing(true);
        return;
      }
      if (tappedTarget == BookStatsTouchTarget::DateCancel) {
        finishDateEditing(false);
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finishDateEditing(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cycleEditField();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      adjustSelectedDateField(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      adjustSelectedDateField(1);
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    exitStatsActivity();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (historyActions && page == Page::PerBook) {
      showBookOptions();
      return;
    }
    mappedInput.suppressNextConfirmRelease();
    if (page == Page::PerBook && hasEditableBook()) {
      beginDateEditing();
      return;
    }
    exitStatsActivity();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left && showNextStatsPage()) {
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    if (!showPreviousStatsPage()) {
      exitStatsActivity();
    }
    return;
  }

  if (page == Page::PerBook) {
    int touchedTarget = -1;
    if (hasEditableBook() && mappedInput.wasItemTapped(touchedTarget) &&
        touchedTarget == BookStatsTouchTarget::StartedDaysStat) {
      beginDateEditing();
      return;
    }
    if (hasEditableBook() && upOrLeftPressed) {
      beginDateEditing();
      return;
    }
    if (downOrRightPressed) {
      showNextStatsPage();
      return;
    }
    return;
  }

  if (upOrLeftPressed) {
    showPreviousStatsPage();
    return;
  }

  if (page == Page::ThisDevice && showAllDevicesStats && downOrRightPressed) {
    showNextStatsPage();
  }
}

void BookStatsActivity::render(RenderLock&&) {
  if (historyActions && (!statsAvailable || historyDeletionFailed)) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    if (mappedInput.hasTouchHardware())
      TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS));
    else
      CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
    const int x = metrics.contentSidePadding;
    const int width = std::max(1, renderer.getScreenWidth() - x * 2);
    const int y = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
    const auto title = renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), width);
    renderer.drawText(UI_10_FONT_ID, x, y, title.c_str());
    const auto message = renderer.truncatedText(
        UI_10_FONT_ID, historyDeletionFailed ? tr(STR_BOOK_DELETE_HISTORY_FAILED) : tr(STR_HISTORY_STATS_UNAVAILABLE),
        width);
    renderer.drawText(UI_10_FONT_ID, x, y + renderer.getLineHeight(UI_10_FONT_ID) + 20, message.c_str());
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_BOOK_OPTIONS), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }
  if (usesNoRtcSingleScreenLayout()) {
    renderNoRtcCombinedStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                                 estimatedTimeLeftSeconds, globalStats,
                                 showAllDevicesStats ? &allDevicesStats : nullptr, true,
                                 historyActions ? tr(STR_BOOK_OPTIONS) : nullptr);
    renderer.displayBuffer();
    return;
  }

  switch (page) {
    case Page::PerBook:
      renderPerBookStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                             estimatedTimeLeftSeconds, true, hasEditableBook(), true,
                             historyActions ? tr(STR_BOOK_OPTIONS) : nullptr);
      break;
    case Page::ThisDevice:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_THIS_DEVICE_SCREEN), globalStats, true,
                            showAllDevicesStats);
      break;
    case Page::AllDevices:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_ALL_DEVICES_SCREEN), allDevicesStats, true, false);
      break;
    case Page::EditDates:
      renderEditBookDatesPage(renderer, &mappedInput, bookTitle, stats, selectedEditField, true);
      break;
  }
  if (page != Page::EditDates) {
    drawPageIndicators(renderer, static_cast<int>(page), showAllDevicesStats ? 3 : 2);
  }
  renderer.displayBuffer();
}
