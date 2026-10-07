/**
 * Pico Synthesizer - Main Program
 *
 * Architecture:
 *   Core 0: UI, buttons, potentiometers, display
 *   Core 1: Dedicated real-time audio generation
 *
 * Hardware:
 *   - 12 Keys (GP0-GP5, GP12-GP15, GP19, GP20) for notes C4-B4
 *   - Volume Potentiometer (GP27 / ADC1)
 *   - Vibrato Potentiometer (GP26 / ADC0)
 *   - Filter Potentiometer (GP28 / ADC2)
 *   - Waveform Button (GP21)
 *   - Octave Button (GP22)
 *   - I2S Audio (GP16, 17, 18)
 *   - Optional ST7735 display (GP10, 11, 19, 20, 21) - disabled by default,
 *     its CS/DC/RST pins overlap with keys A#/B and the waveform button
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"

#include "synth.h"
#include "display.h"
#include "shared_state.h"
#include "wifi_server.h"

// ============================================================
// PIN DEFINITIONS
// ============================================================

// Note buttons: 12 keys (C4 to B4) - removed GP22 to free it for octave button
static const uint8_t BUTTON_PINS[12] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 19, 20};
#define NUM_BUTTONS 12

#define VOLUME_POTI_PIN     27
#define VIBRATO_POTI_PIN    26
#define FILTER_POTI_PIN     28
#define WAVEFORM_BUTTON_PIN 21
#define OCTAVE_BUTTON_PIN   22

#define DISPLAY_UPDATE_INTERVAL_MS  100
#define POTI_UPDATE_INTERVAL        5

// The ST7735 display shares GP19/20/21 with two note keys and the waveform
// button, so it is compiled out unless SYNTH_ENABLE_DISPLAY is set in CMake
#if SYNTH_ENABLE_DISPLAY
#define DISPLAY_CALL(call) call
#else
#define DISPLAY_CALL(call) ((void)0)
#endif

// ============================================================
// POTENTIOMETER HANDLING
// ============================================================

typedef struct {
    uint8_t adc_input;
    uint16_t last_raw;
    uint8_t last_percent;
    float smoothed;
} Potentiometer;

static void pot_init(Potentiometer* pot, uint8_t gpio_pin) {
    if (gpio_pin == 26) pot->adc_input = 0;
    else if (gpio_pin == 27) pot->adc_input = 1;
    else if (gpio_pin == 28) pot->adc_input = 2;
    else pot->adc_input = 0;

    adc_gpio_init(gpio_pin);
    adc_select_input(pot->adc_input);
    pot->last_raw = adc_read();
    pot->smoothed = (float)pot->last_raw / 4095.0f;
    pot->last_percent = (uint8_t)(pot->smoothed * 100.0f);  // Linear 0-100
}

static uint8_t pot_read_percent(Potentiometer* pot) {
    adc_select_input(pot->adc_input);
    sleep_us(100);  // Longer settling time for ADC channel switch

    // Take multiple samples and average (with delays)
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += adc_read();
        sleep_us(10);
    }
    uint16_t raw = sum / 4;

    // Linear smoothing (80% old, 20% new for responsiveness)
    float new_val = (float)raw / 4095.0f;
    pot->smoothed = pot->smoothed * 0.8f + new_val * 0.2f;

    // Linear 0-100 mapping (no curve)
    pot->last_raw = raw;
    uint8_t pct = (uint8_t)(pot->smoothed * 100.0f);
    if (pct > 100) pct = 100;
    pot->last_percent = pct;

    return pot->last_percent;
}

// Heavy smoothing version for noisy pots (like vibrato)
static uint8_t pot_read_percent_heavy(Potentiometer* pot) {
    adc_select_input(pot->adc_input);
    sleep_us(100);  // Longer settling time for ADC channel switch

    // Take multiple samples and average (with delays)
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += adc_read();
        sleep_us(10);
    }
    uint16_t raw = sum / 4;

    // Heavier smoothing (90% old, 10% new)
    float new_val = (float)raw / 4095.0f;
    pot->smoothed = pot->smoothed * 0.9f + new_val * 0.1f;

    // Linear 0-100 mapping
    pot->last_raw = raw;
    uint8_t pct = (uint8_t)(pot->smoothed * 100.0f);
    if (pct > 100) pct = 100;
    pot->last_percent = pct;

    return pot->last_percent;
}

// ============================================================
// AUDIO CONSTANTS
// ============================================================

#define SAMPLE_RATE_AUDIO 22050  // Lower sample rate - matches audio_driver.c
#define TABLE_SIZE 2048           // Larger table = smoother interpolation
#define TABLE_MASK (TABLE_SIZE - 1)

// Phase accumulator uses 16.16 fixed point
// phase_step = (freq * TABLE_SIZE * 65536) / SAMPLE_RATE
// Simplified: phase_step = freq * 3038.8 (approx)

// ============================================================
// AUDIO CORE (Core 1)
// ============================================================

// Pre-computed wavetables
static int16_t wt_sine[TABLE_SIZE];
static int16_t wt_saw[TABLE_SIZE];
static int16_t wt_square[TABLE_SIZE];
static int16_t wt_triangle[TABLE_SIZE];

// Pre-computed phase increments for C4-C5 at each octave
static uint32_t phase_inc[3][13];

// Note frequencies (C4 to B4) - 12 notes, one octave
static const float note_freqs[12] = {
    261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
    369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f
};

static void generate_wavetables(void) {
    // Full amplitude - we'll handle gain in mixing
    // Use 16000 as base (leaves headroom in int16)
    for (int i = 0; i < TABLE_SIZE; i++) {
        float t = (float)i / TABLE_SIZE;
        float angle = t * 2.0f * 3.14159265f;

        // Sine - pure tone, DC-free by definition
        wt_sine[i] = (int16_t)(sinf(angle) * 16000.0f);

        // Bandlimited saw - more harmonics for richer sound
        float saw = 0.0f;
        for (int h = 1; h <= 16; h++) {  // 16 harmonics
            float rolloff = 1.0f - (h - 1) * 0.05f;  // Gentler rolloff
            if (rolloff < 0.1f) rolloff = 0.1f;
            saw += sinf(h * angle) / h * rolloff;
        }
        wt_saw[i] = (int16_t)(saw * 10000.0f);

        // Bandlimited square - more odd harmonics
        float sq = 0.0f;
        for (int h = 1; h <= 15; h += 2) {  // 8 odd harmonics
            float rolloff = 1.0f - (h - 1) * 0.04f;
            if (rolloff < 0.1f) rolloff = 0.1f;
            sq += sinf(h * angle) / h * rolloff;
        }
        wt_square[i] = (int16_t)(sq * 10000.0f);

        // Triangle - very few harmonics (naturally bandlimited)
        wt_triangle[i] = (int16_t)((t < 0.5f ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t)) * 16000.0f);
    }

    // Pre-compute phase increments
    for (int oct = -1; oct <= 1; oct++) {
        float mult = powf(2.0f, (float)oct);
        for (int n = 0; n < NUM_BUTTONS; n++) {
            float freq = note_freqs[n] * mult;
            phase_inc[oct + 1][n] = (uint32_t)((freq * TABLE_SIZE * 65536.0f) / SAMPLE_RATE_AUDIO);
        }
    }
}

// Audio state (static for Core 1)
static uint32_t audio_phases[NUM_BUTTONS] = {0};
static int32_t audio_envelopes[NUM_BUTTONS] = {0};
static uint8_t audio_env_stage[NUM_BUTTONS] = {0};
static int32_t audio_voice_detune[NUM_BUTTONS] = {0};
static uint32_t audio_rng = 12345;

// Per-voice filter states (for analog-style warmth)
static int32_t voice_filter_state[NUM_BUTTONS] = {0};
static int32_t voice_filter_coef[NUM_BUTTONS] = {0};  // Per-voice cutoff variation


// Helper: update voice detuning based on params
static void update_detuning(const SynthParams* p) {
    for (int n = 0; n < NUM_BUTTONS; n++) {
        int32_t detune_val = 0;
        if (p->detune_amount > 0) {
            switch (p->detune_mode) {
                case 1:  // Alternating: 0, +1, -1, +2, -2...
                    {
                        int spread = (n + 1) / 2;
                        if (n % 2 == 0) spread = -spread;
                        detune_val = spread * p->detune_amount / 2;
                    }
                    break;
                case 2:  // Spread: linear from -max to +max
                    detune_val = ((n - NUM_BUTTONS/2) * (int)p->detune_amount) / (NUM_BUTTONS/2);
                    break;
                case 3:  // Random: different each recalc
                    audio_rng = audio_rng * 1103515245 + 12345;
                    detune_val = (int32_t)(audio_rng % p->detune_amount) - (p->detune_amount / 2);
                    break;
                default:  // 0 = off
                    detune_val = 0;
                    break;
            }
        }
        audio_voice_detune[n] = detune_val;
    }
}

// Saturation helper
static inline int32_t saturate(int32_t x, int32_t thresh, int32_t curve) {
    if (x > thresh) {
        int32_t excess = x - thresh;
        return thresh + (excess * curve) / (curve + excess);
    } else if (x < -thresh) {
        int32_t excess = -x - thresh;
        return -thresh - (excess * curve) / (curve + excess);
    }
    return x;
}

static void audio_core_main(void) {
    printf("[Core 1] Audio starting...\n");

    // Generate wavetables
    generate_wavetables();

    // Master filter state
    int32_t filter_state_l = 0;
    int32_t filter_state_r = 0;

    // LFO state
    uint32_t lfo_phase = 0;
    int32_t lfo_value = 0;

    // Previous button state
    uint32_t prev_pressed = 0;

    // Current params (cached, updated when dirty)
    SynthParams p = shared_get_params();

    // Envelope max
    const int32_t ENV_MAX = 256;

    // Audio buffer (static to reduce stack usage)
    static int16_t buffer[BUFFER_SIZE * 2];

    // Initial detuning setup
    update_detuning(&p);

    // Signal ready
    shared_set_ready(true);
    printf("[Core 1] Ready (preset: %s)\n", PRESET_NAMES[shared_get_preset()]);

    while (shared_is_audio_running()) {
        // Check for param changes
        if (shared_params_changed()) {
            p = shared_get_params();
            update_detuning(&p);
            printf("[Audio] Preset: %s\n", PRESET_NAMES[shared_get_preset()]);
        }

        // Read shared state
        uint32_t pressed = shared_get_pressed();
        uint8_t volume = shared_get_volume();
        uint8_t vibrato = shared_get_vibrato();
        uint8_t filter_pct = shared_get_filter();
        WaveformType waveform = shared_get_waveform();
        int8_t octave = shared_get_octave();

        // Select wavetable
        int16_t* wavetable;
        switch (waveform) {
            case WAVE_SAW:      wavetable = wt_saw; break;
            case WAVE_SQUARE:   wavetable = wt_square; break;
            case WAVE_TRIANGLE: wavetable = wt_triangle; break;
            default:            wavetable = wt_sine; break;
        }

        // Get phase increments for current octave
        uint32_t* ph_inc = phase_inc[octave + 1];

        // Process note on/off triggers
        for (int n = 0; n < NUM_BUTTONS; n++) {
            bool note_on = (pressed >> n) & 1;
            bool was_on = (prev_pressed >> n) & 1;

            if (note_on && !was_on) {
                // Note on - start attack
                audio_env_stage[n] = 1;
                // Phase initialization based on preset setting (tunable via /tune)
                if (p.phase_mode == 1) {
                    // Random - each note starts at random phase
                    audio_rng = audio_rng * 1103515245 + 12345;
                    audio_phases[n] = audio_rng;
                } else if (p.phase_mode == 2) {
                    // Spread - phases distributed across waveform
                    audio_phases[n] = (n * 0xFFFFFFFFU) / NUM_BUTTONS;
                } else {
                    // Sync (default) - all start at 0
                    audio_phases[n] = 0;
                }
            } else if (!note_on && was_on) {
                // Note off - start release
                audio_env_stage[n] = 4;
            }
        }
        prev_pressed = pressed;

        // Filter coefficient - subtle hall effect
        // Map filter_pct (0-100) to coef (80-255) for gentle darkening
        // 0% = darker (coef=80), 100% = fully open (coef=255)
        int32_t filter_coef = 80 + (filter_pct * 175) / 100;

        // Volume multiplier (0 = silence)
        int32_t vol_pct = volume;

        // LFO for vibrato - speed increases with vibrato amount (like real vibrato)
        // Base rate ~5Hz, increases to ~8Hz at max vibrato
        uint32_t lfo_inc = 3000 + (vibrato * 30);  // Faster LFO at 44100Hz

        // Generate audio
        for (int i = 0; i < BUFFER_SIZE; i++) {
            int32_t mix_l = 0;
            int32_t mix_r = 0;
            int voice_count = 0;

            // Update LFO per sample
            lfo_phase += lfo_inc;
            int lfo_idx = (lfo_phase >> 16) & 255;
            // Scale index to cover full wavetable (256 positions -> 2048 table)
            lfo_value = wt_sine[(lfo_idx * 8) & TABLE_MASK] >> 8;

            // Sum all active voices
            for (int n = 0; n < NUM_BUTTONS; n++) {
                // Skip completely idle voices
                if (audio_env_stage[n] == 0) {
                    audio_phases[n] += ph_inc[n];
                    continue;
                }

                // Update envelope per sample using tunable rates
                if (audio_env_stage[n] == 1) {  // Attack
                    audio_envelopes[n] += p.attack_rate;
                    if (audio_envelopes[n] >= ENV_MAX) {
                        audio_envelopes[n] = ENV_MAX;
                        audio_env_stage[n] = 2;
                    }
                } else if (audio_env_stage[n] == 2) {  // Decay
                    if (audio_envelopes[n] > p.sustain_level) {
                        audio_envelopes[n] -= p.decay_rate;
                    } else {
                        audio_envelopes[n] = p.sustain_level;
                        audio_env_stage[n] = 3;
                    }
                } else if (audio_env_stage[n] == 4) {  // Release
                    audio_envelopes[n] -= p.release_rate;
                    if (audio_envelopes[n] <= 0) {
                        audio_envelopes[n] = 0;
                        audio_env_stage[n] = 0;
                        audio_phases[n] += ph_inc[n];
                        continue;
                    }
                }

                voice_count++;

                // Wavetable lookup with higher precision linear interpolation
                uint32_t phase = audio_phases[n];
                int idx = (phase >> 16) & TABLE_MASK;
                // Use 12-bit fraction for smoother interpolation
                int frac = (phase >> 4) & 0xFFF;

                int16_t s0 = wavetable[idx];
                int16_t s1 = wavetable[(idx + 1) & TABLE_MASK];
                int32_t sample = s0 + (((s1 - s0) * frac) >> 12);

                // Apply envelope
                sample = (sample * audio_envelopes[n]) >> 8;

                // Vibrato - affects both pitch (frequency) and amplitude
                if (vibrato > 0) {
                    // Amplitude modulation: reduce amplitude slightly with LFO
                    // lfo_value ranges roughly -128 to +128
                    // Scale so max vibrato gives ~20% amplitude variation
                    int32_t amp_mod = 256 - ((lfo_value * vibrato) >> 9);  // 256 = no change
                    if (amp_mod < 200) amp_mod = 200;  // Limit to max 22% reduction
                    if (amp_mod > 312) amp_mod = 312;  // And max 22% increase
                    sample = (sample * amp_mod) >> 8;
                }

                // Advance phase
                uint32_t inc = ph_inc[n];

                // Pitch modulation (classic vibrato - pitch wobble)
                if (vibrato > 0) {
                    // Stronger pitch modulation - up to ~1 semitone at max
                    int32_t lfo_scaled = (lfo_value * (int32_t)vibrato) >> 5;
                    int32_t vib_mod = (lfo_scaled * (int32_t)(ph_inc[n] >> 6)) >> 6;
                    inc += vib_mod;
                }
                audio_phases[n] += inc;

                // Add to mix (mono - same signal to both channels)
                // Stereo panning disabled for now to keep things simple
                mix_l += sample;
                mix_r += sample;
            }

            // SIMPLE MIXING: divide by voice count using reciprocal multiply (faster than division)
            // reciprocal[n] = 65536 / n, so (x * reciprocal[n]) >> 16 ≈ x / n
            static const uint16_t recip[13] = {0, 65535, 32768, 21845, 16384, 13107, 10923, 9362, 8192, 7282, 6554, 5958, 5461};
            if (voice_count > 0 && voice_count < 13) {
                mix_l = (mix_l * recip[voice_count]) >> 16;
                mix_r = (mix_r * recip[voice_count]) >> 16;
            }

            // Apply volume (use shift approximation: vol/100 ≈ vol*167>>14 for vol 0-100)
            // Actually keep simple multiply - it's clearer and compiler optimizes well
            mix_l = (mix_l * vol_pct) / 100;
            mix_r = (mix_r * vol_pct) / 100;

            // One-pole lowpass filter (hall effect)
            // filter_coef: 0=very dark (heavy filtering), 255=bright (bypass)
            // Formula: output = state + coef/256 * (input - state)
            if (filter_coef < 255) {
                filter_state_l += ((mix_l - filter_state_l) * filter_coef) >> 8;
                filter_state_r += ((mix_r - filter_state_r) * filter_coef) >> 8;
                mix_l = filter_state_l;
                mix_r = filter_state_r;
            }

            // Hard clip only (no soft saturation, no dithering)
            if (mix_l > 32000) mix_l = 32000;
            else if (mix_l < -32000) mix_l = -32000;
            if (mix_r > 32000) mix_r = 32000;
            else if (mix_r < -32000) mix_r = -32000;

            // Output stereo
            buffer[i * 2] = (int16_t)mix_l;
            buffer[i * 2 + 1] = (int16_t)mix_r;
        }

        // Send to audio driver
        audio_write_buffer(buffer);
    }

    printf("[Core 1] Stopped\n");
}

// ============================================================
// BUTTON HANDLING
// ============================================================

static uint32_t last_wf_state = 1, last_oct_state = 1;
static uint32_t last_wf_time = 0, last_oct_time = 0;

static void buttons_init(void) {
    // Initialize note buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_init(BUTTON_PINS[i]);
        gpio_set_dir(BUTTON_PINS[i], GPIO_IN);
        gpio_pull_up(BUTTON_PINS[i]);
    }
    // Initialize waveform/octave buttons
    gpio_init(WAVEFORM_BUTTON_PIN);
    gpio_init(OCTAVE_BUTTON_PIN);
    gpio_set_dir(WAVEFORM_BUTTON_PIN, GPIO_IN);
    gpio_set_dir(OCTAVE_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(WAVEFORM_BUTTON_PIN);
    gpio_pull_up(OCTAVE_BUTTON_PIN);

    printf("[Buttons] Initialized: %d note buttons, waveform=GP%d, octave=GP%d\n",
           NUM_BUTTONS, WAVEFORM_BUTTON_PIN, OCTAVE_BUTTON_PIN);
}

static uint32_t read_buttons(void) {
    uint32_t mask = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!gpio_get(BUTTON_PINS[i])) mask |= (1u << i);
    }
    return mask;
}

static int popcount(uint32_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

// ============================================================
// MAIN
// ============================================================

static Synth synth;

int main(void) {
    // VERY FIRST THING - blink LED to prove code runs
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    for (volatile int i = 0; i < 5; i++) {
        gpio_put(25, 1);
        sleep_ms(300);
        gpio_put(25, 0);
        sleep_ms(300);
    }

    stdio_init_all();
    sleep_ms(500);

    printf("\n=== PICO SYNTHESIZER ===\n");
    printf("Core 0: UI | Core 1: Audio\n\n");

    // Initialize
    shared_state_init();
    adc_init();

    Potentiometer volume_pot, vibrato_pot, filter_pot;
    pot_init(&volume_pot, VOLUME_POTI_PIN);
    pot_init(&vibrato_pot, VIBRATO_POTI_PIN);
    pot_init(&filter_pot, FILTER_POTI_PIN);

    buttons_init();
    DISPLAY_CALL(synth_display_init());
    DISPLAY_CALL(synth_display_show_ready(false));  // Show "LOADING..."
    synth_init(&synth);
    audio_init(&synth);

    // Initial state
    uint8_t vol = pot_read_percent(&volume_pot);
    uint8_t vib = pot_read_percent_heavy(&vibrato_pot);
    uint8_t flt = pot_read_percent(&filter_pot);

    shared_set_volume(vol);
    shared_set_vibrato(vib);
    shared_set_filter(flt);
    shared_set_waveform(WAVE_SINE);
    shared_set_octave(0);

    DISPLAY_CALL(synth_display_update_volume(vol));
    DISPLAY_CALL(synth_display_update_vibrato(vib));
    DISPLAY_CALL(synth_display_update_filter(flt));
    DISPLAY_CALL(synth_display_update_notes(0));

    printf("Starting audio...\n");
    shared_start_audio();
    multicore_launch_core1(audio_core_main);

    // Wait for Core 1 to signal ready
    while (!shared_is_ready()) {
        sleep_ms(10);
    }
    DISPLAY_CALL(synth_display_show_ready(true));  // Show "READY"
    printf("Ready!\n");

    // Initialize WiFi server
    printf("Starting WiFi...\n");
    if (wifi_server_init()) {
        printf("WiFi ready! Connect to '%s' and open http://%s/\n",
               WIFI_SSID, wifi_server_get_ip());
    } else {
        printf("WiFi failed to start (synth will still work)\n");
    }

    // Main loop
    uint32_t last_display = 0;
    uint32_t last_debug = 0;
    uint8_t last_vol = vol, last_vib = vib, last_flt = flt;
    int last_notes = 0;
    int loop_count = 0;

    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Read buttons (fast)
        uint32_t pressed = read_buttons();
        shared_set_pressed(pressed);

        // Slower updates
        loop_count++;
        if (loop_count >= POTI_UPDATE_INTERVAL) {
            loop_count = 0;

            // Potentiometers
            vol = pot_read_percent(&volume_pot);
            vib = pot_read_percent_heavy(&vibrato_pot);
            // Filter pot is wired in reverse - invert the reading
            flt = 100 - pot_read_percent(&filter_pot);

            // Apply web overrides if enabled
            if (shared_get_web_vibrato_override()) {
                vib = shared_get_web_vibrato();
            }
            if (shared_get_web_filter_override()) {
                flt = shared_get_web_filter();
            }

            shared_set_volume(vol);
            shared_set_vibrato(vib);
            shared_set_filter(flt);

            // Control buttons (active LOW - pressed = 0)
            uint32_t wf_btn = gpio_get(WAVEFORM_BUTTON_PIN);
            if (wf_btn == 0 && last_wf_state == 1 && now - last_wf_time > 200) {
                WaveformType wf = shared_cycle_waveform();
                DISPLAY_CALL(synth_display_set_waveform(wf));
                last_wf_time = now;
                printf("[BTN] Waveform -> %d\n", wf);
            }
            last_wf_state = wf_btn;

            uint32_t oct_btn = gpio_get(OCTAVE_BUTTON_PIN);
            if (oct_btn == 0 && last_oct_state == 1 && now - last_oct_time > 200) {
                int8_t oct = shared_cycle_octave();
                DISPLAY_CALL(synth_display_set_octave(oct));
                last_oct_time = now;
                printf("[BTN] Octave -> %d\n", oct);
            }
            last_oct_state = oct_btn;

            // Display updates (throttled)
            if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
                int notes = popcount(pressed);

                if (abs(vol - last_vol) > 2) {
                    DISPLAY_CALL(synth_display_update_volume(vol));
                    last_vol = vol;
                }
                if (abs(vib - last_vib) > 2) {
                    DISPLAY_CALL(synth_display_update_vibrato(vib));
                    last_vib = vib;
                }
                if (abs(flt - last_flt) > 2) {
                    DISPLAY_CALL(synth_display_update_filter(flt));
                    last_flt = flt;
                }
                if (notes != last_notes) {
                    DISPLAY_CALL(synth_display_update_notes(notes));
                    last_notes = notes;
                }

                last_display = now;
            }
        }

        // Poll WiFi stack
        wifi_server_poll();

        // Debug: print states every 2 seconds
        if (now - last_debug >= 2000) {
            // Read raw ADC values for ALL pots to diagnose filter issue
            adc_select_input(0);  // Vibrato (GP26)
            sleep_us(50);
            uint16_t vib_raw = adc_read();

            adc_select_input(1);  // Volume (GP27)
            sleep_us(50);
            uint16_t vol_raw = adc_read();

            adc_select_input(2);  // Filter (GP28)
            sleep_us(50);
            uint16_t flt_raw = adc_read();

            printf("[POT] Vol:%d(raw%d) Vib:%d(raw%d) Flt:%d(raw%d)\n",
                   vol, vol_raw, vib, vib_raw, flt, flt_raw);
            last_debug = now;
        }

        sleep_us(50);
    }

    return 0;
}
