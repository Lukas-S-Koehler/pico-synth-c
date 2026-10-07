#include "synth.h"
#include <string.h>
#include <stdlib.h>

// ============================================================
// LOOKUP TABLES FOR EFFICIENCY
// ============================================================
static float midi_freq_table[128];
static float exp_table[256];  // For fast exponential curves

// Pre-compute tables at startup
static void init_lookup_tables(void) {
    // MIDI note to frequency
    for (int i = 0; i < 128; i++) {
        midi_freq_table[i] = 440.0f * powf(2.0f, (i - 69) / 12.0f);
    }
    
    // Exponential curve table (for smooth envelope curves)
    for (int i = 0; i < 256; i++) {
        exp_table[i] = (expf((float)i / 256.0f * 5.0f) - 1.0f) / (expf(5.0f) - 1.0f);
    }
}

float midi_to_frequency(uint8_t note) {
    if (note < 128) return midi_freq_table[note];
    return 440.0f;
}

float cents_to_ratio(float cents) {
    return powf(2.0f, cents / 1200.0f);
}

// ============================================================
// WAVETABLE GENERATION - THE KEY TO GOOD SOUND
// ============================================================

// Generate bandlimited sawtooth using additive synthesis
// This prevents aliasing - the #1 cause of harsh digital sound!
static void generate_bandlimited_saw(int16_t* table, int num_harmonics) {
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float sample = 0.0f;
        float phase = (float)i / WAVETABLE_SIZE * 2.0f * M_PI;
        
        for (int h = 1; h <= num_harmonics; h++) {
            // Gibbs phenomenon reduction with Lanczos sigma
            float sigma = sinf(M_PI * h / (num_harmonics + 1)) / (M_PI * h / (num_harmonics + 1));
            sample += sigma * sinf(h * phase) / h;
        }
        
        // Normalize and convert to 16-bit
        table[i] = (int16_t)(sample * 0.6f * 32767.0f);
    }
}

// Generate bandlimited square wave
static void generate_bandlimited_square(int16_t* table, int num_harmonics) {
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float sample = 0.0f;
        float phase = (float)i / WAVETABLE_SIZE * 2.0f * M_PI;
        
        // Square wave only has odd harmonics
        for (int h = 1; h <= num_harmonics; h += 2) {
            float sigma = sinf(M_PI * h / (num_harmonics + 1)) / (M_PI * h / (num_harmonics + 1));
            sample += sigma * sinf(h * phase) / h;
        }
        
        table[i] = (int16_t)(sample * 0.8f * 32767.0f);
    }
}

void synth_init_wavetables(Synth* synth) {
    // Sine - already bandlimited (pure tone)
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float phase = (float)i / WAVETABLE_SIZE * 2.0f * M_PI;
        synth->wavetable_sine[i] = (int16_t)(sinf(phase) * 32767.0f);
    }
    
    // Triangle - bandlimited (has few harmonics anyway)
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float sample = 0.0f;
        float phase = (float)i / WAVETABLE_SIZE * 2.0f * M_PI;
        for (int h = 1; h <= 15; h += 2) {
            float sign = ((h - 1) / 2 % 2 == 0) ? 1.0f : -1.0f;
            sample += sign * sinf(h * phase) / (h * h);
        }
        synth->wavetable_triangle[i] = (int16_t)(sample * 0.8f * 32767.0f);
    }
}

// Generate multiple wavetables for different octaves
// Higher notes need fewer harmonics to avoid aliasing
void synth_init_bandlimited_wavetables(Synth* synth) {
    // Octave 0 (lowest): many harmonics
    // Octave 7 (highest): few harmonics
    int harmonics_per_octave[] = {256, 128, 64, 32, 16, 8, 4, 2};
    
    for (int oct = 0; oct < 8; oct++) {
        generate_bandlimited_saw(synth->wavetable_saw_bl[oct], harmonics_per_octave[oct]);
        generate_bandlimited_square(synth->wavetable_square_bl[oct], harmonics_per_octave[oct]);
    }
}

