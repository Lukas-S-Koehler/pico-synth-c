#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// Audio Configuration - Lower sample rate for reduced CPU load
#define SAMPLE_RATE 22050 
#define BUFFER_SIZE 256
#define NUM_VOICES 13   // Match MicroPython polyphony (13 notes C4-C5)
#define OVERSAMPLING 1  // No oversampling at lower sample rate

// Fixed-point math for efficiency (16.16 format)
typedef int32_t fixed_t;
#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)
#define FLOAT_TO_FIXED(x) ((fixed_t)((x) * FIXED_ONE))
#define FIXED_TO_FLOAT(x) ((float)(x) / FIXED_ONE)
#define FIXED_MUL(a, b) ((fixed_t)(((int64_t)(a) * (b)) >> FIXED_SHIFT))

// Wavetable size (power of 2 for efficient wrapping)
#define WAVETABLE_SIZE 2048 
#define WAVETABLE_MASK (WAVETABLE_SIZE - 1)

// ============================================================
// OSCILLATOR
// ============================================================
typedef enum {
    OSC_SINE,
    OSC_SAW,
    OSC_SQUARE,
    OSC_TRIANGLE,
    OSC_NOISE,
    OSC_SUPERSAW,
    OSC_NUM_TYPES
} OscillatorType;

typedef struct {
    OscillatorType type;
    fixed_t phase;           // Current phase (0 to WAVETABLE_SIZE << FIXED_SHIFT)
    fixed_t phase_increment; // Phase increment per sample
    float detune;            // Detune in cents
    float pulse_width;       // For square wave (0.0 to 1.0)
    float mix;               // Oscillator mix level
    
    // For supersaw
    float spread;            // Detune spread for supersaw
    fixed_t phases[7];       // Multiple phases for supersaw
} Oscillator;

// ============================================================
// ENVELOPE (ADSR)
// ============================================================
typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvelopeStage;

typedef struct {
    EnvelopeStage stage;
    float value;             // Current envelope value (0.0 to 1.0)
    float attack_rate;       // Rate of attack (per sample)
    float decay_rate;        // Rate of decay
    float sustain_level;     // Sustain level (0.0 to 1.0)
    float release_rate;      // Rate of release
    
    // Curve shaping (makes it sound more musical)
    float attack_curve;      // Exponential curve for attack
    float decay_curve;       // Exponential curve for decay
    float release_curve;     // Exponential curve for release
} Envelope;

// ============================================================
// FILTER (State Variable Filter - musical sounding)
// ============================================================
typedef enum {
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_NOTCH
} FilterType;

typedef struct {
    FilterType type;
    float cutoff;            // Cutoff frequency (Hz)
    float resonance;         // Resonance (0.0 to 1.0, careful above 0.9!)
    float drive;             // Soft saturation drive
    
    // Filter state
    float low;
    float band;
    float high;
    float notch;
    
    // Smoothed parameters (prevents clicks)
    float cutoff_smooth;
    float resonance_smooth;
} Filter;

// ============================================================
// LFO (Low Frequency Oscillator)
// ============================================================
typedef struct {
    OscillatorType type;
    float rate;              // Rate in Hz
    float depth;             // Modulation depth (0.0 to 1.0)
    float phase;             // Current phase
    float value;             // Current output value
    
    // Destinations
    float pitch_mod;         // How much to modulate pitch
    float filter_mod;        // How much to modulate filter
    float amp_mod;           // How much to modulate amplitude
} LFO;

// ============================================================
// VOICE (combines everything for polyphony)
// ============================================================
typedef struct {
    bool active;
    uint8_t note;            // MIDI note number
    float velocity;          // Note velocity (0.0 to 1.0)
    
    Oscillator osc1;
    Oscillator osc2;
    Envelope amp_env;
    Envelope filter_env;
    Filter filter;
    
    // Voice-specific filter envelope amount
    float filter_env_amount;
    
    // Glide/portamento
    float target_frequency;
    float current_frequency;
    float glide_rate;
} Voice;

