# Release guide: T-Embed CC1101 Plus, lab profile

This firmware is for learning and for testing equipment you own or have
explicit permission to test. It has not been run on a physical device yet (see
`FEATURE_MATRIX.md`); treat the first flash as a bring-up, with the recovery
steps below ready.

## 0. The easy way: the web flasher

<https://danyokkk.github.io/Momentum-Firmware-T-Embed-C1101/>, in Chrome, Edge
or Opera on a computer:

1. Choose a build (the newest release is preselected; "Development build" is
   the newest commit on `main`).
2. Plug the T-Embed in, press **Connect** and pick "USB JTAG/serial debug
   unit". The page refuses anything that is not an ESP32-S3 with 16 MB flash.
3. Leave **Back up the device first** ticked and press **Install**.

What the page does, in order: downloads the four files and checks each one's
size, SHA-256 and MD5 against the build manifest; reads the whole 16 MB flash in
256 KB chunks, checks each chunk against the device's own MD5 and saves it as
`t-embed-backup-<date>.bin`; writes the bootloader, partition table, firmware
and OTA boot selection at the offsets from the build's partition table; has the
device check its MD5 of every written region; restarts it. Writing the OTA boot
selection matters: without it a device that last ran from the second app slot
would keep starting the old firmware.

Under **Advanced** the page can also restore a backup, make a backup without
installing, erase the flash, and list the files with a ready esptool command.

The rest of this guide covers doing the same by hand.

## 1. Build

Requirements, pinned in `tools/tembed_plus/toolchain.lock.json`:

| Item | Version |
| --- | --- |
| ESP-IDF | v5.4.1 (`4c2820d`) with the `esp32s3` tools |
| Compiler | xtensa-esp-elf esp-14.2.0_20241119 |
| CMake / Ninja | 3.30.2 / 1.12.1 |
| Python | 3.12 |
| Managed components | `dependencies.lock` (esp_tinyusb 1.7.6~2, tinyusb 0.21.0~4, led_strip 3.0.3, mdns 1.12.0) |

Install the SDK once (Windows, PowerShell; avoid paths with spaces for the SDK
and its tools):

```powershell
git clone -b v5.4.1 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git C:\Espressif\frameworks\esp-idf-v5.4.1
$env:IDF_PATH = 'C:\Espressif\frameworks\esp-idf-v5.4.1'; $env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
python $env:IDF_PATH\tools\idf_tools.py install --targets=esp32s3
python $env:IDF_PATH\tools\idf_tools.py install-python-env
```

Then, from the repository root:

```bash
python tools/tembed_plus/build.py doctor
```

```bash
python tools/tembed_plus/build.py build
```

