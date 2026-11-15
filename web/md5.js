/**
 * MD5 (RFC 1321) over a Uint8Array, returned as lowercase hex.
 *
 * esptool-js compares the MD5 the device computes over what it wrote with the
 * MD5 of the file, and browsers' Web Crypto has no MD5, so it is implemented
 * here. It is used only for that write check, never for security: the files
 * themselves are verified with SHA-256 against the published build manifest.
 *
 * Tested by tests/web/test_md5.mjs (RFC 1321 vectors and random data against
 * Node's crypto) and checked at runtime against the MD5 recorded in each build
 * manifest before anything is written.
 */

// Per-round shift amounts.
const S = new Uint8Array([
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
]);

// K[i] = floor(abs(sin(i + 1)) * 2^32), generated, not typed.
const K = new Int32Array([
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
  0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
  0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
  0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
  0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
  0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
  0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
  0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
]);

/**
 * @param {Uint8Array} bytes
 * @returns {string} 32 lowercase hex digits
 */
export function md5Hex(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new TypeError("md5Hex expects a Uint8Array");

  let a0 = 0x67452301 | 0;
  let b0 = 0xefcdab89 | 0;
  let c0 = 0x98badcfe | 0;
  let d0 = 0x10325476 | 0;
  const M = new Int32Array(16);

  const block = (view, offset) => {
    for (let i = 0; i < 16; i++) M[i] = view.getInt32(offset + i * 4, true);
    let a = a0;
    let b = b0;
    let c = c0;
    let d = d0;
    for (let i = 0; i < 64; i++) {
      let f;
      let g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) & 15;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) & 15;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) & 15;
      }
      const next = d;
      d = c;
      c = b;
      const x = (a + f + K[i] + M[g]) | 0;
      b = (b + ((x << S[i]) | (x >>> (32 - S[i])))) | 0;
      a = next;
    }
    a0 = (a0 + a) | 0;
    b0 = (b0 + b) | 0;
    c0 = (c0 + c) | 0;
    d0 = (d0 + d) | 0;
  };

  // Whole 64-byte blocks straight from the input (no copy of large images).
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const whole = bytes.length - (bytes.length % 64);
  for (let offset = 0; offset < whole; offset += 64) block(view, offset);

  // Tail: remaining bytes, 0x80, zero padding, 64-bit little-endian bit length.
  const rest = bytes.length - whole;
  const tailLength = rest < 56 ? 64 : 128;
  const tail = new Uint8Array(tailLength);
  tail.set(bytes.subarray(whole));
  tail[rest] = 0x80;
  const tailView = new DataView(tail.buffer);
  tailView.setUint32(tailLength - 8, (bytes.length * 8) >>> 0, true);
  tailView.setUint32(tailLength - 4, Math.floor(bytes.length / 0x20000000), true);
  for (let offset = 0; offset < tailLength; offset += 64) block(tailView, offset);

  const digest = new DataView(new ArrayBuffer(16));
  digest.setInt32(0, a0, true);
  digest.setInt32(4, b0, true);
  digest.setInt32(8, c0, true);
  digest.setInt32(12, d0, true);
  let hex = "";
  for (let i = 0; i < 16; i++) hex += digest.getUint8(i).toString(16).padStart(2, "0");
  return hex;
}
