/* Host-test stand-in for FlipperFormat: an in-memory "Key: value" document.
 *
 * Reads follow the real implementation's rule that matters to key files: a
 * read searches FORWARD from the current position, so fields must be read in
 * file order unless the caller rewinds. A missed key leaves the cursor at the
 * end, as on the device. */
#pragma once

#include <furi.h>
#include <lib/toolbox/stream/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipperFormat FlipperFormat;

FlipperFormat* flipper_format_string_alloc(void);
void flipper_format_free(FlipperFormat* ff);
bool flipper_format_rewind(FlipperFormat* ff);
Stream* flipper_format_get_raw_stream(FlipperFormat* ff);

bool flipper_format_write_header_cstr(FlipperFormat* ff, const char* type, uint32_t version);
bool flipper_format_write_string_cstr(FlipperFormat* ff, const char* key, const char* data);
bool flipper_format_write_uint32(
    FlipperFormat* ff,
    const char* key,
    const uint32_t* data,
    uint16_t count);
bool flipper_format_write_hex(FlipperFormat* ff, const char* key, const uint8_t* data, uint16_t count);
bool flipper_format_write_float(FlipperFormat* ff, const char* key, const float* data, uint16_t count);

bool flipper_format_read_string(FlipperFormat* ff, const char* key, FuriString* data);
bool flipper_format_read_uint32(FlipperFormat* ff, const char* key, uint32_t* data, uint16_t count);
bool flipper_format_read_hex(FlipperFormat* ff, const char* key, uint8_t* data, uint16_t count);
bool flipper_format_read_float(FlipperFormat* ff, const char* key, float* data, uint16_t count);

/* ---- host-only helpers (not part of the firmware API) ---- */
bool ff_host_load_text(FlipperFormat* ff, const char* text);
size_t ff_host_count(const FlipperFormat* ff);
const char* ff_host_key_at(const FlipperFormat* ff, size_t index);
/* Position-independent lookup; NULL when the key is absent. */
const char* ff_host_value(const FlipperFormat* ff, const char* key);

#ifdef __cplusplus
}
#endif
