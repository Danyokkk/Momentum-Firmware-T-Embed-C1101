/**
 * T-Embed CC1101 Plus web flasher.
 *
 * Safety rules this file keeps:
 * - Only an ESP32-S3 with 16 MB of flash is written to.
 * - Every firmware file is checked against the SHA-256 in its build manifest
 *   before anything is written, and the device's MD5 of each written region
 *   is compared with the file afterwards (esptool-js writeFlash).
 * - Backups are read in chunks and every chunk is checked against the
 *   device's own MD5 of that region; esptool-js does not verify reads.
 * - Offsets come from the build manifest, which the build pipeline derives
 *   from the firmware's own partition table.
 */
import { md5Hex } from "./md5.js";

const ESPTOOL_MODULE = "./vendor/esptool-js/bundle.js";
const EXPECTED_CHIP = "ESP32-S3";
const EXPECTED_FLASH = "16MB";
const FLASH_BYTES = 16 * 1024 * 1024;
const ESPRESSIF_VID = 0x303a;
// The T-Embed's only USB port is the ESP32-S3's own USB-Serial/JTAG, where the
// baud rate is irrelevant. Keeping the ROM and transfer rates equal stops
// esptool-js from reopening the port, which can reset that interface.
const BAUD = 115200;
const BACKUP_CHUNK = 256 * 1024;
const CHANNEL_LABEL = { release: "Release", prerelease: "Pre-release", dev: "Development" };

const $ = (id) => document.getElementById(id);

const state = {
  repo: "",
  builds: [],
  build: null,
  conn: null, // { port, transport, loader, description, flash }
  busy: false,
  lastBackup: null,
};

class FlasherError extends Error {
  constructor(message, hints = []) {
    super(message);
    this.name = "FlasherError";
    this.hints = hints;
  }
}

/* ------------------------------------------------------------- helpers -- */

