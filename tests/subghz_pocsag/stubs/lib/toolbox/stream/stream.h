/* Host-test stand-in for the toolbox stream API. */
#pragma once

#include <stdbool.h>

typedef struct Stream Stream;
bool stream_clean(Stream* stream);
