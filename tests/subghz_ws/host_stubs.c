/* Host implementations behind tests/subghz_ws/stubs. Test-only code. */
#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>

/* ---------------------------------------------------------------- furi --- */

unsigned host_furi_log_errors = 0;
FuriHalRtcLocaleUnits host_locale_units = FuriHalRtcLocaleUnitsMetric;
uint32_t host_rtc_timestamp = 1700000000u;

void host_furi_fail(const char* kind, const char* expr, const char* file, int line) {
    fprintf(stderr, "%s failed: %s (%s:%d)\n", kind, expr, file, line);
    fflush(stderr);
    abort();
}

void host_furi_log(char level, const char* tag, const char* fmt, ...) {
    if(level == 'E') host_furi_log_errors++;
    if(getenv("HOST_FURI_LOG") == NULL) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[%c][%s] ", level, tag);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

struct FuriString {
    char* data;
    size_t size;
    size_t capacity;
};

static void furi_string_reserve(FuriString* s, size_t needed) {
    if(needed + 1 <= s->capacity) return;
    size_t capacity = s->capacity ? s->capacity : 32;
    while(capacity < needed + 1) capacity *= 2;
    s->data = realloc(s->data, capacity);
    furi_check(s->data);
    s->capacity = capacity;
}

FuriString* furi_string_alloc(void) {
    FuriString* s = calloc(1, sizeof(FuriString));
    furi_check(s);
    furi_string_reserve(s, 0);
    s->data[0] = '\0';
    return s;
}

void furi_string_free(FuriString* s) {
    furi_check(s);
    free(s->data);
    free(s);
}

void furi_string_reset(FuriString* s) {
    furi_check(s);
    s->size = 0;
    s->data[0] = '\0';
}

void furi_string_cat(FuriString* s, const char* cstr) {
    furi_check(s && cstr);
    size_t extra = strlen(cstr);
    furi_string_reserve(s, s->size + extra);
    memcpy(s->data + s->size, cstr, extra + 1);
    s->size += extra;
}

void furi_string_set(FuriString* s, const char* cstr) {
    furi_string_reset(s);
    furi_string_cat(s, cstr);
}

static int furi_string_vcat(FuriString* s, const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    furi_check(needed >= 0);
    furi_string_reserve(s, s->size + (size_t)needed);
    vsnprintf(s->data + s->size, (size_t)needed + 1, fmt, args);
    s->size += (size_t)needed;
    return needed;
}

int furi_string_cat_printf(FuriString* s, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = furi_string_vcat(s, fmt, args);
    va_end(args);
    return written;
}

int furi_string_printf(FuriString* s, const char* fmt, ...) {
    furi_string_reset(s);
    va_list args;
    va_start(args, fmt);
    int written = furi_string_vcat(s, fmt, args);
    va_end(args);
    return written;
}

const char* furi_string_get_cstr(const FuriString* s) {
    furi_check(s);
    return s->data;
}

size_t furi_string_size(const FuriString* s) {
    furi_check(s);
    return s->size;
}

/* ------------------------------------------------------- flipper format --- */

#define FF_MAX_ITEMS 64
#define FF_MAX_KEY   48
#define FF_MAX_VALUE 512

typedef struct {
    char key[FF_MAX_KEY];
    char value[FF_MAX_VALUE];
} FfItem;

struct FlipperFormat {
    FfItem items[FF_MAX_ITEMS];
    size_t count;
    size_t cursor; /* next item a forward search starts from */
};

struct Stream {
    FlipperFormat* owner;
};

FlipperFormat* flipper_format_string_alloc(void) {
    FlipperFormat* ff = calloc(1, sizeof(FlipperFormat));
    furi_check(ff);
    return ff;
}

void flipper_format_free(FlipperFormat* ff) {
    free(ff);
}

bool flipper_format_rewind(FlipperFormat* ff) {
    furi_check(ff);
    ff->cursor = 0;
    return true;
}

Stream* flipper_format_get_raw_stream(FlipperFormat* ff) {
    /* The document doubles as its own stream handle. */
    return (Stream*)ff;
}

bool stream_clean(Stream* stream) {
    FlipperFormat* ff = (FlipperFormat*)stream;
    furi_check(ff);
    ff->count = 0;
    ff->cursor = 0;
    return true;
}

static bool ff_append(FlipperFormat* ff, const char* key, const char* value) {
    furi_check(ff && key && value);
    if(ff->count >= FF_MAX_ITEMS) return false;
    if(strlen(key) >= FF_MAX_KEY || strlen(value) >= FF_MAX_VALUE) return false;
    strcpy(ff->items[ff->count].key, key);
    strcpy(ff->items[ff->count].value, value);
    ff->count++;
    return true;
}

/* Forward search from the cursor; a miss parks the cursor at the end. */
static const char* ff_seek(FlipperFormat* ff, const char* key) {
    furi_check(ff && key);
    for(size_t i = ff->cursor; i < ff->count; i++) {
        if(strcmp(ff->items[i].key, key) == 0) {
            ff->cursor = i + 1;
            return ff->items[i].value;
        }
    }
    ff->cursor = ff->count;
    return NULL;
}

bool flipper_format_write_header_cstr(FlipperFormat* ff, const char* type, uint32_t version) {
    char number[16];
    snprintf(number, sizeof(number), "%lu", (unsigned long)version);
    return ff_append(ff, "Filetype", type) && ff_append(ff, "Version", number);
}

bool flipper_format_write_string_cstr(FlipperFormat* ff, const char* key, const char* data) {
    return ff_append(ff, key, data);
}

bool flipper_format_write_uint32(
    FlipperFormat* ff,
    const char* key,
    const uint32_t* data,
    uint16_t count) {
    char value[FF_MAX_VALUE] = {0};
    size_t used = 0;
    for(uint16_t i = 0; i < count; i++) {
        int n = snprintf(
            value + used, sizeof(value) - used, i ? " %lu" : "%lu", (unsigned long)data[i]);
        if(n < 0 || (size_t)n >= sizeof(value) - used) return false;
        used += (size_t)n;
    }
    return ff_append(ff, key, value);
}

bool flipper_format_write_hex(
    FlipperFormat* ff,
    const char* key,
    const uint8_t* data,
    uint16_t count) {
    char value[FF_MAX_VALUE] = {0};
    size_t used = 0;
    for(uint16_t i = 0; i < count; i++) {
        int n = snprintf(value + used, sizeof(value) - used, i ? " %02X" : "%02X", data[i]);
        if(n < 0 || (size_t)n >= sizeof(value) - used) return false;
        used += (size_t)n;
    }
    return ff_append(ff, key, value);
}

bool flipper_format_write_float(
    FlipperFormat* ff,
    const char* key,
    const float* data,
    uint16_t count) {
    char value[FF_MAX_VALUE] = {0};
    size_t used = 0;
    for(uint16_t i = 0; i < count; i++) {
        int n = snprintf(
            value + used, sizeof(value) - used, i ? " %f" : "%f", (double)data[i]);
        if(n < 0 || (size_t)n >= sizeof(value) - used) return false;
        used += (size_t)n;
    }
    return ff_append(ff, key, value);
}

bool flipper_format_read_string(FlipperFormat* ff, const char* key, FuriString* data) {
    const char* value = ff_seek(ff, key);
    if(!value) return false;
    furi_string_set(data, value);
    return true;
}

bool flipper_format_read_uint32(FlipperFormat* ff, const char* key, uint32_t* data, uint16_t count) {
    const char* value = ff_seek(ff, key);
    if(!value) return false;
    for(uint16_t i = 0; i < count; i++) {
        while(*value == ' ') value++;
        if(!isdigit((unsigned char)*value)) return false;
        char* end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(value, &end, 10);
        if(end == value || errno != 0 || parsed > 0xFFFFFFFFul) return false;
        data[i] = (uint32_t)parsed;
        value = end;
    }
    return true;
}

bool flipper_format_read_hex(FlipperFormat* ff, const char* key, uint8_t* data, uint16_t count) {
    const char* value = ff_seek(ff, key);
    if(!value) return false;
    for(uint16_t i = 0; i < count; i++) {
        while(*value == ' ') value++;
        if(!isxdigit((unsigned char)value[0]) || !isxdigit((unsigned char)value[1])) return false;
        char byte[3] = {value[0], value[1], '\0'};
        data[i] = (uint8_t)strtoul(byte, NULL, 16);
        value += 2;
    }
    return true;
}

bool flipper_format_read_float(FlipperFormat* ff, const char* key, float* data, uint16_t count) {
    const char* value = ff_seek(ff, key);
    if(!value) return false;
    for(uint16_t i = 0; i < count; i++) {
        char* end = NULL;
        float parsed = strtof(value, &end);
        if(end == value) return false;
        data[i] = parsed;
        value = end;
    }
    return true;
}

bool ff_host_load_text(FlipperFormat* ff, const char* text) {
    furi_check(ff && text);
    ff->count = 0;
    ff->cursor = 0;
    const char* line = text;
    while(*line) {
        const char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        while(length && (line[length - 1] == '\r' || line[length - 1] == ' ')) length--;
        const char* colon = memchr(line, ':', length);
        if(length && line[0] != '#' && colon) {
            char key[FF_MAX_KEY] = {0};
            char value[FF_MAX_VALUE] = {0};
            size_t key_length = (size_t)(colon - line);
            const char* value_start = colon + 1;
            while(value_start < line + length && *value_start == ' ') value_start++;
            size_t value_length = (size_t)(line + length - value_start);
            if(key_length >= FF_MAX_KEY || value_length >= FF_MAX_VALUE) return false;
            memcpy(key, line, key_length);
            memcpy(value, value_start, value_length);
            if(!ff_append(ff, key, value)) return false;
        }
        if(!end) break;
        line = end + 1;
    }
    return true;
}

size_t ff_host_count(const FlipperFormat* ff) {
    return ff->count;
}

const char* ff_host_key_at(const FlipperFormat* ff, size_t index) {
    return index < ff->count ? ff->items[index].key : NULL;
}

const char* ff_host_value(const FlipperFormat* ff, const char* key) {
    for(size_t i = 0; i < ff->count; i++) {
        if(strcmp(ff->items[i].key, key) == 0) return ff->items[i].value;
    }
    return NULL;
}
