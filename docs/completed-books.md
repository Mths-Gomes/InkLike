# Completed books and reader titles

The existing `GlobalReadingStats::completedBooks` total is cumulative. It does
not reset at New Year. Manually undoing completion or explicitly resetting
global statistics can still change it. Reader titles use this same local-device
total; synced device totals are not used to award titles.

**Book Counter Options**, at the bottom of Settings > System > Reading Stats
after the Reserve Fuel options, offers Books Read (the existing counter), Reader Title
(the title and its matching icon), and Hidden. Existing hidden-counter preferences migrate to
Hidden. Reading Stats retains its existing counter preference independently.
Open **Home > Menu > Reader Stats**, alongside Reading Stats, for the title,
progress, milestone list, and paginated completion history. The menu and screen
label in Brazilian Portuguese is **Status do leitor**. This does not add any
progress numbers to the Dashboard. The single overview starts with the current
title and icon in a selectable card with **View Titles**, followed by a
**Finished Books** entry, then next-title XP progress at the bottom. Space and a separator keep the
sections distinct. Open on the title card shows all ten milestones on one screen;
Open on Finished Books shows the paginated history. There are no lateral tabs.

Each row shows the required book count in its own column, followed by the
24px icon and 12pt title together. Each milestone has a custom monochrome icon:
closed book with bookmark, sprout, compass, turning page, heart, bitten apple, worm,
shield with keyhole, crown, and star with laurels. Only First Chapter and Page
Turner use a book; the other symbols fill the available icon area. The current
title stays bold. SVG sources live in `src/components/icons/svg/title-*.svg`;
regenerate `readerTitleIcons.h` through `scripts/generate_icons.py` with
`readerTitleIcons.manifest` and `--library crossink --sizes 24,32`. The packed
bitmaps total 2,000 bytes (720 for 24px and 1,280 for 32px), stored as constant
assets; no image buffer is allocated. Only the Reader Stats overview uses the
32px current-title icon. The Dashboard and milestone list retain their 24px icons.

The overview reuses the existing UI fonts at their usual sizes, with 10pt
next-title text and 12pt milestone names. No additional font assets are needed.
Overview headings and finished-book titles use regular weight. Card subtitles
are one existing font size smaller (12pt to 10pt, or 10pt to 8pt at small UI scale),
and the XP count uses 8pt text. The overview caption reads **Current Title:**.
The current milestone retains its existing bold highlight on the reference screen.

Overview card borders align with the XP section; explicit 4px inner padding and
zero theme row inset avoid excess indentation. The finished-book list uses 12px
outer margins beyond the renderer's runtime bezel-safe bounds and 4px inner padding.

The Dashboard title slot uses the same level-to-icon selection as the milestones,
including on devices without a clock. Counter mode keeps its existing appearance.
Icon assets have one shared definition and are drawn in white on inverted sleep screens.

On the overview and finished-book list, front buttons 1–4 are Back, Open, Up, Down,
using the device's logical menu mapping. Up/Down select between the overview's two
entries or move through books. Otherwise-unassigned side buttons mirror Up/Down
on these screens. Up on the first book wraps to the final book, and
Down on the final book wraps to the first. Selection crosses five-entry page
boundaries automatically. Back from either list returns directly to the overview,
including from the last book page. The static title reference only shows Back.
Touch devices can tap the title card, Finished Books, or a book, and swipe vertically
to move the selection. Only the finished-book list has page squares: at most three
are shown, the current square is black, and a small dot on either side signals
additional pages in that direction. Empty lists have no squares.

Open reuses `BookStatsActivity` for the selected book, including its existing
per-book and global layout, without opening the book file, recording a session,
or changing recents. Back restores the same list selection and page. Returning to
the overview and reopening the list also retains the selection. Closing Reader
Stats destroys this navigation state; reopening starts at the current-title card
on the overview, with the first book selected in the history.

## SD-card format, version 1

Each book has a JSON file at
`/.crosspoint/completed_books/<cache-directory-name>.json`, separate from
disposable content caches. Example:

```json
{"version":1,"title":"A book","completed":true,"year":2026,"month":11,"day":11}
```

Dates come from the saved per-book finish date, including manual corrections.
The completion list displays each book's title and localized month/year (for
example `Nov/2026` in English or `nov/2026` in Portuguese) on one compact line,
without a hyphen separator; long titles are shortened to keep the date visible. The JSON still preserves
the complete year, month, and day.
When no reliable date is known, all three date fields are `null`; no year is
guessed. Unmarking a book updates `completed` to `false`, so it is excluded from
the completion list and yearly counts. Remarking updates the same record; this
version records one completion per book identity, not a reread event log.

Writes use the existing atomic JSON helper with `.tmp`/`.bak` recovery. Firmware
book renames and moves migrate the record's filename to the new cache identity.
Deleting a content cache or book does not delete the completion history. Moving
files externally can create a new identity, as with existing per-book stats.

The JSON filename stem is the cache-directory reference: `epub_<hash>` or
`xtc_<hash>`. Existing records already contain this reference and need no migration.
The selected record resolves to `/.crosspoint/<stem>/`; `BookReadingStats::tryLoad`
reads the native versioned BIN or supported legacy/recovery file. BIN filenames,
schemas, and upstream-facing `load()` calls remain compatible. No sessions or
reading-time totals are duplicated into JSON. Missing or incompatible BIN data
shows an unavailable-statistics warning instead of presenting fabricated zeroes.

