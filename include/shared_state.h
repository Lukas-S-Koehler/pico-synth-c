/**
 * shared_state.h - Thread-safe shared state between cores
 *
 * Core 0 (UI): writes pressed_mask, volume, vibrato, filter, waveform, octave
 * Core 1 (Audio): reads all of the above
 *
 * Uses volatile and aligned accesses for safe inter-core communication
 */

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "display.h"
#include "synth_params.h"

// ============================================================
// SHARED STATE STRUCTURE
// ============================================================

typedef struct {
    // Button state (bitmask for 13 keys)
    volatile uint32_t pressed_mask;

    // Potentiometer values (0-100 percent)
    volatile uint8_t volume;      // 0-100
    volatile uint8_t vibrato;     // 0-100
    volatile uint8_t filter;      // 0-100

    // Waveform and octave
    volatile uint8_t waveform;    // 0=SINE, 1=SAW, 2=SQUARE, 3=TRIANGLE
    volatile int8_t octave;       // -1, 0, +1

    // Control flags
    volatile bool audio_running;
    volatile bool audio_ready;     // True when Core 1 is ready to play

    // Web UI overrides (when enabled, use these instead of pots)
    volatile bool web_vibrato_override;   // If true, use web_vibrato value
    volatile bool web_filter_override;    // If true, use web_filter value
    volatile uint8_t web_vibrato;         // 0-100
    volatile uint8_t web_filter;          // 0-100

    // Synth parameters (for polyphony tuning)
    volatile uint8_t preset_index;        // Current preset 0-7
    volatile SynthParams params;          // Active parameters
    volatile bool params_dirty;           // True when params changed
} SharedState;

// ============================================================
// GLOBAL SHARED STATE
// ============================================================

// Defined in shared_state.c
extern SharedState shared_state;

// ============================================================
// WRITER FUNCTIONS (Core 0 - UI)
// ============================================================

static inline void shared_set_pressed(uint32_t mask) {
    shared_state.pressed_mask = mask;
}

static inline void shared_set_volume(uint8_t vol) {
    shared_state.volume = vol;
}

static inline void shared_set_vibrato(uint8_t vib) {
    shared_state.vibrato = vib;
}

static inline void shared_set_filter(uint8_t flt) {
    shared_state.filter = flt;
}

static inline void shared_set_waveform(WaveformType wf) {
    shared_state.waveform = (uint8_t)wf;
}

static inline void shared_set_octave(int8_t oct) {
    shared_state.octave = oct;
}

static inline void shared_start_audio(void) {
    shared_state.audio_running = true;
}

static inline void shared_stop_audio(void) {
    shared_state.audio_running = false;
}

// Cycle waveform and return new value
static inline WaveformType shared_cycle_waveform(void) {
    uint8_t wf = (shared_state.waveform + 1) % 4;
    shared_state.waveform = wf;
    return (WaveformType)wf;
}

// Cycle octave and return new value
static inline int8_t shared_cycle_octave(void) {
    int8_t oct = shared_state.octave + 1;
    if (oct > 1) oct = -1;
    shared_state.octave = oct;
    return oct;
}

// ============================================================
// READER FUNCTIONS (Core 1 - Audio)
// ============================================================

static inline uint32_t shared_get_pressed(void) {
    return shared_state.pressed_mask;
}

static inline uint8_t shared_get_volume(void) {
    return shared_state.volume;
}

static inline uint8_t shared_get_vibrato(void) {
    return shared_state.vibrato;
}

static inline uint8_t shared_get_filter(void) {
    return shared_state.filter;
}

static inline WaveformType shared_get_waveform(void) {
    return (WaveformType)shared_state.waveform;
}

static inline int8_t shared_get_octave(void) {
    return shared_state.octave;
}

static inline bool shared_is_audio_running(void) {
    return shared_state.audio_running;
}

static inline void shared_set_ready(bool ready) {
    shared_state.audio_ready = ready;
}

static inline bool shared_is_ready(void) {
    return shared_state.audio_ready;
}

// ============================================================
// WEB UI OVERRIDES
// ============================================================

static inline void shared_set_web_vibrato(bool enabled, uint8_t value) {
    shared_state.web_vibrato = value;
    shared_state.web_vibrato_override = enabled;
}

static inline void shared_set_web_filter(bool enabled, uint8_t value) {
    shared_state.web_filter = value;
    shared_state.web_filter_override = enabled;
}

static inline bool shared_get_web_vibrato_override(void) {
    return shared_state.web_vibrato_override;
}

static inline bool shared_get_web_filter_override(void) {
    return shared_state.web_filter_override;
}

static inline uint8_t shared_get_web_vibrato(void) {
    return shared_state.web_vibrato;
}

static inline uint8_t shared_get_web_filter(void) {
    return shared_state.web_filter;
}

// ============================================================
// SYNTH PARAMETERS (for polyphony tuning)
// ============================================================

// Get current preset index
static inline uint8_t shared_get_preset(void) {
    return shared_state.preset_index;
}

// Set preset by index (loads preset params)
static inline void shared_set_preset(uint8_t idx) {
    if (idx >= PRESET_COUNT) idx = 0;
    shared_state.preset_index = idx;
    shared_state.params = PRESETS[idx];
    shared_state.params_dirty = true;
}

// Cycle to next preset
static inline uint8_t shared_cycle_preset(void) {
    uint8_t next = (shared_state.preset_index + 1) % PRESET_COUNT;
    shared_set_preset(next);
    return next;
}

// Get current params (copy to avoid race conditions)
static inline SynthParams shared_get_params(void) {
    return shared_state.params;
}

// Set individual parameter (auto-switches to CUSTOM preset)
static inline void shared_set_param_attack(uint8_t val) {
    shared_state.params.attack_rate = val;
    shared_state.preset_index = 7;  // CUSTOM
    shared_state.params_dirty = true;
}

static inline void shared_set_param_decay(uint8_t val) {
    shared_state.params.decay_rate = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_sustain(uint8_t val) {
    shared_state.params.sustain_level = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_release(uint8_t val) {
    shared_state.params.release_rate = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_detune_amount(uint8_t val) {
    shared_state.params.detune_amount = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_detune_mode(uint8_t val) {
    shared_state.params.detune_mode = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_gain_mode(uint8_t val) {
    shared_state.params.gain_mode = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_headroom(uint8_t val) {
    shared_state.params.headroom = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_sat_threshold(uint8_t val) {
    shared_state.params.sat_threshold = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_sat_hardness(uint8_t val) {
    shared_state.params.sat_hardness = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_phase_mode(uint8_t val) {
    shared_state.params.phase_mode = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

static inline void shared_set_param_stereo(uint8_t val) {
    shared_state.params.stereo_width = val;
    shared_state.preset_index = 7;
    shared_state.params_dirty = true;
}

// Check and clear dirty flag
static inline bool shared_params_changed(void) {
    bool dirty = shared_state.params_dirty;
    shared_state.params_dirty = false;
    return dirty;
}

// ============================================================
// INITIALIZATION
// ============================================================

void shared_state_init(void);

#endif // SHARED_STATE_H
