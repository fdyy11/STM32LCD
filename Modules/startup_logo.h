#ifndef STARTUP_LOGO_H
#define STARTUP_LOGO_H

#include <stdint.h>

#define STARTUP_LOGO_WIDTH 268u
#define STARTUP_LOGO_HEIGHT 220u
#define STARTUP_LOGO_ROW_BYTES ((STARTUP_LOGO_WIDTH + 1u) / 2u)

/* Two palette indices per byte, high nibble first; rows are byte-aligned. */
extern const uint16_t startup_logo_palette[16];
extern const uint8_t startup_logo[STARTUP_LOGO_ROW_BYTES * STARTUP_LOGO_HEIGHT];

#endif