Device EPUB deletion now uses the existing stats-preserving cache cleanup, as
web deletion already did. XTC deletion also leaves its stats intact. Explicit
Delete Book Stats remains supported; the completion record can then remain with
an unavailable-statistics warning. Manual SD-card removal can also lose BIN data.
Cache cleanup preserves `.bin.bak` and `.bin.tmp` recovery files too, including
a lone backup after an interrupted stats save.

Historical records are imported progressively when a completed EPUB/XTC is
opened or its Reading Stats is viewed, provided tracking is enabled. No dates
or book names can be recovered from the old aggregate counter alone. The
yearly count therefore describes **recorded** completions and may be lower
than the lifetime counter. The JSON history is local to the SD card and is not
merged with synced statistics. Resetting global statistics does not erase it.

## Removing one finished book

Open **Reader Stats > Finished Books**, select a book, then choose **Open >
Options > Delete Book History**. In Brazilian Portuguese: **Status do leitor >
Livros concluídos > Abrir > Opções > Excluir histórico do livro**. Options also
contains Edit Dates when the book has statistics and the device has a clock.
Both action rows use a `>` indicator rather than a saved-setting Selected label.
History deletion is available without a clock, without the original book file,
and even when statistics are unavailable. Missing stats show a warning with
Options instead of a fabricated zero-statistics screen.

The book title is quoted and followed by a colon and space, then the deletion
question once; the popup does not depend on newline handling for separation.
The confirmation names the book and states that the action removes all its
reading statistics and its finished-book entry, and updates the books-read count
and current reader title. Cancel preserves everything. Confirm
removes the native per-book stats and their supported recovery/legacy sources,
the JSON entry and its recovery files, and one completion from the local total
(only if that record was still marked completed). The counter never goes below
zero. Global reading time, sessions, streaks, and synced-device snapshots retain
their cumulative values. The book file, reading position, and reader preferences
are preserved. The existing file-browser Delete Book Stats action retains its
original narrower behavior; it does not remove the catalog entry.

Success returns to the finished-book list, retaining the nearest valid selection
and adjusting pagination after removing the last item on a page. Overview,
milestones, and the Dashboard refresh from the updated local counter. Historical
stats screens do not run over a live reader, and suppress pending date snapshots
and exit saves after deletion so old in-memory data cannot recreate the BIN.

Deletion uses one bounded, version-1 journal at
`/.crosspoint/completed_books/.deleting.json` containing `version`, `cacheId`, and
`counted`. Recovery runs at boot before ordinary stats loads. The global counter
is staged using `/.crosspoint/global_stats.history-before.bin` and
`/.crosspoint/global_stats.history-after.bin`. Both use the unchanged native
159-byte global-statistics schema. Consuming the after-file by renaming it to
`global_stats.bin` is the durable commit receipt; subsequent retries cannot
subtract again, even if other books have been completed in between. The catalog
and journal are removed before that receipt. An I/O failure leaves recovery data
and a retryable Options screen. That book cannot be edited or reimported while
its deletion is pending; unrelated completions can still be recorded.

Only five display entries are kept in the title activity (about 1 KB); records
are read one at a time. Titles in the JSON are bounded to 512 UTF-8 bytes.
Each page entry trades 32 display-title bytes for a 32-byte cache reference, so
the page retains its 1 KB budget. Opening one book loads its full stored title
on demand, preventing truncated list labels from overwriting saved metadata.
The SDK list interaction table has fixed capacity: two overview entries or five
history rows, never the complete library. The views reuse the same activity and
page buffer, rather than allocating a new library list for each screen.
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
test header Back, overview/book taps, and vertical swipes. With more than
five completions, test first/last wraparound, page crossings, returning directly
to the overview from the last page, and returning from a book to the same selection.
With over three pages, check both continuation dots and the current-page square.
Native tests simulate 26 completions (six pages) to cover pagination without
needing a large on-device history. Exit/reopen Reader
Stats and check that navigation resets. Verify sessions, reading time, and recents
do not change while browsing. Delete a completed EPUB/XTC and reopen its statistics;
explicitly delete its stats and verify the unavailable warning. No cache reset is
needed to test titles or history; deleting files cannot restore stats lost earlier.

Verify the original overview font sizes, larger overview icon, XP-last order, aligned card borders, and book
list margins on X3/X4. Compare front Up/Down and the two side buttons in both
the overview and finished-book list. Switch English/Portuguese and verify
`Nov/2026` / `nov/2026`; no cache reset is needed for these presentation changes.

For history deletion, cancel once and check that the list, count, dates, and
statistics remain unchanged. Then confirm on a book at a title threshold and
verify the count decreases once and the current title/bold milestone update.
Repeat with an absent book file, missing statistics, an empty final page, and
the last record. On a device without a clock, Options must still offer deletion.
Native tests inject failures at journal creation, stats removal, counter
publication, catalog cleanup, and journal cleanup, then verify recovery and
preservation of other completions without changing the BIN format.
