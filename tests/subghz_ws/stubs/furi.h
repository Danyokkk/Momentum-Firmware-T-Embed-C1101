/* Host-test stand-in for <furi.h>: only what lib/subghz decoders touch. */
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same contract as components/furi/core/memmgr.h on the device: Flipper code
 * assumes malloc() returns zeroed memory (decoders never initialise their
 * parser state), so the port redirects it to calloc(). */
#define malloc(size) calloc(1, (size))

#ifdef __cplusplus
extern "C" {
#endif

void host_furi_fail(const char* kind, const char* expr, const char* file, int line);

/* Both stay active in tests: a decoder tripping an assert is a test failure. */
#define furi_assert(x, ...) \
    do {                    \
        if(!(x)) host_furi_fail("furi_assert", #x, __FILE__, __LINE__); \
    } while(0)
#define furi_check(x, ...) \
    do {                   \
        if(!(x)) host_furi_fail("furi_check", #x, __FILE__, __LINE__); \
    } while(0)
#define furi_crash(...) host_furi_fail("furi_crash", "", __FILE__, __LINE__)

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef EXT_PATH
#define EXT_PATH(path) "/ext/" path
#endif

#define FURI_LOG_E(tag, ...) host_furi_log('E', tag, __VA_ARGS__)
#define FURI_LOG_W(tag, ...) host_furi_log('W', tag, __VA_ARGS__)
#define FURI_LOG_I(tag, ...) host_furi_log('I', tag, __VA_ARGS__)
#define FURI_LOG_D(tag, ...) host_furi_log('D', tag, __VA_ARGS__)
#define FURI_LOG_T(tag, ...) host_furi_log('T', tag, __VA_ARGS__)
void host_furi_log(char level, const char* tag, const char* fmt, ...);
/* Number of FURI_LOG_E calls so far; tests use it to spot error paths. */
extern unsigned host_furi_log_errors;

typedef struct FuriString FuriString;
FuriString* furi_string_alloc(void);
void furi_string_free(FuriString* s);
void furi_string_reset(FuriString* s);
void furi_string_set(FuriString* s, const char* cstr);
void furi_string_cat(FuriString* s, const char* cstr);
int furi_string_cat_printf(FuriString* s, const char* fmt, ...);
int furi_string_printf(FuriString* s, const char* fmt, ...);
const char* furi_string_get_cstr(const FuriString* s);
size_t furi_string_size(const FuriString* s);

#ifdef __cplusplus
}
#endif