// ============================================================
// SYNTH INITIALIZATION
// ============================================================
void synth_init(Synth* synth) {
    memset(synth, 0, sizeof(Synth));
    
    init_lookup_tables();
    synth_init_wavetables(synth);
    synth_init_bandlimited_wavetables(synth);
    
    synth->noise_state = 0x12345678;
    
    // Default patch - warm pad
    synth->patch = (SynthPatch){
        .osc1_type = OSC_SAW,
        .osc2_type = OSC_SAW,
        .osc1_level = 0.5f,
        .osc2_level = 0.3f,
        .osc2_detune = 0.0f,
        .osc2_fine = 7.0f,  // Slight detune for warmth
        
        .amp_attack = 0.01f,
        .amp_decay = 0.3f,
        .amp_sustain = 0.7f,
        .amp_release = 0.5f,
        
        .filter_type = FILTER_LOWPASS,
        .filter_cutoff = 2000.0f,
        .filter_resonance = 0.3f,
        .filter_drive = 1.0f,
        
        .filter_attack = 0.01f,
        .filter_decay = 0.5f,
        .filter_sustain = 0.3f,
        .filter_release = 0.5f,
        .filter_env_amount = 3000.0f,
        
        .lfo_rate = 0.5f,
        .lfo_pitch_depth = 0.0f,
        .lfo_filter_depth = 500.0f,
        
        .master_volume = 0.7f,
        .glide_time = 0.0f
    };
    
    // Initialize LFO
    lfo_init(&synth->lfo);
}

// ============================================================
// OSCILLATOR RENDERING WITH ANTI-ALIASING
// ============================================================

// Select appropriate wavetable based on frequency (key anti-aliasing technique!)
static int get_octave_for_frequency(float freq) {
    // Map frequency to octave (C0 = ~16Hz is octave 0)
    int octave = (int)(log2f(freq / 16.0f));
    if (octave < 0) octave = 0;
    if (octave > 7) octave = 7;
    return octave;
}

// Fast pseudo-random for noise
uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

float synth_render_oscillator(Synth* synth, Oscillator* osc, float frequency, uint8_t note) {
    // Calculate phase increment
    float freq_with_detune = frequency * cents_to_ratio(osc->detune);
    osc->phase_increment = FLOAT_TO_FIXED(freq_with_detune * WAVETABLE_SIZE / SAMPLE_RATE);
    
    float sample = 0.0f;
    int octave = get_octave_for_frequency(freq_with_detune);
    
    // Phase position for wavetable lookup
    uint32_t phase_int = (osc->phase >> FIXED_SHIFT) & WAVETABLE_MASK;
    float phase_frac = FIXED_TO_FLOAT(osc->phase & (FIXED_ONE - 1));
    
    // Linear interpolation between samples (smoother sound)
    #define INTERP(table) \
        ((table[phase_int] * (1.0f - phase_frac) + \
          table[(phase_int + 1) & WAVETABLE_MASK] * phase_frac) / 32767.0f)
    
    switch (osc->type) {
        case OSC_SINE:
            sample = INTERP(synth->wavetable_sine);
            break;
            
        case OSC_SAW:
            // Use bandlimited version based on octave
            sample = INTERP(synth->wavetable_saw_bl[octave]);
            break;
            
        case OSC_SQUARE:
            sample = INTERP(synth->wavetable_square_bl[octave]);
            break;
            
        case OSC_TRIANGLE:
            sample = INTERP(synth->wavetable_triangle);
            break;
            
        case OSC_NOISE:
            // White noise (good for drums/FX)
            sample = ((float)xorshift32(&synth->noise_state) / (float)UINT32_MAX) * 2.0f - 1.0f;
            break;
            
        case OSC_SUPERSAW:
            // 7 detuned saw oscillators for huge sound
            {
                float spread = osc->spread * 0.5f;  // Max +/- 50 cents spread
                float detunes[] = {-spread, -spread*0.66f, -spread*0.33f, 0, 
                                   spread*0.33f, spread*0.66f, spread};
                
                for (int i = 0; i < 7; i++) {
                    float f = freq_with_detune * cents_to_ratio(detunes[i] * 100.0f);
                    uint32_t ph = (osc->phases[i] >> FIXED_SHIFT) & WAVETABLE_MASK;
                    sample += synth->wavetable_saw_bl[octave][ph] / 32767.0f / 5.0f;
                    osc->phases[i] += FLOAT_TO_FIXED(f * WAVETABLE_SIZE / SAMPLE_RATE);
                }
            }
            break;
            
        default:
            break;
    }
    
    // Advance phase
    osc->phase += osc->phase_increment;
    
    return sample * osc->mix;
}