// ============================================================
// SYNTH PATCH (all parameters)
// ============================================================
typedef struct {
    // Oscillators
    OscillatorType osc1_type;
    OscillatorType osc2_type;
    float osc1_level;
    float osc2_level;
    float osc2_detune;       // Semitones
    float osc2_fine;         // Cents
    
    // Amp Envelope
    float amp_attack;
    float amp_decay;
    float amp_sustain;
    float amp_release;
    
    // Filter
    FilterType filter_type;
    float filter_cutoff;
    float filter_resonance;
    float filter_drive;
    
    // Filter Envelope
    float filter_attack;
    float filter_decay;
    float filter_sustain;
    float filter_release;
    float filter_env_amount;
    
    // LFO
    float lfo_rate;
    float lfo_pitch_depth;
    float lfo_filter_depth;
    
    // Effects
    float chorus_depth;
    float chorus_rate;
    float reverb_mix;
    float reverb_decay;
    
    // Master
    float master_volume;
    float glide_time;
} SynthPatch;

// ============================================================
// MAIN SYNTH STATE
// ============================================================
typedef struct {
    Voice voices[NUM_VOICES];
    LFO lfo;
    SynthPatch patch;
    
    // Wavetables (pre-computed for efficiency)
    int16_t wavetable_sine[WAVETABLE_SIZE];
    int16_t wavetable_saw[WAVETABLE_SIZE];
    int16_t wavetable_square[WAVETABLE_SIZE];
    int16_t wavetable_triangle[WAVETABLE_SIZE];
    
    // Bandlimited wavetables for different octaves (anti-aliasing)
    int16_t wavetable_saw_bl[8][WAVETABLE_SIZE];    // 8 octaves
    int16_t wavetable_square_bl[8][WAVETABLE_SIZE];
    
    // Output buffer
    int16_t output_buffer[BUFFER_SIZE * 2];  // Stereo
    
    // Simple delay/chorus buffer
    int16_t delay_buffer[SAMPLE_RATE];  // 1 second max
    uint32_t delay_write_pos;
    
    // Random state for noise
    uint32_t noise_state;
} Synth;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

// Initialization
void synth_init(Synth* synth);
void synth_init_wavetables(Synth* synth);
void synth_init_bandlimited_wavetables(Synth* synth);
void synth_load_patch(Synth* synth, const SynthPatch* patch);

// Note handling
void synth_note_on(Synth* synth, uint8_t note, uint8_t velocity);
void synth_note_off(Synth* synth, uint8_t note);
void synth_all_notes_off(Synth* synth);

// Audio rendering
void synth_render(Synth* synth, int16_t* buffer, uint32_t num_samples);
float synth_render_voice(Synth* synth, Voice* voice);
float synth_render_oscillator(Synth* synth, Oscillator* osc, float frequency, uint8_t note);

// Envelope processing
void envelope_trigger(Envelope* env);
void envelope_release(Envelope* env);
float envelope_process(Envelope* env);

// Filter processing
void filter_init(Filter* filter);
float filter_process(Filter* filter, float input);
void filter_set_cutoff(Filter* filter, float cutoff);

// LFO processing
void lfo_init(LFO* lfo);
float lfo_process(LFO* lfo);

// Utility functions
float midi_to_frequency(uint8_t note);
float cents_to_ratio(float cents);
float soft_clip(float x);  // Musical soft clipping
float fast_tanh(float x);  // Fast approximation
uint32_t xorshift32(uint32_t* state);  // Fast random

// Parameter smoothing (prevents clicks)
float smooth_parameter(float current, float target, float rate);

// ============================================================
// AUDIO DRIVER (from audio_driver.c)
// ============================================================
void audio_init(Synth* synth);
void audio_update(void);
void audio_write_buffer(const int16_t* buffer);
void audio_stop(void);
uint32_t audio_get_sample_rate(void);
uint32_t audio_get_buffer_size(void);

// ============================================================
// PRESET PATCHES
// ============================================================
extern const SynthPatch PATCH_WARM_PAD;
extern const SynthPatch PATCH_BASS;
extern const SynthPatch PATCH_LEAD;
extern const SynthPatch PATCH_PLUCK;

#endif // SYNTH_H
