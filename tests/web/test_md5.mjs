// Tests web/md5.js against RFC 1321 vectors and against Node's crypto.
//
//   node tests/web/test_md5.mjs
//
// md5.js is imported from a data: URL so this works whatever Node decides
// about module types for plain .js files.
import { createHash, randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";

const source = readFileSync(new URL("../../web/md5.js", import.meta.url), "utf8");
const { md5Hex } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);

let checks = 0;
let failures = 0;
function check(condition, message) {
  checks++;
  if (!condition) {
    failures++;
    console.log(`  FAIL ${message}`);
  }
}

const encoder = new TextEncoder();
const rfc1321 = [
  ["", "d41d8cd98f00b204e9800998ecf8427e"],
  ["a", "0cc175b9c0f1b6a831c399e269772661"],
  ["abc", "900150983cd24fb0d6963f7d28e17f72"],
  ["message digest", "f96b697d7cb7938d525a2f31aaf161d0"],
  ["abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"],
  ["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"],
  ["12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"],
];
for (const [text, expected] of rfc1321) {
  check(md5Hex(encoder.encode(text)) === expected, `RFC 1321 vector ${JSON.stringify(text)}`);
}

const nodeMd5 = (bytes) => createHash("md5").update(bytes).digest("hex");

// Every length around the padding boundaries (55/56/63/64 bytes) and beyond.
for (let length = 0; length <= 300; length++) {
  const bytes = new Uint8Array(randomBytes(length));
  check(md5Hex(bytes) === nodeMd5(bytes), `random data of ${length} bytes`);
}

// A view that does not start at offset 0 of its buffer.
const backing = new Uint8Array(randomBytes(5000));
const view = backing.subarray(7, 4007);
check(md5Hex(view) === nodeMd5(view), "subarray with a non-zero byteOffset");

// Firmware-sized and flash-sized inputs.
for (const size of [3 * 1024 * 1024 + 123, 16 * 1024 * 1024]) {
  const bytes = new Uint8Array(randomBytes(size));
  check(md5Hex(bytes) === nodeMd5(bytes), `random data of ${size} bytes`);
}
const erased = new Uint8Array(0x2000).fill(0xff);
check(md5Hex(erased) === nodeMd5(erased), "8 KB of 0xFF (blank OTA data)");

let threw = false;
try {
  md5Hex("not bytes");
} catch {
  threw = true;
}
check(threw, "rejects a string");

console.log(`${checks} checks, ${failures} failure(s)`);
if (failures) process.exit(1);
console.log("PASS: web/md5.js");
