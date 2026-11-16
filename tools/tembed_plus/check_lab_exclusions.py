#!/usr/bin/env python3
"""Fail if anything "excluded for safety" made it into a lab-profile build.

Checks, against a finished build directory:
  1. the generated app registry has none of fam_config.LAB_EXCLUDED_APPS;
  2. no excluded source file was handed to the compiler;
  3. the linked ELF defines no symbol matching the deny-list below;
  4. the resolved sdkconfig has the settings the profile relies on.

Run by tools/tembed_plus/build.py after every build, and usable on its own:
    python tools/tembed_plus/check_lab_exclusions.py --build-dir build_t_embed_plus
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import posixpath
import re
import runpy
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# Symbol-name patterns that must not be defined in the image. Each entry is
# (regex, what it would mean if present).
DENY_SYMBOLS = [
    (r"^wlan_hal_(send_raw|raw_tx_\w+)$", "raw 802.11 frame injection"),
    (r"^wlan_hal_beacon_spam_\w+$|^beacon_spam_task$", "Wi-Fi beacon spam"),
    (r"^wlan_hal_evil_portal_\w+$|^wlan_evil_portal\w*$", "evil portal / credential capture"),
    (r"^wifite_\w+$|^wf_pixie\w*$", "WPS PIN attack engine"),
    (r"^wlan_app(_\w+)?$", "upstream WiFi attack app"),
    (r"^wlan_(netcut|mitm|cred_sniff|html_inject|printhijack|smart_deauth|airsnitch)\w*$",
     "Wi-Fi MITM / deauth helpers"),
    (r"^wlan_airprint_start_(spam|clone|capture)$|^wlan_airprint_get_spam_count$",
     "AirPrint printer spam / printer impersonation"),
    (r"^nrf24_hw_(jammer|flood|tx|rx_start_promiscuous)\w*$", "nRF24 jamming / injection"),
    (r"^nrf24_(jam|mj)_\w+$|^nrf24_app_scene_(jam|mj)\w*$", "nRF24 jammer / MouseJack"),
    (r"^subbrute_\w+$|^subghz_scene_bf_\w+$", "Sub-GHz code brute-forcer"),
    (r"^subghz_jammer_\w+$|^subghz_scene_jammer_\w+$|^jammer_radio_device_loader_\w+$",
     "Sub-GHz jammer"),
    (r"^ble_spam\w*$", "BLE advertisement spam"),
    (r"^bad_usb\w*$|^ducky_\w+$", "scripted keystroke injection"),
    (r"^js_blebeacon\w*$", "scripted BLE advertising"),
    # NFC is read-only: the scenes that drive these are replaced, not built.
    (r"^nfc_scene_(mf_classic_dict_attack|mf_ultralight_c_dict_attack|mf_classic_detect_reader|"
     r"mf_classic_mfkey_\w+)_on_(enter|event|exit)$", "NFC key recovery (dictionary attack / mfkey)"),
    (r"^nfc_scene_(slix_unlock\w*|slix_key_input|mf_ultralight_unlock_\w+|mf_ultralight_key_input|"
     r"mf_ultralight_capture_pass|des_auth_\w+)_on_(enter|event|exit)$", "NFC password/key unlock"),
    (r"^nfc_scene_(emulate|write|rpc|clone_place_blank|chameleon_wipe|mf_classic_update_initial\w*|"
     r"set_type|set_uid|set_sak|set_atqa|generate_info)_on_(enter|event|exit)$",
     "NFC emulate / write / clone scenes"),
    # SDK entry points that only the excluded features reach. Their absence from
    # the image is the strongest evidence: the capability is not linked at all.
    (r"^esp_wifi_80211_tx$", "raw 802.11 transmit (SDK)"),
    (r"^esp_now_init$|^esp_now_send$", "ESP-NOW (unauthenticated mesh link)"),
]

# Files inside an excluded app's directory that other, kept apps compile.
# Each one was read; the comment says what it is.
ALLOWED_SHARED_SOURCES = {
    "applications/main/wlan_app/views/wlan_lan_view.c",   # list view used by Streaming
    "applications/main/wlan_app/views/wlan_view_common.c",  # its drawing helpers
    "applications/main/wlan_app/wlan_webfs.c",  # SD file server engine, used by WiFi Lab
    "applications/main/wlan_app/wlan_smb.c",    # SMB client (libsmb2 wrapper), used by WiFi Lab
    "applications/main/wlan_app/wlan_netscan.c",  # ARP lookup of the joined /24 + NetBIOS names
    "applications/main/wlan_app/wlan_androidtv.c",  # Android TV Remote v2 client (PIN pairing)
    "applications/main/wlan_app/views/wlan_androidtv_remote_view.c",  # its button grid
    "applications/main/wlan_app/wlan_airprint.c",  # IPP receiver; spam/clone/capture entry points
                                                   # are denied below (DENY_SYMBOLS)
    "applications/main/wlan_app/wlan_pcap.c",      # pcap file writer
    "applications/main/wlan_app/wlan_pcap_rec.c",  # buffered writer behind the channel monitor
    "applications/main/wlan_app/views/wlan_sniffer_view.c",  # counter screen of the monitor
}

SDKCONFIG_EXPECT = {
    "CONFIG_ESP_TLS_INSECURE": False,
    "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY": False,
    "CONFIG_T_EMBED_EXTERNAL_UART": False,
    "CONFIG_T_EMBED_BW16_I2C": False,
    "CONFIG_PARTITION_TABLE_CUSTOM": True,
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": True,
}


def find_nm(riscv: bool) -> str | None:
    names = (
        ("riscv32-esp-elf-nm",)
        if riscv
        else ("xtensa-esp32s3-elf-nm", "xtensa-esp-elf-nm")
    )
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    roots = [Path("C:/Espressif/tools/tools"), Path.home() / ".espressif" / "tools"]
    if os.environ.get("IDF_TOOLS_PATH"):
        roots.insert(0, Path(os.environ["IDF_TOOLS_PATH"]) / "tools")
    pattern = (
        "riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-nm*"
        if riscv
        else "xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm*"
    )
    for root in roots:
        matches = sorted(root.glob(pattern))
        if matches:
            return str(matches[-1])
    return None


def defined_symbols(elf: Path, riscv: bool) -> set[str]:
    nm = find_nm(riscv)
    if not nm:
        sys.exit("check_lab_exclusions: toolchain nm not found (add the toolchain to PATH)")
    output = subprocess.run(
        [nm, "--defined-only", str(elf)], capture_output=True, text=True, check=True
    ).stdout
    symbols = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            # Strip GCC clone suffixes such as ".constprop.0" / ".isra.3".
            symbols.add(parts[-1].split(".")[0])
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument(
        "--no-sdkconfig",
        action="store_true",
        help="skip the sdkconfig expectations (they describe the T-Embed Plus defaults)",
    )
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    failures: list[str] = []

    os.environ.setdefault("FLIPPER_BOARD", "lilygo_t_embed_cc1101")
    os.environ.setdefault("FLIPPER_PROFILE", "lab")
    config = runpy.run_path(str(REPO_ROOT / "fam_config.py"))
    excluded_apps: dict[str, str] = config["LAB_EXCLUDED_APPS"]
    excluded_sources: dict[str, list[str]] = config["LAB_EXCLUDED_SOURCES"]

    # 1. registry
    registry = build / "esp-idf" / "main" / "generated" / "applications.c"
    if not registry.is_file():
        failures.append(f"missing {registry} (build first)")
    else:
        text = registry.read_text(encoding="utf-8", errors="replace")
        for appid in excluded_apps:
            if re.search(rf'\.appid\s*=\s*"{re.escape(appid)}"', text) or re.search(
                rf'\.path\s*=\s*"{re.escape(appid)}"', text
            ):
                failures.append(f"registry still lists excluded app '{appid}'")

    # 2. compiled sources
    ported = build / "esp-idf" / "main" / "generated" / "ported_apps.cmake"
    if ported.is_file():
        listed = re.findall(r'"([^"\n]+\.(?:c|cpp|cc))"', ported.read_text(encoding="utf-8"))
        # normpath collapses "streaming/../wlan_app/x.c", so a file reached
        # through another app's manifest is still attributed to its real folder.
        normalised = [posixpath.normpath(path.replace("\\", "/")) for path in listed]
        for appid, patterns in excluded_sources.items():
            for pattern in patterns:
                needle = f"*/applications/main/{appid}/{pattern}"
                for path in normalised:
                    if fnmatch.fnmatch(path, needle):
                        failures.append(f"excluded source is compiled: {path}")
        for appid in excluded_apps:
            for path in normalised:
                if any(path.endswith(allowed) for allowed in ALLOWED_SHARED_SOURCES):
                    continue
                if f"/applications/main/{appid}/" in path or (
                    appid == "wlan" and "/applications/main/wlan_app/" in path
                ):
                    failures.append(f"source of excluded app '{appid}' is compiled: {path}")

    # 3. linked symbols
    elf = build / "furi_esp32.elf"
    checked_symbols = 0
    if not elf.is_file():
        failures.append(f"missing {elf} (build first)")
    else:
        config_text = (
            (build / "sdkconfig").read_text(encoding="utf-8", errors="replace")
            if (build / "sdkconfig").is_file()
            else ""
        )
        riscv = re.search(r'^CONFIG_IDF_TARGET_ARCH_RISCV=y$', config_text, re.MULTILINE) is not None
        symbols = defined_symbols(elf, riscv)
        checked_symbols = len(symbols)
        for pattern, meaning in DENY_SYMBOLS:
            regex = re.compile(pattern)
            hits = sorted(symbol for symbol in symbols if regex.match(symbol))
            if hits:
                shown = ", ".join(hits[:6]) + (" ..." if len(hits) > 6 else "")
                failures.append(f"{meaning}: {len(hits)} symbol(s) linked ({shown})")

    # 4. sdkconfig
    sdkconfig = build / "sdkconfig"
    if args.no_sdkconfig:
        pass
    elif not sdkconfig.is_file():
        failures.append(f"missing {sdkconfig}")
    else:
        text = sdkconfig.read_text(encoding="utf-8", errors="replace")
        for key, expected in SDKCONFIG_EXPECT.items():
            enabled = re.search(rf"^{key}=y$", text, re.MULTILINE) is not None
            if enabled != expected:
                failures.append(f"{key} should be {'y' if expected else 'off'}")

    if failures:
        print("check_lab_exclusions: FAIL")
        for failure in failures:
            print("  -", failure)
        return 1
    print(
        f"check_lab_exclusions: PASS ({len(excluded_apps)} apps, "
        f"{sum(len(v) for v in excluded_sources.values())} source globs, "
        f"{len(DENY_SYMBOLS)} symbol rules over {checked_symbols} linked symbols)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