function h(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") node.className = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? "" : String(value));
  }
  for (const child of children.flat()) {
    if (child === undefined || child === null || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

function formatBytes(bytes) {
  if (bytes >= 1024 * 1024) {
    const mb = bytes / (1024 * 1024);
    return `${mb >= 10 ? mb.toFixed(0) : mb.toFixed(1)} MB`;
  }
  return `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

function formatDate(iso) {
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return "";
  return date.toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
}

function hexOffset(value) {
  return `0x${value.toString(16)}`;
}

function timestamp() {
  const d = new Date();
  const p = (v) => String(v).padStart(2, "0");
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

function saveBytes(bytes, filename) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const link = h("a", { href: url, download: filename });
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 120000);
}

function repoUrl(path = "") {
  return state.repo ? `https://github.com/${state.repo}${path}` : "";
}

function buildBase(build) {
  return new URL(build.base, document.baseURI);
}

function partUrl(build, part) {
  return new URL(part.file, buildBase(build)).href;
}

async function copyText(text, button) {
  try {
    await navigator.clipboard.writeText(text);
    const label = button.textContent;
    button.textContent = "Copied";
    setTimeout(() => (button.textContent = label), 1500);
  } catch {
    button.textContent = "Copy failed";
  }
}

/* ----------------------------------------------------------------- log -- */

function log(text) {
  const pre = $("log");
  const line = String(text);
  pre.textContent += line.endsWith("\n") ? line : `${line}\n`;
  pre.scrollTop = pre.scrollHeight;
}

const terminal = {
  clean() {},
  writeLine: (data) => log(data),
  write: (data) => log(data),
};

/* ------------------------------------------------------------ controls -- */

function setBusy(busy) {
  state.busy = busy;
  document.body.classList.toggle("busy", busy);
  updateControls();
}

function updateControls() {
  const supported = "serial" in navigator && window.isSecureContext;
  const connected = Boolean(state.conn);
  const busy = state.busy;

  const connectBtn = $("connect-btn");
  connectBtn.disabled = busy || !supported;
  connectBtn.textContent = connected ? "Connect a different device" : "Connect";
  $("disconnect-btn").hidden = !connected;
  $("disconnect-btn").disabled = busy;

  $("install-btn").disabled = busy || !connected || !state.build;
  $("backup-btn").disabled = busy || !connected;
  $("erase-btn").disabled = busy || !connected;
  $("restore-btn").disabled = busy || !connected || $("restore-file").files.length === 0;
  $("opt-backup").disabled = busy;
  $("opt-erase").disabled = busy;
  for (const radio of document.querySelectorAll('input[name="build"]')) radio.disabled = busy;

  let hint = "";
  if (!supported) hint = "Needs Chrome, Edge or Opera on a computer.";
  else if (!state.build) hint = "Choose a build first.";
  else if (!connected) hint = "Connect the device first.";
  $("install-hint").textContent = hint;
}

function setDeviceInfo(text, pending = false) {
  const info = $("device-info");
  info.textContent = text;
  info.classList.toggle("pending", pending);
}

/* ---------------------------------------------------- progress / result -- */

const progress = {
  start(phases) {
    const list = $("phases");
    list.replaceChildren(...phases.map(([key, label]) => h("li", { "data-key": key, "data-state": "pending" }, label)));
    $("progress-bar").value = 0;
    $("progress-detail").textContent = "";
    $("progress").hidden = false;
    $("progress").scrollIntoView({ block: "nearest" });
  },
  phase(key, detail = "") {
    for (const item of $("phases").children) {
      if (item.dataset.key === key) item.dataset.state = "active";
    }
    $("progress-bar").value = 0;
    $("progress-detail").textContent = detail;
  },
  set(fraction, detail) {
    $("progress-bar").value = Math.max(0, Math.min(1, fraction));
    if (detail !== undefined) $("progress-detail").textContent = detail;
  },
  finish(key, detail) {
    for (const item of $("phases").children) {
      if (item.dataset.key === key) item.dataset.state = "done";
    }
    $("progress-bar").value = 1;
    if (detail !== undefined) $("progress-detail").textContent = detail;
  },
  fail(key) {
    for (const item of $("phases").children) {
      if (item.dataset.key === key && item.dataset.state !== "done") item.dataset.state = "failed";
    }
  },
  hide() {
    $("progress").hidden = true;
  },
};

function clearResult() {
  const box = $("result");
  box.hidden = true;
  box.replaceChildren();
}

function showResult(kind, title, ...content) {
  const box = $("result");
  box.className = `result ${kind}`;
  box.setAttribute("role", kind === "error" ? "alert" : "status");
  box.replaceChildren(h("h3", {}, title), ...content);
  box.hidden = false;
  box.scrollIntoView({ block: "nearest" });
}

function explain(error, step) {
  const message = (error && error.message) || String(error);
  const hints = [...((error && error.hints) || [])];
  if (/failed to connect|timed? ?out|no serial data|invalid head|wrong boot mode|couldn't sync/i.test(message)) {
    hints.push(
      "Put the device in download mode: hold the encoder (BOOT), click RST on the back, release the encoder, then press Connect again.",
    );
  }
  if (/failed to open|already open|access denied|in use|networkerror/i.test(message)) {
    hints.push("Another program or browser tab is using the port. Close it and try again.");
  }
  if (/lost|disconnect|device has been|break|framing|overrun/i.test(message)) {
    hints.push("The connection dropped. Check the cable, plug straight into the computer and try again.");
  }
  if (step === "write" || step === "restart") {
    hints.push(
      "The device may be only partly written now. That is recoverable: put it in download mode and install again, or restore your backup under Advanced.",
    );
  }
  return { message, hints: [...new Set(hints)] };
}

function showError(error, step, title = "That did not work") {
  const { message, hints } = explain(error, step);
  log(`ERROR: ${message}`);
  showResult(
    "error",
    title,
    h("p", {}, message),
    hints.length ? h("ul", {}, hints.map((hint) => h("li", {}, hint))) : null,
    h("p", { class: "muted" }, "The technical log below has the details; include it if you report this."),
  );
}

function confirmDialog(title, lines, okLabel = "Continue") {
  const dialog = $("confirm-dialog");
  $("confirm-title").textContent = title;
  $("confirm-body").replaceChildren(h("ul", {}, lines.map((line) => h("li", {}, line))));
  $("confirm-ok").textContent = okLabel;
  return new Promise((resolve) => {
    dialog.addEventListener("close", () => resolve(dialog.returnValue === "ok"), { once: true });
    dialog.returnValue = "cancel";
    dialog.showModal();
  });
}

/* -------------------------------------------------------------- builds -- */

async function loadBuilds() {
  try {
    const response = await fetch("builds.json", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    if (data.schema !== 1 || !Array.isArray(data.builds)) throw new Error("unexpected format");
    state.repo = data.repo || "";
    state.builds = data.builds;
    renderBuilds(data.default);
  } catch (error) {
    $("build-list").replaceChildren(
      h(
        "p",
        { class: "callout error" },
        `Could not load the list of builds (${error.message}). Reload the page; the files are also on the GitHub releases page.`,
      ),
    );
  }
}

function renderBuilds(defaultId) {
  const list = $("build-list");
  if (state.builds.length === 0) {
    list.replaceChildren(h("p", { class: "muted" }, "No builds have been published yet."));
    return;
  }
  list.replaceChildren(
    ...state.builds.map((build) => {
      const size = build.parts.reduce((sum, part) => sum + part.size, 0);
      const meta = [formatDate(build.date), build.commit ? build.commit.slice(0, 7) : "", formatBytes(size)]
        .filter(Boolean)
        .join(" · ");
      return h(
        "label",
        { class: "build-option" },
        h("input", {
          type: "radio",
          name: "build",
          value: build.id,
          onchange: () => selectBuild(build.id),
        }),
        h(
          "span",
          { class: "build-body" },
          h(
            "span",
            { class: "build-title" },
            build.title,
            h("span", { class: `badge ${build.channel}` }, CHANNEL_LABEL[build.channel] || build.channel),
          ),
          h("span", { class: "build-meta" }, meta),
        ),
      );
    }),
  );
  const initial = state.builds.some((b) => b.id === defaultId) ? defaultId : state.builds[0].id;
  selectBuild(initial);
}

function selectBuild(id) {
  state.build = state.builds.find((build) => build.id === id) || null;
  for (const radio of document.querySelectorAll('input[name="build"]')) radio.checked = radio.value === id;
  renderBuildDetails();
  renderDownloads();
  renderSdLink();
  updateControls();
}

function renderBuildDetails() {
  const box = $("build-details");
  const build = state.build;
  if (!build) {
    box.replaceChildren();
    return;
  }
  const links = [];
  if (build.notes_url) links.push(h("a", { href: build.notes_url }, build.channel === "dev" ? "Commit" : "Release notes"));
  if (build.commit && state.repo && build.channel !== "dev") {
    links.push(h("a", { href: repoUrl(`/commit/${build.commit}`) }, "Commit"));
  }
  box.replaceChildren(
    h(
      "p",
      {},
      "Firmware version ",
      h("code", {}, build.firmware_version || "unknown"),
      build.board_name ? ` for the ${build.board_name}.` : ".",
    ),
    build.channel === "dev"
      ? h("p", {}, "Built automatically from the newest commit on main. It changes whenever the code does.")
      : null,
    links.length ? h("p", {}, ...links.flatMap((link, i) => (i ? [" · ", link] : [link]))) : null,
  );
}

function renderDownloads() {
  const box = $("downloads");
  const build = state.build;
  if (!build) {
    box.replaceChildren(h("p", { class: "muted" }, "Choose a build first."));
    return;
  }
  const rows = build.parts.map((part) =>
    h(
      "li",
      {},
      h("a", { href: partUrl(build, part), download: part.file }, part.file),
      h("span", { class: "muted" }, `at ${hexOffset(part.offset)}, ${formatBytes(part.size)}`),
      h("span", { class: "hash", title: `SHA-256 ${part.sha256}` }, `sha256 ${part.sha256.slice(0, 16)}…`),
    ),
  );
  const extras = [];
  if (build.manifest_url) extras.push(h("a", { href: new URL(build.manifest_url, document.baseURI).href }, "manifest.json"));
  if (build.sums_url) extras.push(h("a", { href: new URL(build.sums_url, document.baseURI).href }, "SHA256SUMS"));
  box.replaceChildren(
    h("ul", { class: "downloads" }, rows),
    extras.length ? h("p", {}, ...extras.flatMap((link, i) => (i ? [" · ", link] : [link]))) : null,
  );
  const args = build.parts.map((part) => `${hexOffset(part.offset)} ${part.file}`).join(" ");
  $("esptool-cmd").textContent = `python -m esptool --chip esp32s3 write_flash ${args}`;
}

function renderSdLink() {
  const build = state.build;
  const link = $("sd-link");
  const size = $("sd-size");
  if (build && build.sd_bundle && build.sd_bundle.url) {
    link.href = new URL(build.sd_bundle.url, document.baseURI).href;
    size.textContent = build.sd_bundle.size ? formatBytes(build.sd_bundle.size) : "";
  } else {
    link.href = repoUrl("/releases") || "#";
    size.textContent = "";
  }
}

/* ---------------------------------------------------------- connection -- */

let esptoolModule = null;
async function loadEsptool() {
  if (!esptoolModule) esptoolModule = import(ESPTOOL_MODULE);
  return esptoolModule;
}

async function closeTransport(transport) {
  if (!transport) return;
  try {
    await transport.disconnect();
  } catch (error) {
    log(`(closing the port: ${error.message || error})`);
  }
}

async function disconnect(keepInfo = false) {
  const conn = state.conn;
  state.conn = null;
  if (conn) await closeTransport(conn.transport);
  if (!keepInfo) setDeviceInfo("");
  updateControls();
}

async function connect() {
  if (state.busy) return;
  clearResult();
  let port;
  try {
    // Must be the first await: the browser only shows its port chooser while
    // the click that started this is still "fresh".
    port = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
  } catch (error) {
    if (error && error.name === "NotFoundError") {
      $("connect-help").open = true;
      setDeviceInfo("No device chosen. If it was not in the list, see the help below.", true);
    } else {
      showError(error, "connect", "Could not open the device");
    }
    return;
  }

  setBusy(true);
  await disconnect(true);
  setDeviceInfo("Connecting…", true);
  let transport = null;
  try {
    const { ESPLoader, Transport } = await loadEsptool();
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport,
      baudrate: BAUD,
      romBaudrate: BAUD,
      terminal,
      debugLogging: false,
    });
    log("--- connecting");
    const description = await loader.main("default_reset");
    const chip = loader.chip && loader.chip.CHIP_NAME;
    if (chip !== EXPECTED_CHIP) {
      throw new FlasherError(
        `This device is an ${chip || "unknown chip"}. The T-Embed CC1101 uses an ESP32-S3, so this firmware cannot run on it. Nothing was written.`,
      );
    }
    const flash = await loader.detectFlashSize();
    if (flash !== EXPECTED_FLASH) {
      throw new FlasherError(
        `Its flash chip reports ${flash || "an unknown size"}, but the T-Embed CC1101 has 16 MB. Stopping so nothing is written to the wrong device.`,
      );
    }
    state.conn = { port, transport, loader, description, flash };
    setDeviceInfo(`${description} · ${flash} flash`);
    log("--- connected");
  } catch (error) {
    await closeTransport(transport);
    state.conn = null;
    setDeviceInfo("");
    $("connect-help").open = true;
    showError(error, "connect", "Could not connect");
  } finally {
    setBusy(false);
  }
}

/* ------------------------------------------------------ flash operations -- */

async function fetchParts(build, onProgress) {
  const total = build.parts.reduce((sum, part) => sum + part.size, 0);
  let done = 0;
  const parts = [];
  for (const part of build.parts) {
    const response = await fetch(partUrl(build, part), { cache: "no-cache" });
    if (!response.ok) {
      throw new FlasherError(`Could not download ${part.file} (HTTP ${response.status}).`, [
        "Reload the page and try again.",
      ]);
    }
    const data = new Uint8Array(await response.arrayBuffer());
    if (data.length !== part.size) {
      throw new FlasherError(`${part.file} has ${data.length} bytes; the build manifest says ${part.size}. Nothing was written.`, [
        "Reload the page: the site may have been updated while it was open.",
      ]);
    }
    const sha = await sha256Hex(data);
    if (sha !== part.sha256) {
      throw new FlasherError(`${part.file} does not match the SHA-256 in the build manifest, so nothing was written.`, [
        "Reload the page and try again. If it keeps happening, please report it.",
      ]);
    }
    if (md5Hex(data) !== part.md5) {
      throw new FlasherError(
        "This browser computed a different MD5 for a firmware file than the build manifest records, so the check after writing would not be trustworthy. Nothing was written.",
        ["Try another browser, and please report this."],
      );
    }
    log(`${part.file}: ${data.length} bytes, sha256 ${sha}`);
    parts.push({ ...part, data });
    done += part.size;
    onProgress(done / total, `${part.file} verified`);
  }
  return parts;
}

async function readWholeFlash(onProgress) {
  const { loader } = state.conn;
  const image = new Uint8Array(FLASH_BYTES);
  for (let address = 0; address < FLASH_BYTES; address += BACKUP_CHUNK) {
    const length = Math.min(BACKUP_CHUNK, FLASH_BYTES - address);
    let verified = false;
    for (let attempt = 1; attempt <= 3 && !verified; attempt++) {
      const chunk = await loader.readFlash(address, length, (_packet, received) => {
        onProgress((address + received) / FLASH_BYTES, `${formatBytes(address + received)} of 16 MB read`);
      });
      if (chunk.length !== length) {
        throw new FlasherError(`The device returned ${chunk.length} bytes at ${hexOffset(address)} instead of ${length}.`);
      }
      const deviceMd5 = await loader.flashMd5sum(address, length);
      if (String(deviceMd5) === md5Hex(chunk)) {
        image.set(chunk, address);
        verified = true;
      } else {
        log(`backup: chunk at ${hexOffset(address)} failed its MD5 check (attempt ${attempt}), reading it again`);
      }
    }
    if (!verified) {
      throw new FlasherError(`The backup could not be read reliably at ${hexOffset(address)}. No backup was saved and nothing was written.`, [
        "Try another cable or USB port.",
      ]);
    }
  }
  return image;
}

async function writeImages(images, eraseAll, onProgress) {
  const { loader } = state.conn;
  const sizes = images.map((image) => image.data.length);
  const total = sizes.reduce((a, b) => a + b, 0);
  await loader.writeFlash({
    fileArray: images.map((image) => ({ data: image.data, address: image.offset })),
    // Keep the headers exactly as built, so the MD5 check compares the
    // device's flash with the very file that was verified above.
    flashMode: "keep",
    flashFreq: "keep",
    flashSize: "keep",
    eraseAll,
    compress: true,
    reportProgress: (index, written, length) => {
      const before = sizes.slice(0, index).reduce((a, b) => a + b, 0);
      const fraction = length ? written / length : 1;
      const name = images[index].file || `part ${index + 1}`;
      onProgress((before + fraction * sizes[index]) / total, `Writing ${name} (${Math.round(fraction * 100)}%)`);
    },
    calculateMD5Hash: (image) => md5Hex(image),
  });
}

async function restartDevice() {
  const { loader } = state.conn;
  await loader.after("hard_reset");
}

async function runOperation(phases, work) {
  setBusy(true);
  clearResult();
  progress.start(phases);
  const step = { current: phases[0][0] };
  try {
    await work(step);
    return true;
  } catch (error) {
    progress.fail(step.current);
    showError(error, step.current);
    await disconnect(true);
    setDeviceInfo("Disconnected after the error. Connect again to retry.", true);
    return false;
  } finally {
    setBusy(false);
  }
}

async function install() {
  const build = state.build;
  if (!build || !state.conn || state.busy) return;
  const backup = $("opt-backup").checked;
  const erase = $("opt-erase").checked;

  const lines = [`Build: ${build.title} (firmware ${build.firmware_version || "unknown"}).`];
  lines.push(backup ? "First, the whole flash is saved to a file in your downloads." : "No backup will be made.");
  lines.push(erase ? "The flash is erased completely: settings, PIN and TX region are lost." : "Your settings are kept.");
  lines.push("This build has not been tested on a physical device yet.");
  if (!(await confirmDialog("Install this firmware?", lines, "Install"))) return;

  const phases = [["download", "Download and verify the firmware"]];
  if (backup) phases.push(["backup", "Back up the device"]);
  phases.push(["write", erase ? "Erase the flash and write the firmware" : "Write the firmware"]);
  phases.push(["restart", "Restart the device"]);

  let backupName = null;
  const ok = await runOperation(phases, async (step) => {
    step.current = "download";
    progress.phase("download");
    const images = await fetchParts(build, (f, d) => progress.set(f, d));
    progress.finish("download", "All files match the build manifest.");

    if (backup) {
      step.current = "backup";
      progress.phase("backup", "Reading the flash…");
      const image = await readWholeFlash((f, d) => progress.set(f, d));
      backupName = `t-embed-backup-${timestamp()}.bin`;
      saveBytes(image, backupName);
      state.lastBackup = backupName;
      log(`backup saved as ${backupName}`);
      progress.finish("backup", `Saved as ${backupName}`);
    }

    step.current = "write";
    progress.phase("write", erase ? "Erasing the whole flash; this can take a minute…" : "Writing…");
    await writeImages(images, erase, (f, d) => progress.set(f, d));
    progress.finish("write", "Written and verified.");

    step.current = "restart";
    progress.phase("restart");
    await restartDevice();
    progress.finish("restart");
    await disconnect(true);
    setDeviceInfo("Installed. The device is restarting.");
  });

  if (ok) {
    showResult(
      "ok",
      "Installed",
      h("p", {}, `${build.title} is on the device and it is restarting. You can unplug it now.`),
      backupName ? h("p", {}, "Your backup was saved as ", h("code", {}, backupName), ". Keep it until the new firmware has proved itself.") : null,
      h(
        "ol",
        {},
        h("li", {}, "Set up the SD card with the bundle (below)."),
        h("li", {}, "Choose your TX region in Sub-GHz → Radio Settings before transmitting."),
        h("li", {}, "Run the first-boot checks and tell us how it went."),
      ),
    );
  }
}

async function backupOnly() {
  if (!state.conn || state.busy) return;
  let name = null;
  const ok = await runOperation([["backup", "Back up the device"]], async (step) => {
    step.current = "backup";
    progress.phase("backup", "Reading the flash…");
    const image = await readWholeFlash((f, d) => progress.set(f, d));
    name = `t-embed-backup-${timestamp()}.bin`;
    saveBytes(image, name);
    state.lastBackup = name;
    progress.finish("backup", `Saved as ${name}`);
  });
  if (ok) {
    showResult(
      "ok",
      "Backup saved",
      h("p", {}, "Saved as ", h("code", {}, name), " in your downloads. Every part of it was checked against the device."),
      h("p", {}, "The device is still in download mode. Install a build, or press RST on the back to restart it."),
    );
  }
}

async function restoreBackup() {
  const file = $("restore-file").files[0];
  if (!file || !state.conn || state.busy) return;
  clearResult();
  const data = new Uint8Array(await file.arrayBuffer());
  if (data.length !== FLASH_BYTES) {
    showError(
      new FlasherError(`A full backup of this device is exactly 16 MB, but ${file.name} is ${formatBytes(data.length)}. Nothing was written.`),
      "restore",
      "Not a full backup",
    );
    return;
  }
  if (data[0] !== 0xe9) {
    showError(
      new FlasherError(`${file.name} does not start with an ESP32 bootloader image, so it is probably not a backup of this device. Nothing was written.`),
      "restore",
      "Not a full backup",
    );
    return;
  }
  const lines = [
    `File: ${file.name} (16 MB).`,
    "Everything on the device is replaced with the contents of this file.",
  ];
  if (!(await confirmDialog("Restore this backup?", lines, "Restore"))) return;

  const ok = await runOperation(
    [
      ["write", "Write the backup"],
      ["restart", "Restart the device"],
    ],
    async (step) => {
      step.current = "write";
      progress.phase("write", "Writing 16 MB; this takes a few minutes…");
      await writeImages([{ data, offset: 0, file: file.name }], false, (f, d) => progress.set(f, d));
      progress.finish("write", "Written and verified.");
      step.current = "restart";
      progress.phase("restart");
      await restartDevice();
      progress.finish("restart");
      await disconnect(true);
      setDeviceInfo("Restored. The device is restarting.");
    },
  );
  if (ok) showResult("ok", "Backup restored", h("p", {}, "The device is restarting with the restored contents."));
}

async function eraseFlash() {
  if (!state.conn || state.busy) return;
  const lines = [
    "Everything on the device is deleted: firmware, settings, PIN and TX region.",
    "It will not start again until you install or restore a firmware.",
  ];
  if (!(await confirmDialog("Erase the flash?", lines, "Erase"))) return;
  const ok = await runOperation([["erase", "Erase the flash"]], async (step) => {
    step.current = "erase";
    progress.phase("erase", "Erasing; this can take a minute…");
    await state.conn.loader.eraseFlash();
    progress.finish("erase", "Erased.");
  });
  if (ok) {
    showResult(
      "ok",
      "Flash erased",
      h("p", {}, "The device has no firmware now and is still connected. Press Install to put a build on it."),
    );
  }
}

/* ---------------------------------------------------------------- init -- */

function init() {
  if (!("serial" in navigator) || !window.isSecureContext) {
    $("unsupported").hidden = false;
  } else {
    navigator.serial.addEventListener("disconnect", (event) => {
      if (state.conn && event.target === state.conn.port && !state.busy) {
        state.conn = null;
        setDeviceInfo("The device was disconnected.", true);
        updateControls();
      }
    });
  }

  $("connect-btn").addEventListener("click", connect);
  $("disconnect-btn").addEventListener("click", () => disconnect());
  $("install-btn").addEventListener("click", install);
  $("backup-btn").addEventListener("click", backupOnly);
  $("erase-btn").addEventListener("click", eraseFlash);
  $("restore-btn").addEventListener("click", restoreBackup);
  $("restore-file").addEventListener("change", updateControls);
  $("log-copy").addEventListener("click", (event) => copyText($("log").textContent, event.currentTarget));
  $("cmd-copy").addEventListener("click", (event) => copyText($("esptool-cmd").textContent, event.currentTarget));

  window.addEventListener("beforeunload", (event) => {
    if (state.busy) {
      event.preventDefault();
      event.returnValue = "";
    }
  });

  updateControls();
  loadBuilds();
}

init();