// ============================================================
// ENVELOPE - MUSICAL CURVES
// ============================================================

// Convert time in seconds to rate (samples)
static float time_to_rate(float time_seconds) {
    if (time_seconds < 0.001f) return 1.0f;  // Instant
    return 1.0f / (time_seconds * SAMPLE_RATE);
}

void envelope_trigger(Envelope* env) {
    env->stage = ENV_ATTACK;
    // Don't reset value to avoid clicks if retriggering
}

void envelope_release(Envelope* env) {
    if (env->stage != ENV_IDLE) {
        env->stage = ENV_RELEASE;
    }
}

float envelope_process(Envelope* env) {
    switch (env->stage) {
        case ENV_ATTACK:
            // Exponential attack (more musical than linear)
            env->value += (1.1f - env->value) * env->attack_rate;
            if (env->value >= 1.0f) {
                env->value = 1.0f;
                env->stage = ENV_DECAY;
            }
            break;
            
        case ENV_DECAY:
            // Exponential decay toward sustain
            env->value += (env->sustain_level - env->value) * env->decay_rate;
            if (fabsf(env->value - env->sustain_level) < 0.001f) {
                env->value = env->sustain_level;
                env->stage = ENV_SUSTAIN;
            }
            break;
            
        case ENV_SUSTAIN:
            // Hold at sustain level
            env->value = env->sustain_level;
            break;
            
        case ENV_RELEASE:
            // Exponential release to zero
            env->value *= (1.0f - env->release_rate);
            if (env->value < 0.001f) {
                env->value = 0.0f;
                env->stage = ENV_IDLE;
            }
            break;
            
        case ENV_IDLE:
        default:
            env->value = 0.0f;
            break;
    }
    
    return env->value;
}

// ============================================================
// FILTER - STATE VARIABLE (MUSICAL & VERSATILE)
// ============================================================

void filter_init(Filter* filter) {
    filter->low = 0.0f;
    filter->band = 0.0f;
    filter->high = 0.0f;
    filter->notch = 0.0f;
    filter->cutoff_smooth = filter->cutoff;
    filter->resonance_smooth = filter->resonance;
}

// Soft clipping for musical saturation
float soft_clip(float x) {
    if (x > 1.0f) return 1.0f - expf(1.0f - x);
    if (x < -1.0f) return -1.0f + expf(-1.0f - x);
    return x;
}

// Fast tanh approximation
float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float filter_process(Filter* filter, float input) {
    // Smooth parameter changes (prevents clicks!)
    filter->cutoff_smooth += (filter->cutoff - filter->cutoff_smooth) * 0.001f;
    filter->resonance_smooth += (filter->resonance - filter->resonance_smooth) * 0.001f;
    
    // Calculate filter coefficients
    float f = 2.0f * sinf(M_PI * filter->cutoff_smooth / SAMPLE_RATE);
    f = fminf(f, 0.99f);  // Stability limit
    
    float q = 1.0f - filter->resonance_smooth;
    q = fmaxf(q, 0.05f);  // Prevent self-oscillation explosion
    
    // Apply soft saturation to input (adds warmth)
    if (filter->drive > 1.0f) {
        input = fast_tanh(input * filter->drive) / filter->drive;
    }
    
    // Two-pass for better response (oversampled filter)
    for (int i = 0; i < 2; i++) {
        filter->low += f * filter->band;
        filter->high = input - filter->low - q * filter->band;
        filter->band += f * filter->high;
        filter->notch = filter->high + filter->low;
        
        // Soft clip internal states (prevents blowup with high resonance)
        filter->band = soft_clip(filter->band);
    }
    
    switch (filter->type) {
        case FILTER_LOWPASS:  return filter->low;
        case FILTER_HIGHPASS: return filter->high;
        case FILTER_BANDPASS: return filter->band;
        case FILTER_NOTCH:    return filter->notch;
        default:              return filter->low;
    }
}

