# Board profile: LilyGO T-Embed CC1101 Plus

Checked on 2026-11-14 against LilyGO's own sources, not against a physical unit.
No T-Embed was connected during this work, so every row below is "matches
LilyGO's published definitions", not "measured".

Sources compared:

| Source | Revision |
| --- | --- |
| `components/furi_hal/boards/board_lilygo_t_embed_cc1101.h` (this repo) | Sor3nt `ef4d8f3b` |
| `examples/utilities.h`, `README.md`, `docs/pinmap_cn.md` in Xinyuan-LilyGO/T-Embed-CC1101 | `c36826e` (2026-10-09), hardware "V1.0 24-07-29" |
| LilyGO product listing for the Plus (SKUs K268 / K268-01) | read 2026-11-14 via search results; the product page itself returned HTTP 429 |

## What "Plus" means

The Plus is the T-Embed CC1101 with an nRF24L01 module factory-fitted on the
expansion header. LilyGO's base documentation still describes the nRF24 as an
*external expansion module*; the Plus ships with it installed. Two SKUs exist:
K268 (internal antenna) and K268-01 (external antenna). Which one you have
changes RF range only, not the pin map.

There is one board header for both models. The difference is carried by build
configuration:

| Setting | Plus (this build) | Base model |
| --- | --- | --- |
| `BOARD_HAS_NRF24` | 1 | 1 in the header, but the module is absent; the app reports "not found" |
| `CONFIG_T_EMBED_EXTERNAL_UART` | **n** (GPIO43/44 belong to the nRF24) | may be `y` to use GPIO43/44 as a UART |
| `sdkconfig.defaults.tembed_plus_lab` | applied | not applied |

No separate `board_*_plus.h` was created: the pins are identical and a second
header would only duplicate the first.

## Pin map

Every pin in the port's header matches LilyGO's `utilities.h`, with the one
exception noted under "Open questions".

| Function | GPIO | Notes |
| --- | --- | --- |
| Shared SPI SCK / MOSI / MISO | 11 / 9 / 10 | LCD, SD, CC1101 and nRF24 all sit on SPI2 |
| LCD CS / DC / backlight | 41 / 16 / 21 | ST7789, 320×170 after rotation, 40 MHz |
| SD card CS | 13 | LilyGO recommends SanDisk ≤ 32 GB |
| CC1101 CS / GDO0 / GDO2 | 12 / 3 / 38 | |
| CC1101 band switch SW1 / SW0 | 47 / 48 | 315 MHz: H/L, 434 MHz: H/H, 868–915 MHz: L/H |
| nRF24 CSN / CE | 44 / 43 | IRQ not wired |
| Power enable | 15 | must be high to power the CC1101 and the LEDs |
| Encoder A / B / push | 4 / 5 / 0 | push is also the BOOT strap |
| Side key | 6 | |
| I2C SDA / SCL | 8 / 18 | PN532 `0x24`, BQ27220 `0x55`, BQ25896 `0x6B`, and the Qwiic port |
| PN532 IRQ / reset | 17 / 45 | |
| IR TX / RX | 2 / 1 | |
| Speaker BCLK / LRCLK / DIN | 46 / 40 / 7 | MAX98357A |
| Microphone data / clock | 42 / 39 | PDM |
| WS2812 data | 14 | 8 LEDs |
| USB D− / D+ | 19 / 20 | wired straight to the ESP32-S3 |

## Shared resources and conflicts

- **One SPI bus, four devices.** Display, SD, CC1101 and nRF24 are chip-select
  multiplexed. Early boot drives every CS high and nRF24 CE low before any bus
  traffic (`furi_hal_init_early`). Upstream has an opt-in "Plus SD recovery"
  startup order (`CONFIG_T_EMBED_PLUS_SD_RECOVERY`), but its Kconfig entry depends
  on the BW16 companion link, which this build does not include, so it is off.
  If SD mounting proves unreliable on a real Plus, decoupling that option is the
  first thing to try.
- **GPIO43/44 are triple-booked.** They are nRF24 CE/CSN, the expansion header's
  UART0 TX/RX, and the pins the port uses for an RDM6300 125 kHz reader
  (`BOARD_PIN_RFID_*`) and for an external IR LED (`BOARD_PIN_IR_TX_EXT`). On a
  Plus the nRF24 owns them. Consequences: no external UART, no GPS, no RDM6300
  accessory while the module is fitted. The log console is on USB-Serial/JTAG
  for the same reason.
- **One I2C bus** for NFC, the fuel gauge, the charger and anything on Qwiic.
- **USB has two personalities.** Normally the port is the ESP32-S3
  USB-Serial/JTAG device (console and flashing). Enabling qFlipper or
  USB-Storage from the lock menu installs TinyUSB and takes the PHY away, so
  flashing after that needs download mode (hold the encoder button while
  resetting).
- **No battery ADC.** Battery state comes from the BQ27220 over I2C.

## Capability flags

| Flag | Value | Meaning on this board |
| --- | --- | --- |
| `BOARD_HAS_SUBGHZ` | 1 | built-in CC1101 |
| `BOARD_HAS_NRF24` | 1 | fitted on the Plus; probed at runtime, absent module is reported, not assumed |
| `BOARD_HAS_NFC` | 1 | PN532, 13.56 MHz only |
| `BOARD_HAS_IR` | 1 | TX and RX |
| `BOARD_HAS_RFID` | 1 | **not** a built-in reader: it enables the optional RDM6300 UART accessory, which conflicts with the Plus nRF24. The LF-RFID app is not built. |
| `BOARD_HAS_IBUTTON` | 0 | no 1-Wire hardware |
| `BOARD_HAS_TOUCH` / `VIBRO` | 0 / 0 | |
| `BOARD_HAS_ENCODER`, `SD_CARD`, `BLE`, `RGB_LED`, `SPEAKER`, `MIC` | 1 | |

The radio is 2.4 GHz Wi-Fi and BLE from the ESP32-S3. There is no 5 GHz radio
on the board. (LilyGO's repo carries ESP32-C5 AT firmware documentation; that is
for a separate add-on, not something on the Plus.)

## Open questions (need a physical unit)

1. **LCD reset pin.** The port sets `BOARD_PIN_LCD_RST` to GPIO40. LilyGO's
   software sets the display reset to "not connected" and uses GPIO40 as the
   speaker LRCLK; LilyGO's own pin map notes the schematic and software disagree
   here. The port pulses GPIO40 as a reset at display init and later hands it to
   I2S, which works in upstream's releases, so it is left unchanged.
2. **`BOARD_LCD_GAP_Y 35`** carries an upstream "verify on hardware" comment.
3. **Antenna variant** (K268 vs K268-01) cannot be detected in software.
4. **Sub-GHz output power and band-switch behaviour** at the band edges are
   unmeasured.
