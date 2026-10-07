/**
 * display.h - ST7735 TFT Display Driver Header
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// COLORS (RGB565)
// ============================================================
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F

// ============================================================
// WAVEFORM TYPES (matching MicroPython)
// ============================================================
typedef enum {
    WAVE_SINE = 0,
    WAVE_SAW = 1,
    WAVE_SQUARE = 2,
    WAVE_TRIANGLE = 3
} WaveformType;

// ============================================================
// SYNTH DISPLAY STATE
// ============================================================
typedef struct {
    WaveformType current_waveform;
    int8_t current_octave;  // -1, 0, +1
    int volume;             // 0-100
    int vibrato;            // 0-100
    int filter;             // 0-100
    int notes_playing;      // 0-13
} SynthDisplay;

// ============================================================
// BASIC DISPLAY FUNCTIONS
// ============================================================
void display_init(void);
void display_fill(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_pixel(int x, int y, uint16_t color);
void display_hline(int x, int y, int w, uint16_t color);
void display_vline(int x, int y, int h, uint16_t color);
void display_rect(int x, int y, int w, int h, uint16_t color);
void display_text(const char* text, int x, int y, uint16_t color, uint8_t scale);

// ============================================================
// SYNTH UI FUNCTIONS
// ============================================================
void synth_display_init(void);
void synth_display_update_waveform(void);
void synth_display_update_octave(void);
void synth_display_update_volume(int volume_percent);
void synth_display_update_vibrato(int vibrato_percent);
void synth_display_update_filter(int filter_percent);
void synth_display_update_notes(int note_count);

// Setters with display update
void synth_display_set_waveform(WaveformType waveform);
void synth_display_set_octave(int8_t octave);

// Cycle functions (return new value)
WaveformType synth_display_cycle_waveform(void);
int8_t synth_display_cycle_octave(void);

// Getters
WaveformType synth_display_get_waveform(void);
int8_t synth_display_get_octave(void);

// Ready indicator
void synth_display_show_ready(bool ready);

#endif // DISPLAY_H