// ============================================================
// LFO
// ============================================================

void lfo_init(LFO* lfo) {
    lfo->phase = 0.0f;
    lfo->value = 0.0f;
}

float lfo_process(LFO* lfo) {
    float phase_inc = lfo->rate / SAMPLE_RATE;
    lfo->phase += phase_inc;
    if (lfo->phase >= 1.0f) lfo->phase -= 1.0f;
    
    switch (lfo->type) {
        case OSC_SINE:
            lfo->value = sinf(lfo->phase * 2.0f * M_PI);
            break;
        case OSC_SAW:
            lfo->value = 2.0f * lfo->phase - 1.0f;
            break;
        case OSC_SQUARE:
            lfo->value = (lfo->phase < 0.5f) ? 1.0f : -1.0f;
            break;
        case OSC_TRIANGLE:
            lfo->value = 4.0f * fabsf(lfo->phase - 0.5f) - 1.0f;
            break;
        default:
            lfo->value = sinf(lfo->phase * 2.0f * M_PI);
            break;
    }
    
    return lfo->value * lfo->depth;
}

// ============================================================
// VOICE RENDERING
// ============================================================

static void voice_init_from_patch(Voice* voice, const SynthPatch* patch, uint8_t note, uint8_t velocity) {
    voice->active = true;
    voice->note = note;
    voice->velocity = velocity / 127.0f;
    
    // Oscillator 1
    voice->osc1.type = patch->osc1_type;
    voice->osc1.phase = 0;
    voice->osc1.detune = 0.0f;
    voice->osc1.mix = patch->osc1_level;
    voice->osc1.spread = 0.5f;
    
    // Oscillator 2
    voice->osc2.type = patch->osc2_type;
    voice->osc2.phase = 0;
    voice->osc2.detune = patch->osc2_detune * 100.0f + patch->osc2_fine;
    voice->osc2.mix = patch->osc2_level;
    voice->osc2.spread = 0.5f;
    
    // Amp envelope
    voice->amp_env.stage = ENV_IDLE;
    voice->amp_env.value = 0.0f;
    voice->amp_env.attack_rate = time_to_rate(patch->amp_attack);
    voice->amp_env.decay_rate = time_to_rate(patch->amp_decay) * 3.0f;
    voice->amp_env.sustain_level = patch->amp_sustain;
    voice->amp_env.release_rate = time_to_rate(patch->amp_release) * 3.0f;
    
    // Filter envelope
    voice->filter_env.stage = ENV_IDLE;
    voice->filter_env.value = 0.0f;
    voice->filter_env.attack_rate = time_to_rate(patch->filter_attack);
    voice->filter_env.decay_rate = time_to_rate(patch->filter_decay) * 3.0f;
    voice->filter_env.sustain_level = patch->filter_sustain;
    voice->filter_env.release_rate = time_to_rate(patch->filter_release) * 3.0f;
    
    voice->filter_env_amount = patch->filter_env_amount;
    
    // Filter
    voice->filter.type = patch->filter_type;
    voice->filter.cutoff = patch->filter_cutoff;
    voice->filter.resonance = patch->filter_resonance;
    voice->filter.drive = patch->filter_drive;
    filter_init(&voice->filter);
    
    // Frequency
    voice->target_frequency = midi_to_frequency(note);
    voice->current_frequency = voice->target_frequency;
    voice->glide_rate = 1.0f - powf(0.5f, 1.0f / (patch->glide_time * SAMPLE_RATE + 1.0f));
    
    // Trigger envelopes
    envelope_trigger(&voice->amp_env);
    envelope_trigger(&voice->filter_env);
}

