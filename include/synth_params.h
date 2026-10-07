/**
 * synth_params.h - Tunable synthesis parameters for rapid experimentation
 *
 * All polyphony-related parameters in one place for quick A/B testing.
 * Includes presets and real-time tweakability via web interface.
 */

#ifndef SYNTH_PARAMS_H
#define SYNTH_PARAMS_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// SYNTH PARAMETER STRUCTURE
// ============================================================

typedef struct {
    // === ENVELOPE ===
    uint8_t attack_rate;      // 1-64: higher = faster attack (samples to reach max)
    uint8_t decay_rate;       // 1-32: higher = faster decay
    uint8_t sustain_level;    // 0-255: sustain amplitude
    uint8_t release_rate;     // 1-32: higher = faster release

    // === DETUNING ===
    uint8_t detune_amount;    // 0-100: spread between voices (0=none, 100=max)
    uint8_t detune_mode;      // 0=off, 1=alternating, 2=spread, 3=random

    // === GAIN/MIXING ===
    uint8_t gain_mode;        // 0=linear, 1=sqrt, 2=constant, 3=soft
    uint8_t headroom;         // Voice amplitude 50-150 (% of default 12000)

    // === FILTER ===
    uint8_t filter_min;       // Minimum filter coef (0-50, darker)
    uint8_t filter_max;       // Maximum filter coef (200-255, brighter)

    // === SATURATION ===
    uint8_t sat_threshold;    // 50-150: when soft clip kicks in (% of 12000)
    uint8_t sat_hardness;     // 0-100: 0=very soft, 100=hard clip

    // === PHASE ===
    uint8_t phase_mode;       // 0=sync (all start at 0), 1=random, 2=spread

    // === STEREO ===
    uint8_t stereo_width;     // 0-100: 0=mono, 100=max spread

} SynthParams;

// ============================================================
// PRESET DEFINITIONS
// ============================================================

#define PRESET_COUNT 8

// Preset names (for display)
static const char* const PRESET_NAMES[PRESET_COUNT] = {
    "STOCK",      // 0: Your current settings
    "CLEAN",      // 1: Minimal processing, let voices shine
    "ANALOG",     // 2: Analog-style detuning and warmth
    "BRIGHT",     // 3: Open filter, crisp attack
    "PAD",        // 4: Slow attack, long release
    "TIGHT",      // 5: Fast envelope, punchy
    "WIDE",       // 6: Stereo spread, detuned
    "CUSTOM"      // 7: User-tweakable via web
};

// Default presets
static const SynthParams PRESETS[PRESET_COUNT] = {
    // 0: STOCK - Clean baseline, no processing
    {
        .attack_rate = 8,      // Fast attack
        .decay_rate = 2,
        .sustain_level = 220,  // High sustain
        .release_rate = 4,
        .detune_amount = 0,    // NO detuning - clean chords
        .detune_mode = 0,      // Off
        .gain_mode = 0,        // Linear division
        .headroom = 100,
        .filter_min = 255,     // No filtering
        .filter_max = 255,
        .sat_threshold = 100,
        .sat_hardness = 50,
        .phase_mode = 0,       // Sync phase - cleanest baseline
        .stereo_width = 0      // Mono
    },

    // 1: CLEAN - Absolutely minimal, for A/B testing
    {
        .attack_rate = 16,     // Very fast
        .decay_rate = 4,
        .sustain_level = 250,  // Near max sustain
        .release_rate = 8,
        .detune_amount = 0,    // NO detuning
        .detune_mode = 0,
        .gain_mode = 0,        // Linear division
        .headroom = 100,
        .filter_min = 255,     // Filter fully open (bypassed)
        .filter_max = 255,
        .sat_threshold = 150,
        .sat_hardness = 0,     // No saturation
        .phase_mode = 0,       // Sync phase
        .stereo_width = 0
    },

    // 2: ANALOG - Vintage detuning, warm
    {
        .attack_rate = 4,
        .decay_rate = 2,
        .sustain_level = 200,
        .release_rate = 3,
        .detune_amount = 80,   // More detuning
        .detune_mode = 2,      // Spread mode
        .gain_mode = 3,        // Soft gain
        .headroom = 90,
        .filter_min = 20,      // Warmer
        .filter_max = 180,     // Not too bright
        .sat_threshold = 80,   // Earlier saturation
        .sat_hardness = 30,    // Warm saturation
        .phase_mode = 1,       // Random
        .stereo_width = 30
    },

    // 3: BRIGHT - Crisp, open
    {
        .attack_rate = 16,     // Very fast
        .decay_rate = 4,
        .sustain_level = 190,
        .release_rate = 4,
        .detune_amount = 20,   // Subtle
        .detune_mode = 1,
        .gain_mode = 1,
        .headroom = 100,
        .filter_min = 150,     // Always bright
        .filter_max = 255,
        .sat_threshold = 120,
        .sat_hardness = 60,
        .phase_mode = 0,
        .stereo_width = 10
    },

    // 4: PAD - Slow, ambient
    {
        .attack_rate = 1,      // Very slow attack
        .decay_rate = 1,
        .sustain_level = 230,  // High sustain
        .release_rate = 1,     // Long release
        .detune_amount = 60,
        .detune_mode = 2,      // Spread
        .gain_mode = 3,        // Soft
        .headroom = 85,
        .filter_min = 30,
        .filter_max = 150,     // Never too bright
        .sat_threshold = 90,
        .sat_hardness = 20,
        .phase_mode = 1,
        .stereo_width = 50
    },

    // 5: TIGHT - Punchy, responsive
    {
        .attack_rate = 32,     // Instant attack
        .decay_rate = 8,       // Fast decay
        .sustain_level = 150,  // Lower sustain
        .release_rate = 8,     // Fast release
        .detune_amount = 10,   // Minimal
        .detune_mode = 1,
        .gain_mode = 0,        // Linear (aggressive)
        .headroom = 110,
        .filter_min = 100,
        .filter_max = 255,
        .sat_threshold = 100,
        .sat_hardness = 70,
        .phase_mode = 0,       // Sync
        .stereo_width = 0
    },

    // 6: WIDE - Stereo, lush
    {
        .attack_rate = 4,
        .decay_rate = 2,
        .sustain_level = 200,
        .release_rate = 3,
        .detune_amount = 100,  // Max detuning
        .detune_mode = 3,      // Random
        .gain_mode = 3,
        .headroom = 75,
        .filter_min = 40,
        .filter_max = 200,
        .sat_threshold = 85,
        .sat_hardness = 25,
        .phase_mode = 1,
        .stereo_width = 100    // Full stereo
    },

    // 7: CUSTOM - User tweakable (starts as STOCK)
    {
        .attack_rate = 2,
        .decay_rate = 1,
        .sustain_level = 180,
        .release_rate = 2,
        .detune_amount = 50,
        .detune_mode = 1,
        .gain_mode = 1,
        .headroom = 100,
        .filter_min = 8,
        .filter_max = 250,
        .sat_threshold = 100,
        .sat_hardness = 50,
        .phase_mode = 1,
        .stereo_width = 0
    }
};

// ============================================================
// HELPER MACROS
// ============================================================

// Convert headroom percentage to amplitude (base = 12000)
#define HEADROOM_TO_AMP(h) ((12000 * (h)) / 100)

// Convert sat_threshold to actual value
#define SAT_THRESH_TO_VAL(s) ((12000 * (s)) / 100)

#endif // SYNTH_PARAMS_H
