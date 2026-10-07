"""
audio_core.py - Dedicated Audio Core (Core 1)

This module runs entirely on Core 1 of the Pico, dedicated to:
- Real-time audio generation
- I2S output
- No display/UI work - just pure audio

Core 1 reads from shared_state and generates audio continuously.
This separation prevents UI updates from causing audio glitches.

Usage (called from main.py):
    import _thread
    from audio_core import audio_core_main
    _thread.start_new_thread(audio_core_main, ())
"""

import array
import math
from machine import Pin, I2S
from shared_state import reader, WAVEFORM_NAMES
from wavetables import (
    TABLE_SIZE, WAVEFORMS,
    SINE_LUT, SAW_LUT, SQUARE_LUT, TRIANGLE_LUT
)


# =============================================================================
# AUDIO CONFIGURATION
# =============================================================================

SAMPLE_RATE = 22050
BUFFER_SIZE = 256
ENVELOPE_MAX = 256

# ADSR stages
ENV_IDLE = 0
ENV_ATTACK = 1
ENV_DECAY = 2
ENV_SUSTAIN = 3
ENV_RELEASE = 4

# Envelope rates
ATTACK_RATE = 35
DECAY_RATE = 18
SUSTAIN_LEVEL = 180
RELEASE_RATE = 12

# Filter envelope
FILTER_ENV_ATTACK = 40
FILTER_ENV_DECAY = 8

# Note frequencies C4-C5
FREQUENCIES = [
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99,
    392.00, 415.30, 440.00, 466.16, 493.88, 523.25
]

# LFO
LFO_SIZE = 256
LFO_LUT = array.array('h', [
    int(math.sin(2 * math.pi * i / LFO_SIZE) * 127)
    for i in range(LFO_SIZE)
])


# =============================================================================
# PHASE STEP TABLES
# =============================================================================

def _make_steps(octave_shift=0):
    factor = 2.0 ** octave_shift
    return array.array('I', [
        int((f * factor * TABLE_SIZE * 256) / SAMPLE_RATE)
        for f in FREQUENCIES
    ])

STEPS_BY_OCTAVE = {
    -1: _make_steps(-1),
    0: _make_steps(0),
    1: _make_steps(1)
}


# =============================================================================
# VIPER AUDIO GENERATION
# =============================================================================