float synth_render_voice(Synth* synth, Voice* voice) {
    if (!voice->active) return 0.0f;
    
    // Glide/portamento
    voice->current_frequency += (voice->target_frequency - voice->current_frequency) * voice->glide_rate;
    
    // LFO modulation
    float lfo_value = synth->lfo.value;
    float pitch_mod = 1.0f + lfo_value * synth->patch.lfo_pitch_depth * 0.01f;
    float freq = voice->current_frequency * pitch_mod;
    
    // Render oscillators
    float osc_out = 0.0f;
    osc_out += synth_render_oscillator(synth, &voice->osc1, freq, voice->note);
    osc_out += synth_render_oscillator(synth, &voice->osc2, freq, voice->note);
    
    // Process envelopes
    float amp_env = envelope_process(&voice->amp_env);
    float filter_env = envelope_process(&voice->filter_env);
    
    // Apply filter with envelope and LFO modulation
    float filter_cutoff = synth->patch.filter_cutoff 
                        + filter_env * voice->filter_env_amount
                        + lfo_value * synth->patch.lfo_filter_depth;
    filter_cutoff = fmaxf(20.0f, fminf(filter_cutoff, SAMPLE_RATE * 0.4f));
    voice->filter.cutoff = filter_cutoff;
    
    float filtered = filter_process(&voice->filter, osc_out);
    
    // Apply amplitude envelope and velocity
    float output = filtered * amp_env * voice->velocity;
    
    // Check if voice should be freed
    if (voice->amp_env.stage == ENV_IDLE) {
        voice->active = false;
    }
    
    return output;
}

// ============================================================
// NOTE HANDLING
// ============================================================

// Find free voice or steal oldest
static Voice* find_voice(Synth* synth) {
    // First look for inactive voice
    for (int i = 0; i < NUM_VOICES; i++) {
        if (!synth->voices[i].active) {
            return &synth->voices[i];
        }
    }
    
    // Voice stealing: find voice in release stage
    for (int i = 0; i < NUM_VOICES; i++) {
        if (synth->voices[i].amp_env.stage == ENV_RELEASE) {
            return &synth->voices[i];
        }
    }
    
    // Steal first voice as last resort
    return &synth->voices[0];
}

void synth_note_on(Synth* synth, uint8_t note, uint8_t velocity) {
    if (velocity == 0) {
        synth_note_off(synth, note);
        return;
    }
    
    Voice* voice = find_voice(synth);
    voice_init_from_patch(voice, &synth->patch, note, velocity);
}

void synth_note_off(Synth* synth, uint8_t note) {
    for (int i = 0; i < NUM_VOICES; i++) {
        if (synth->voices[i].active && synth->voices[i].note == note) {
            envelope_release(&synth->voices[i].amp_env);
            envelope_release(&synth->voices[i].filter_env);
        }
    }
}

void synth_all_notes_off(Synth* synth) {
    for (int i = 0; i < NUM_VOICES; i++) {
        if (synth->voices[i].active) {
            envelope_release(&synth->voices[i].amp_env);
            envelope_release(&synth->voices[i].filter_env);
        }
    }
}

// ============================================================
// MAIN RENDER FUNCTION
// ============================================================

// Parameter smoothing helper
float smooth_parameter(float current, float target, float rate) {
    return current + (target - current) * rate;
}

