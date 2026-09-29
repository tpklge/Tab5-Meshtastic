#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Beep patterns.
typedef enum {
    AUDIO_PAT_SILENT  = 0,
    AUDIO_PAT_SHORT   = 1,
    AUDIO_PAT_DOUBLE  = 2,
    AUDIO_PAT_TRIPLE  = 3,
} audio_pattern_t;

// Initialize ES8388 audio codec and I2S driver via BSP.
// Must be called after board.begin(). Non-fatal if hardware absent.
esp_err_t tab5_audio_init(void);

// Set master volume (0–100). Takes effect immediately.
void tab5_audio_set_volume(uint8_t vol);

// Play a non-blocking beep pattern. Returns immediately.
// Beep is generated in a background task; safe to call from any context.
void tab5_audio_beep(audio_pattern_t pat);

// Returns true if audio hardware is available.
bool tab5_audio_available(void);

#ifdef __cplusplus
}
#endif
