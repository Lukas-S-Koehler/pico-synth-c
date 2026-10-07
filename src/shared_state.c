/**
 * shared_state.c - Thread-safe shared state between cores
 */

#include "shared_state.h"

// Global shared state instance
// Aligned to 4 bytes for atomic access on ARM
__attribute__((aligned(4)))
SharedState shared_state;

void shared_state_init(void) {
    shared_state.pressed_mask = 0;
    shared_state.volume = 50;      // 50%
    shared_state.vibrato = 0;      // Off
    shared_state.filter = 80;      // 80%
    shared_state.waveform = 0;     // SINE
    shared_state.octave = 0;       // Middle octave
    shared_state.audio_running = false;
    shared_state.audio_ready = false;

    // Initialize synth params with STOCK preset
    shared_state.preset_index = 0;
    shared_state.params = PRESETS[0];
    shared_state.params_dirty = false;
}
