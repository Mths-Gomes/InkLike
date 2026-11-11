# InkLike

Personal e-reader firmware focused on comfortable reading, useful reading stats, and a few helpful and fun features for my own devices.

InkLike keeps the reading experience simple: open a book, make the page comfortable, and enjoy reading. New features should support that experience and stay practical for the hardware.

## Highlights

- **Reading stats:** reading time, sessions, pages turned, streaks, completed books, and time-of-day and day-of-week charts.
- **Reserve Fuel:** earn reserves through consecutive reading days and spend them automatically to protect a streak after a missed day. Choose a limit of 1, 3, or 5 reserves and an earning interval of 3, 7, 15, or 30 days, or turn it off.
- **A clearer Dashboard:** separate rows for streak and reader type, followed by Reserve Fuel and Books Read, with custom fuel-can and open-book icons.
- **Fuel details in Reading Stats:** see your available reserves, capacity, and progress toward earning the next one. The all-books charts use the same spacing as the per-book screen.
- **An independent Books Read Counter:** show or hide the counter on both the Dashboard and Reading Stats, without changing Reserve Fuel or deleting saved totals.
- **Language support:** the new Reserve Fuel and counter labels are translated into all 28 supported interface languages.
- **Comfortable typography:** built-in Lexend Deca and Bitter fonts, SD-card fonts, and configurable size, spacing, and margins.
- **Reading conveniences:** bookmarks, clippings, a dictionary, Focus Reading, Guide Dots, and customizable button actions.
- **A personal home screen:** Minimal and Dashboard themes, book-cover sleep screens, and custom sleep images.
- **Local transfers and sync:** upload books over Wi-Fi and sync supported reading progress and stats between devices.

Reserve Fuel is device-local. A protected missed day preserves the streak but does not count as a reading day. Missing a day resets fuel-earning progress, and a full reserve does not bank extra progress.

## Device Profiles

| Device | PlatformIO environment |
| --- | --- |
| Xteink X3 / X4 | `default` |
| Xteink X4 Classic | `x4-classic` |
| Xteink X4 Pro | `x4-pro` |
| Seeed reTerminal Sticky | `sticky` |

The current InkLike changes have been tested on an X3. The other device profiles are inherited and need testing on their respective hardware.

## Install or Update

Download the binary for your device from [InkLike Releases](https://github.com/Mths-Gomes/InkLike/releases). If a release is not available yet, build it locally using the instructions below.

For a device already running compatible firmware:

1. Back up the SD card, including its hidden settings and stats folders.
2. Copy the matching `InkLike-*.bin` or `firmware-*.bin` file to the card.
3. Open **Settings > System > SD Card Firmware Update** and select the file.
4. After restarting, check that System settings show **InkLike** and the expected version.

InkLike starts its own version sequence at **1.0.0**. For the first transition from the inherited 1.6.1 firmware, use the SD-card updater: its online updater considers 1.0.0 an older version. Subsequent InkLike update checks use this repository's releases.

Existing SD-card paths, including `.crosspoint` and the settings filename, are retained so the branding change does not reset preferences, progress, or reading stats.

## Build Locally

Install Git and PlatformIO, then clone with the submodules:

```sh
git clone --recurse-submodules https://github.com/Mths-Gomes/InkLike.git
cd InkLike
git switch dev
pio run -e default
```

For an existing clone, initialize dependencies with:

```sh
git submodule update --init --recursive
```

Replace `default` with the environment for your device. Build outputs are under `.pio/build/<environment>/`. InkLike-named binaries and the canonical `firmware-*.bin` copies contain the same image; the canonical names are also used by the update tooling.

To build and flash a connected X3/X4:

```sh
pio run -e default --target upload
```

Development follows **feature/fix branch -> dev -> main**. Use `dev` for ongoing work and integration testing; `main` is the release line. This is a personal project, so feature choices follow what I use and can test on my readers.

## Guides

- [Reader Features](docs/reader-features.md)
- [Controls](docs/controls.md)
- [SD Card Fonts](docs/sd-card-fonts.md)
- [Wi-Fi File Transfers](docs/webserver.md)
- [Reading Stats Sync](docs/reading-stats-sync.md)
- [Reading Progress Sync](docs/nearby-position-sync.md)
- [Development Setup](docs/development/getting-started.md)
- [Testing and Debugging](docs/development/testing-debugging.md)
- [Changelog](CHANGELOG.md)

For smoother reading on devices without PSRAM, prefer text-first EPUBs, organize large libraries into folders, and leave free space on a reliable SD card.

## Credits and License

InkLike is forked from [CrossInk](https://github.com/uxjulia/CrossInk), based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Its starting point is CrossInk 1.6.1, and much of the reading engine, hardware support, typography, and existing features comes from those projects and their contributors.

Device and UI abstractions are provided by [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk).

The project retains the upstream history and copyright notices. See [LICENSE](LICENSE) for the MIT license.
