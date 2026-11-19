# Update-check diagnostics

For devices without accessible USB serial, each manual **Settings > System >
Check for Updates** attempt replaces `/ota-check.log` at the root of the SD card.
The file is written on successful and failed checks. It records the firmware
version and source identity, release endpoint, target firmware family, free heap
and largest free block before/after the request, device Unix time, HTTP status, ESP/socket/TLS
errors, certificate verification flags, received bytes, and release parsing
results. It does not record Wi-Fi names, passwords, or authentication tokens.

The failed-check screen shows the request stage and HTTP/ESP codes, plus the
log path. If any log write, sync, or close fails, the log is shown as unavailable;
a partial file may still exist. An HTTP status of zero means no HTTP response
status was obtained. Stage `complete` means release metadata was fetched and a
matching firmware found; it does not mean the firmware was installed.

The log contains only the most recent check, not a continuous device log. It
is under 1 KiB with the built-in endpoint and normal version strings. Each
formatted write has a 224-byte stack buffer; no network response is buffered
for logging. All SD handles are closed before the network request. A diagnostic
text buffer adds 80 bytes to the update controller, not to every screen.

## Hardware verification (X3)

1. Install the diagnostic X3/X4 `.bin` through the existing SD-card updater.
2. Run **Check for Updates** and wait for a result. If it fails, note the codes
   and the SD-log status. If it succeeds, “No update available” is valid when
   the diagnostic build has the same base version as the public release.
3. Put the device to sleep before removing its SD card. Read `ota-check.log`
   on a computer; preserve that copy before trying another check.
4. Repeat a check and confirm the file is replaced, rather than appended to
   indefinitely. No book-cache reset is required.

This instrumentation diagnoses the metadata check only. Firmware download,
hash validation, and flashing keep their existing behavior and checks.