`doctor` checks the SDK version and that Espressif's Wi-Fi library is
unmodified. Upstream's build used to patch that library inside the SDK; if you
ever built upstream with this SDK, `doctor` fails and tells you how to restore
it. `build` configures, compiles, and then runs
`tools/tembed_plus/check_lab_exclusions.py` on the result. A build that links
anything from the excluded list fails here. `build` also fails if
`dependencies.lock` changed during the build: ESP-IDF's component manager
rewrites that file when it re-resolves components (it does so if a board with
a different chip is built from the same folder with upstream's scripts), and an
image built that way is not the pinned one. Restore the file with
`git checkout -- dependencies.lock` and build again.

Outputs, in `build_t_embed_plus/`:

| File | Flash offset |
| --- | --- |
| `bootloader/bootloader.bin` | `0x0` |
| `partition_table/partition-table.bin` | `0x8000` |
| `ota_data_initial.bin` | `0xBF0000` |
| `furi_esp32.bin` | `0x10000` |

The reference build on 2026-11-14 produced a 3.06 MB `furi_esp32.bin`
(3,064,400 bytes), about half of a 5.94 MB app slot.

Linux and macOS: the same `build.py` commands, with `ESP_IDF_DIR` pointing at
your v5.4.1 checkout. This has not been run there yet.

## 2. Back up first

Before the first flash, save what is on the device and the card:

```bash
python -m esptool --chip esp32s3 -p COM7 read_flash 0x0 0x1000000 tembed_backup_16mb.bin
```

That takes several minutes and gives you a full image you can write back with
`write_flash 0x0`. Copy the SD card's contents to your computer as well.
(`esptool` is in the ESP-IDF Python environment; replace `COM7` with your
port.)

## 3. Flash

Connect the T-Embed by USB-C.

```bash
python tools/tembed_plus/build.py flash --port COM7
```

The script asks you to type `flash` before it writes. If the port does not
appear or the write fails to start, enter download mode: hold the encoder
button (it is the BOOT strap), press and release the reset button, then
release the encoder. Flash, then press reset.

## 4. SD card

Use a SanDisk card of 32 GB or less, FAT32; LilyGO reports other cards as
unreliable on this board.

```bash
python tools/tembed_plus/make_sd_bundle.py --list
```

```bash
python tools/tembed_plus/make_sd_bundle.py --out dist/sd_bundle
```

Copy the contents of `dist/sd_bundle` to the root of the card. The bundle
contains resources only (asset packs, IR, NFC and Sub-GHz data). Add
`--prebuilt-faps` to include an allow-list of upstream's prebuilt external
apps; those are binaries this fork did not build, and some may not load.
`MANIFEST.sha256` lists what was written.

## 5. First boot checklist

Work down this list and record the result of each line in
`FEATURE_MATRIX.md`, changing "build" to "hardware" where it passes.

1. Boots to the desktop without a reset loop; encoder and side key respond.
2. Settings → About shows the board and version.
3. SD card mounts; the archive lists files.
4. Sub-GHz → Read on 433.92 MHz decodes a remote or sensor you own.
5. Sub-GHz start menu has **no** "Jammer" and **no** "Bruteforce"; "Add
   Manually" lists only fixed-code remotes.
6. Sub-GHz → Radio Settings → TX Region shows "Not set". Sending any saved
   signal is refused with "No TX region set". Choose your region; a signal
   inside its bands now sends, one outside is refused.
7. A saved rolling-code file opens and shows its details; Send gives "Decode
   only".
8. NRF 2.4GHz shows only "Spectrum Analyzer", and it detects the module.
9. WiFi Lab → Scan lists networks; Status shows "WiFi is off" after leaving.
10. BLE Lab → Scan lists nearby devices (with WiFi off); leaving the app
    restores normal Bluetooth.
11. Infrared learns and replays one button.
12. NFC reads a test tag. The start menu has Read, Saved and Extra Actions
    only; a saved card offers no Emulate or Write.
13. Lock menu has no "Mesh Clients" and no "Web-Filesystem"; Settings has no
    "Update Firmware".

## 6. Updating

1. Build the new version.
2. Either flash over USB (section 3), or copy `furi_esp32.bin` to
   `/ext/update/furi_esp32.bin` on the card and start the update from
   qFlipper/qT-Embed over USB.
3. The updater writes the inactive slot and reboots into it.

**Rollback.** After an OTA update the new image has 20 seconds of uptime to
confirm itself. If it crashes, hangs into a reset, or you power it off before
then, the bootloader boots the previous image on the next start. So after an
update, leave the device on for half a minute before switching it off. Images
written over USB are not subject to this.

**What is and is not verified.** The updater checks the image's built-in
SHA-256 and chip type, which catches corruption. Images are not
cryptographically signed, so the updater cannot tell your build from someone
else's: only install files you built or trust. Secure Boot is available on the
ESP32-S3 but burns eFuses irreversibly, so it is left off.

## 7. Recovery

| Symptom | Do this |
| --- | --- |
| Boot loop after an OTA update | leave it; the second reset returns to the previous image |
| Boot loop after a USB flash | download mode (section 3), flash the last good build or your backup |
| Black screen, device enumerates on USB | reflash; if unchanged, write the 16 MB backup |
| Stuck UI | hold encoder + side key for 5 s (forced reboot) |
| USB port gone after enabling qFlipper/USB-Storage | expected: use download mode to flash |
| SD card not detected | try a SanDisk ≤ 32 GB card; reformat FAT32 |
| Want upstream's firmware back | write your backup, or flash upstream's release |

Download mode is in the ESP32-S3's ROM and cannot be overwritten by firmware,
so a bad build cannot brick the device.

## 8. Radio rules

- **Set your region before transmitting.** Sub-GHz → Radio Settings → TX
  Region. It starts as "Not set", which blocks all Sub-GHz transmit. The
  presets are the transmission bands Flipper Devices publishes per region
  group (`components/furi_hal/furi_hal_region_table.c`):

  | Preset | Bands (MHz) |
  | --- | --- |
  | EU/UK/RU+ | 433.05–434.79, 868.15–868.55 |
  | US/CA/AU+ | 304.10–321.95, 433.05–434.79, 915.00–928.00 |
  | UAE | 420.00–440.00 |
  | Taiwan | 304.50–321.95, 433.075–434.775, 915.00–927.95 |
  | Singapore | 300.00–300.30, 312.00–316.00, 433.50–434.79, 444.40–444.80 |
  | India/Israel | 433.05–434.79 |
  | Philippines | 430.00–440.00 |
  | China | 314.00–316.00, 430.00–432.00, 433.05–434.79 |
  | Other | 312.00–315.25, 920.50–923.50 |

  The preset limits frequency only. Power and duty-cycle limits are yours to
  respect, and the table is a convenience, not legal advice: check the rules
  where you are. The choice is stored in the device, not on the SD card, and
  applies to every transmit path. Receiving is not restricted here but may be
  regulated where you live.
- Rolling-code remotes (KeeLoq, Security+, Somfy, FAAC SLH and similar) can be
  received and decoded but not transmitted or generated.
- The board's band switch has three paths (315, 434, 868–915 MHz); stay near
  them for usable range.
- The switch that widens the TX range on the SD card is ignored by this build.
- Wi-Fi scanning is passive. Joining a network transmits, so only join networks
  you own or may use.
- The nRF24 tool only listens.
- Antenna: the K268 has internal antennas, the K268-01 external ones. Do not
  transmit with an external antenna disconnected.

## 9. Known limitations

See "Known gaps" and "Still open" in `FEATURE_MATRIX.md`, and the "todo" rows
in `MOMENTUM_PARITY.md`. In short: no hardware validation yet, no signed
updates, region presets limit frequency but not power, the NFC app is
read-only by design, and the Wi-Fi and BLE tools are deliberately small.

## 10. Tests

```bash
python tests/subghz_ws/run.py
```

```bash
python tests/subghz_region/run.py
```

```bash
python tests/ble_lab/run.py
```

These are the weather-decoder, region-table and BLE advertisement-parser host
tests (they need `gcc`, or on Windows `pip install ziglang==0.13.0` and
`ZIG_CC` set to its `zig.exe`). Upstream's own offline suite is
`python tests/run_t_embed_features.py`.

```bash
node tests/web/test_md5.mjs
```

The last one checks the web flasher's MD5 code against RFC 1321 and Node's
crypto (CI runs it; Node is not needed for anything else).

To compile every board profile the repository carries (compile check only):

```bash
python tools/tembed_plus/cross_build.py
```

## 11. Continuous integration, releases and the web flasher

Two workflows in `.github/workflows/`:

| Workflow | Runs on | Does |
| --- | --- | --- |
| `firmware.yml` | pushes to `main`, tags `v*`, pull requests, manual | host tests with gcc ASan/UBSan; T-Embed Plus build in the `espressif/idf:v5.4.1` container, with the SDK, dependency-lock and image checks of `build.py`; packaging; build-provenance attestation; on `main` it deploys the web flasher, on a tag it publishes a GitHub release |
| `boards.yml` | pushes to `main`, pull requests, manual | compile check of the ten other board profiles; thormini is allowed to fail (upstream link error) |

Upstream's `build.yml` and `native-tools-tests.yml` are kept but run only by
hand; `protopirate-tests.yml` is unchanged.

**One-time setup:** in the repository, Settings &rarr; Pages &rarr; Build and
deployment &rarr; Source: **GitHub Actions**. A workflow cannot switch Pages on
with its own token, so until this is done the deploy step fails with a message
saying Pages is not enabled. Then open that failed run and choose **Re-run
failed jobs** (only the deploy runs again, about a minute), or start a fresh
run with Actions &rarr; Firmware and web flasher &rarr; Run workflow.

**Making a release:**

```bash
git tag v0.1.0
```

```bash
git push origin v0.1.0
```

The tag build attaches the four firmware files, `manifest.json`, `SHA256SUMS`
and `sd_bundle.zip` to a new GitHub release (tags containing `-`, such as
`v0.2.0-rc.1`, become pre-releases), then starts a deploy from `main` so the web
flasher lists it. The site offers the three newest releases plus the current
development build.

**What the pipeline guarantees:** files on the site and in releases come only
from this repository's Actions runs; the build stops if the SDK is modified, if
the pinned managed components were re-resolved, or if a left-out feature is
linked; packaging stops if any offset disagrees with the partition table or an
image is not for the ESP32-S3; the site builder re-checks every file's SHA-256
and only accepts esptool-js if it matches the integrity pinned in
`web/vendor.json`. Anyone can confirm a file's origin with
`gh attestation verify <file> --repo Danyokkk/Momentum-Firmware-T-Embed-C1101`.

**Building the site locally** (after a firmware build):

```bash
python tools/web/package_build.py --out dist/package --label local-test --channel dev
```

```bash
python tools/web/build_site.py --out dist/site --package dist/package --repo Danyokkk/Momentum-Firmware-T-Embed-C1101
```

```bash
python -m http.server 8765 --directory dist/site
```

Then open <http://localhost:8765> (Web Serial needs `localhost` or HTTPS).
