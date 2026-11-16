#pragma once

/* Minimal stub. On the original Flipper (STM32) this header configures the MCU
 * clock tree; on this ESP32 port the clocks are set up by ESP-IDF. It exists
 * only because some upstream apps (e.g. clock_settings) include it; none of the
 * built apps call its functions. */
