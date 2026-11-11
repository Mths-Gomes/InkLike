# Completed books and reader titles

The existing `GlobalReadingStats::completedBooks` total is cumulative. It does
not reset at New Year. Manually undoing completion or explicitly resetting
global statistics can still change it. Reader titles use this same local-device
total; synced device totals are not used to award titles.

**Book Counter Options**, at the bottom of Settings > System > Reading Stats
after the Reserve Fuel options, offers Books Read (the existing counter), Reader Title
(the title and its matching icon), and Hidden. Existing hidden-counter preferences migrate to
Hidden. Reading Stats retains its existing counter preference independently.
Open **Home > Menu > Reader Titles**, alongside Reading Stats, for the title,
progress, milestone list, and paginated completion history. This does not add
any progress numbers to the Dashboard. The page order is overview, all ten
milestones on one compact page, then the completion history pages.

Each row shows the required book count in its own column, followed by the
icon and title together. Each milestone has a custom 24px monochrome icon:
closed book with bookmark, sprout, compass, turning page, heart, bitten apple, worm,
shield with keyhole, crown, and star with laurels. Only First Chapter and Page
Turner use a book; the other symbols fill the available icon area. The current
title stays bold. SVG sources live in `src/components/icons/svg/title-*.svg`;
regenerate `readerTitleIcons.h` through `scripts/generate_icons.py` with
`readerTitleIcons.manifest` and `--library crossink --sizes 24`. The ten packed
bitmaps total 720 bytes, stored as constant assets; no image buffer is allocated.

The Dashboard title slot uses the same level-to-icon selection as the milestones,
including on devices without a clock. Counter mode keeps its existing appearance.
Icon assets have one shared definition and are drawn in white on inverted sleep screens.

Reader Titles uses Back to exit and Previous/Next to change pages. The second
front button is unused and its hint is fully hidden, without an outline.
Outlined squares at the bottom show the pages, with
the current page filled black; long histories use a window of indicators
around the current page. Touch devices keep header Back and swipe navigation.

## SD-card format, version 1

Each book has a JSON file at
`/.crosspoint/completed_books/<cache-directory-name>.json`, separate from
disposable content caches. Example:

```json
{"version":1,"title":"A book","completed":true,"year":2026,"month":11,"day":11}
```

Dates come from the saved per-book finish date, including manual corrections.
The completion list displays each book on one compact line as `Title - YYYY/MM`;
long titles are shortened to keep the date visible. The JSON still preserves
the complete year, month, and day.
When no reliable date is known, all three date fields are `null`; no year is
guessed. Unmarking a book updates `completed` to `false`, so it is excluded from
the completion list and yearly counts. Remarking updates the same record; this
version records one completion per book identity, not a reread event log.

Writes use the existing atomic JSON helper with `.tmp`/`.bak` recovery. Firmware
book renames and moves migrate the record's filename to the new cache identity.
Deleting a content cache or book does not delete the completion history. Moving
files externally can create a new identity, as with existing per-book stats.

Historical records are imported progressively when a completed EPUB/XTC is
opened or its Reading Stats is viewed, provided tracking is enabled. No dates
or book names can be recovered from the old aggregate counter alone. The
yearly count therefore describes **recorded** completions and may be lower
than the lifetime counter. The JSON history is local to the SD card and is not
merged with synced statistics. Resetting global statistics does not erase it.

Only five display entries are kept in the title activity (about 1 KB); records
are read one at a time. Titles in the JSON are bounded to 512 UTF-8 bytes.
There are no background tasks, new framebuffer, or library-sized allocations.

## Hardware verification

On X3/X4, choose each Dashboard slot mode, complete an EPUB from the reader
and Library/File Browser, and inspect the title screen and SD record. On X3
with a valid clock, confirm the date/year. On X4 without a reliable date,
confirm that the date is unknown. Repeat with XTC. Undo completion, then mark
again: one active record should remain. On a clock-capable device edit the
finish date across a year boundary, save it, and verify the record; cancel an
edit and verify that the record stays unchanged. Test moving into `/Read`
and renaming through the firmware without duplicate records. Clear the book
cache and verify that the independent history remains. On Sticky/X4 Pro also
test header Back, navigation taps, and swipe pagination. No cache reset is
needed to test titles or history.