@micropython.viper
def _generate_viper(buf, lut, phases, steps, env_ptr,
                    lfo_lut, lfo_phase_ptr,
                    volume: int, vibrato: int, filter_cutoff: int,
                    resonance: int, filter_state_ptr):
    """Viper-optimized audio generation with SVF."""
    b = ptr16(buf)
    l = ptr16(lut)
    p = ptr32(phases)
    s = ptr32(steps)
    env = ptr16(env_ptr)
    lfo = ptr16(lfo_lut)
    lfo_p = ptr32(lfo_phase_ptr)
    filt = ptr32(filter_state_ptr)

    table_mask: int = 1023
    lfo_mask: int = 255
    phase_max: int = 262144

    vol: int = volume
    vib_depth: int = vibrato
    cutoff: int = filter_cutoff
    reso: int = resonance

    svf_low: int = filt[0]
    svf_band: int = filt[1]

    lfo_phase: int = lfo_p[0]
    lfo_step: int = 3

    i: int = 0
    while i < 256:
        sample: int = 0

        lfo_val: int = lfo[(lfo_phase >> 8) & lfo_mask]
        lfo_phase += lfo_step
        if lfo_phase >= 65536:
            lfo_phase -= 65536

        pitch_mod: int = 256
        if vib_depth > 0:
            pitch_mod = 256 + ((lfo_val * vib_depth) >> 9)

        # Process all 13 notes
        n: int = 0
        while n < 13:
            if env[n] > 0:
                ph: int = p[n]
                idx: int = (ph >> 8) & table_mask
                frac: int = ph & 255
                idx_next: int = (idx + 1) & table_mask
                s0: int = l[idx]
                s1: int = l[idx_next]
                interp: int = s0 + (((s1 - s0) * frac) >> 8)
                sample += (interp * env[n]) >> 8

            step: int = s[n]
            if vib_depth > 0:
                step = (step * pitch_mod) >> 8
            p[n] = p[n] + step
            if p[n] >= phase_max:
                p[n] -= phase_max
            n += 1

        sample = (sample * vol) >> 8

        # SVF filter
        f: int = cutoff >> 2
        if f < 4:
            f = 4
        q: int = 256 - reso
        if q < 20:
            q = 20

        svf_low = svf_low + ((f * svf_band) >> 8)
        high: int = sample - svf_low - ((q * svf_band) >> 8)
        svf_band = svf_band + ((f * high) >> 8)

        if svf_band > 30000:
            svf_band = 30000
        elif svf_band < -30000:
            svf_band = -30000

        sample = svf_low

        # Soft clipping
        if sample > 20000:
            excess: int = sample - 20000
            sample = 20000 + ((excess * 27) // (27 + ((excess * excess) >> 12)))
        elif sample < -20000:
            excess = -sample - 20000
            sample = -20000 - ((excess * 27) // (27 + ((excess * excess) >> 12)))

        if sample > 32000:
            sample = 32000
        elif sample < -32000:
            sample = -32000

        b[i] = sample
        i += 1

    filt[0] = svf_low
    filt[1] = svf_band
    lfo_p[0] = lfo_phase


# =============================================================================
# AUDIO CORE CLASS
# =============================================================================

class AudioCore:
    """
    Audio processing core - runs on Core 1.
    Reads shared state and generates audio continuously.
    """

    def __init__(self):
        # I2S Audio output
        self.i2s = I2S(
            0,
            sck=Pin(16),
            ws=Pin(17),
            sd=Pin(18),
            mode=I2S.TX,
            bits=16,
            format=I2S.MONO,
            rate=SAMPLE_RATE,
            ibuf=BUFFER_SIZE * 16
        )

        # Audio state
        self.phases = array.array('I', [0] * 13)
        self.buffer = array.array('h', [0] * BUFFER_SIZE)
        self.envelopes = array.array('H', [0] * 13)
        self.env_stages = array.array('B', [0] * 13)
        self.filter_envs = array.array('H', [0] * 13)
        self.filter_state = array.array('i', [0, 0])
        self.lfo_phase = array.array('I', [0])

        # Track previous state for edge detection
        self.prev_pressed = 0
        self.prev_waveform = 0
        self.prev_octave = 0

        # Current wavetable and steps
        self.current_lut = SINE_LUT
        self.current_steps = STEPS_BY_OCTAVE[0]

    def _update_wavetable(self, waveform_idx: int, octave: int):
        """Update wavetable when waveform or octave changes."""
        waveform_name = WAVEFORM_NAMES[waveform_idx]
        self.current_lut = WAVEFORMS[waveform_name][octave]
        self.current_steps = STEPS_BY_OCTAVE[octave]

    def _process_envelopes(self, pressed: int):
        """Process ADSR envelopes for all notes."""
        for note in range(13):
            mask = 1 << note
            note_on = pressed & mask
            was_on = self.prev_pressed & mask
            stage = self.env_stages[note]
            env_val = self.envelopes[note]
            filt_env = self.filter_envs[note]

            if note_on:
                if not was_on:
                    if env_val < 32:
                        self.phases[note] = 0
                    self.env_stages[note] = ENV_ATTACK
                    self.filter_envs[note] = 0

                if stage == ENV_ATTACK:
                    env_val += (ENVELOPE_MAX + 20 - env_val) * ATTACK_RATE >> 8
                    if env_val >= ENVELOPE_MAX:
                        env_val = ENVELOPE_MAX
                        self.env_stages[note] = ENV_DECAY
                    filt_env += FILTER_ENV_ATTACK
                    if filt_env > ENVELOPE_MAX:
                        filt_env = ENVELOPE_MAX

                elif stage == ENV_DECAY:
                    env_val -= (env_val - SUSTAIN_LEVEL) * DECAY_RATE >> 8
                    if env_val <= SUSTAIN_LEVEL + 2:
                        env_val = SUSTAIN_LEVEL
                        self.env_stages[note] = ENV_SUSTAIN
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0

                elif stage == ENV_SUSTAIN:
                    env_val = SUSTAIN_LEVEL
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0
            else:
                if was_on:
                    self.env_stages[note] = ENV_RELEASE

                if stage == ENV_RELEASE or env_val > 0:
                    env_val -= env_val * RELEASE_RATE >> 8
                    if env_val < 2:
                        env_val = 0
                        self.env_stages[note] = ENV_IDLE
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0

            self.envelopes[note] = env_val
            self.filter_envs[note] = filt_env

        self.prev_pressed = pressed

    def generate_and_output(self):
        """Generate one buffer and send to I2S."""
        # Read shared state
        pressed = reader.get_pressed()
        volume = reader.get_volume()
        vibrato = reader.get_vibrato()
        filter_val = reader.get_filter()
        waveform_idx = reader.get_waveform_idx()
        octave = reader.get_octave()

        # Check for waveform/octave changes
        if waveform_idx != self.prev_waveform or octave != self.prev_octave:
            self._update_wavetable(waveform_idx, octave)
            self.prev_waveform = waveform_idx
            self.prev_octave = octave

        # Process envelopes
        self._process_envelopes(pressed)

        # Calculate filter parameters
        avg_filt_env = sum(self.filter_envs) // 13
        base_cutoff = 20 + (filter_val * filter_val * 236) // 10000
        env_mod = (avg_filt_env * 80) >> 8
        filter_cutoff_final = min(256, base_cutoff + env_mod)
        resonance = 88  # Fixed moderate resonance

        # Generate audio
        _generate_viper(
            self.buffer,
            self.current_lut,
            self.phases,
            self.current_steps,
            self.envelopes,
            LFO_LUT,
            self.lfo_phase,
            volume,
            vibrato,
            filter_cutoff_final,
            resonance,
            self.filter_state
        )

        # Output to I2S
        self.i2s.write(self.buffer)

    def run(self):
        """Main audio loop - runs until stopped."""
        print("[Core 1] Audio core starting...")

        while reader.is_running():
            self.generate_and_output()

        print("[Core 1] Audio core stopped.")
        self.i2s.deinit()


# =============================================================================
# CORE 1 ENTRY POINT
# =============================================================================

def audio_core_main():
    """
    Entry point for Core 1.
    Call this via: _thread.start_new_thread(audio_core_main, ())
    """
    try:
        core = AudioCore()
        core.run()
    except Exception as e:
        print(f"[Core 1] Error: {e}")
        import sys
        sys.print_exception(e)