void synth_render(Synth* synth, int16_t* buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        // Process LFO (once per sample)
        synth->lfo.rate = synth->patch.lfo_rate;
        synth->lfo.depth = 1.0f;  // Full depth, scaled per destination
        lfo_process(&synth->lfo);
        
        // Mix all voices
        float mix = 0.0f;
        for (int v = 0; v < NUM_VOICES; v++) {
            mix += synth_render_voice(synth, &synth->voices[v]);
        }
        
        // Master volume with soft clipping (prevents harsh digital clipping)
        mix *= synth->patch.master_volume;
        mix = soft_clip(mix);
        
        // Convert to 16-bit stereo (same signal both channels for mono speaker)
        int16_t sample = (int16_t)(mix * 32000.0f);  // Slight headroom
        buffer[i * 2] = sample;      // Left
        buffer[i * 2 + 1] = sample;  // Right
    }
}

// ============================================================
// PRESET PATCHES
// ============================================================

const SynthPatch PATCH_WARM_PAD = {
    .osc1_type = OSC_SAW,
    .osc2_type = OSC_SAW,
    .osc1_level = 0.4f,
    .osc2_level = 0.3f,
    .osc2_fine = 8.0f,
    .amp_attack = 0.3f,
    .amp_decay = 0.5f,
    .amp_sustain = 0.8f,
    .amp_release = 1.0f,
    .filter_type = FILTER_LOWPASS,
    .filter_cutoff = 1500.0f,
    .filter_resonance = 0.2f,
    .filter_attack = 0.5f,
    .filter_decay = 1.0f,
    .filter_sustain = 0.5f,
    .filter_release = 1.0f,
    .filter_env_amount = 2000.0f,
    .lfo_rate = 0.3f,
    .lfo_filter_depth = 300.0f,
    .master_volume = 0.6f
};

const SynthPatch PATCH_BASS = {
    .osc1_type = OSC_SAW,
    .osc2_type = OSC_SQUARE,
    .osc1_level = 0.6f,
    .osc2_level = 0.3f,
    .osc2_detune = -12.0f,  // One octave down
    .amp_attack = 0.005f,
    .amp_decay = 0.2f,
    .amp_sustain = 0.8f,
    .amp_release = 0.2f,
    .filter_type = FILTER_LOWPASS,
    .filter_cutoff = 500.0f,
    .filter_resonance = 0.4f,
    .filter_drive = 1.5f,
    .filter_attack = 0.01f,
    .filter_decay = 0.3f,
    .filter_sustain = 0.2f,
    .filter_release = 0.2f,
    .filter_env_amount = 3000.0f,
    .master_volume = 0.7f
};

const SynthPatch PATCH_LEAD = {
    .osc1_type = OSC_SUPERSAW,
    .osc2_type = OSC_SQUARE,
    .osc1_level = 0.5f,
    .osc2_level = 0.2f,
    .osc2_fine = 5.0f,
    .amp_attack = 0.01f,
    .amp_decay = 0.1f,
    .amp_sustain = 0.9f,
    .amp_release = 0.3f,
    .filter_type = FILTER_LOWPASS,
    .filter_cutoff = 3000.0f,
    .filter_resonance = 0.3f,
    .filter_attack = 0.01f,
    .filter_decay = 0.2f,
    .filter_sustain = 0.6f,
    .filter_release = 0.3f,
    .filter_env_amount = 2500.0f,
    .lfo_rate = 5.0f,
    .lfo_pitch_depth = 0.3f,  // Vibrato
    .master_volume = 0.6f,
    .glide_time = 0.05f
};

const SynthPatch PATCH_PLUCK = {
    .osc1_type = OSC_SAW,
    .osc2_type = OSC_TRIANGLE,
    .osc1_level = 0.5f,
    .osc2_level = 0.3f,
    .amp_attack = 0.001f,
    .amp_decay = 0.4f,
    .amp_sustain = 0.0f,
    .amp_release = 0.3f,
    .filter_type = FILTER_LOWPASS,
    .filter_cutoff = 800.0f,
    .filter_resonance = 0.5f,
    .filter_attack = 0.001f,
    .filter_decay = 0.2f,
    .filter_sustain = 0.0f,
    .filter_release = 0.2f,
    .filter_env_amount = 5000.0f,
    .master_volume = 0.7f
};
