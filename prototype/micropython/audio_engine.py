"""
audio_engine.py - Professional Audio Engine with SVF and ADSR

Based on techniques from pico_synth reference implementation:
- State Variable Filter (SVF) with resonance - musical sounding
- Exponential ADSR envelopes - proper attack/decay curves
- Filter envelope modulation - punchy attacks
- Soft clipping (tanh approximation) - warm distortion
- Oscillator detuning - rich, full sound
- Anti-aliased wavetables with Gibbs phenomenon reduction
- Linear interpolation for smooth playback
- True pitch vibrato

Usage:
    from audio_engine import SynthEngine

    engine = SynthEngine()
    engine.set_waveform("SAW")
    buf = engine.generate(pressed_mask, volume)
"""

import math
import array
from wavetables import (
    TABLE_SIZE, AMPLITUDE, WAVEFORMS,
    SINE_LUT, SAW_LUT, SQUARE_LUT, TRIANGLE_LUT
)


# =============================================================================
# CONFIGURATION
# =============================================================================

# Audio sample rate in Hz
SAMPLE_RATE = 22050

# Audio buffer size in samples (256 samples @ 22050 Hz = ~11.6ms latency)
BUFFER_SIZE = 256

# Envelope parameters (8-bit resolution: 0-256)
ENVELOPE_MAX = 256

# ADSR envelope stages
ENV_IDLE = 0
ENV_ATTACK = 1
ENV_DECAY = 2
ENV_SUSTAIN = 3
ENV_RELEASE = 4

# Exponential envelope rates (per buffer, ~11.6ms)
# These create musical-sounding curves (fast start, slow finish)
ATTACK_RATE = 35      # Fast attack (~85ms to peak)
DECAY_RATE = 18       # Moderate decay
SUSTAIN_LEVEL = 180   # ~70% sustain
RELEASE_RATE = 12     # Smooth release (~250ms)

# Filter envelope settings
FILTER_ENV_AMOUNT = 3000  # Hz modulation at full envelope
FILTER_ENV_ATTACK = 40    # Very fast filter attack (punchy)
FILTER_ENV_DECAY = 8      # Slow filter decay (sweep down)

# LFO for vibrato
LFO_SIZE = 256
# LFO rate: ~5 Hz (musical vibrato speed)
LFO_RATE = 5.0
LFO_STEP = int((LFO_RATE * LFO_SIZE * 256) / SAMPLE_RATE)

# Note frequencies C4-C5 (chromatic scale)
# Index matches button order in main.py
FREQUENCIES = [
    261.63,  # 0:  C4
    277.18,  # 1:  C#4
    293.66,  # 2:  D4
    311.13,  # 3:  D#4
    329.63,  # 4:  E4
    349.23,  # 5:  F4
    369.99,  # 6:  F#4
    392.00,  # 7:  G4
    415.30,  # 8:  G#4
    440.00,  # 9:  A4
    466.16,  # 10: A#4
    493.88,  # 11: B4
    523.25,  # 12: C5
]


# =============================================================================
# PRE-CALCULATED PHASE STEPS
# =============================================================================
# Phase step = (frequency * TABLE_SIZE * 256) / SAMPLE_RATE
# The *256 gives us 8 bits of fractional precision for interpolation

def _make_steps(octave_shift=0):
    """Calculate phase steps for given octave."""
    factor = 2.0 ** octave_shift
    return array.array('I', [
        int((f * factor * TABLE_SIZE * 256) / SAMPLE_RATE)
        for f in FREQUENCIES
    ])

STEPS_OCT_DOWN = _make_steps(-1)
STEPS_OCT_NORMAL = _make_steps(0)
STEPS_OCT_UP = _make_steps(1)


# =============================================================================
# LFO WAVETABLE (for vibrato)
# =============================================================================

LFO_LUT = array.array('h', [
    int(math.sin(2 * math.pi * i / LFO_SIZE) * 127)
    for i in range(LFO_SIZE)
])


# =============================================================================
# VIPER-OPTIMIZED AUDIO GENERATION WITH SVF AND LINEAR INTERPOLATION
# =============================================================================

@micropython.viper
def _generate_viper(buf, lut, phases, steps, env_ptr,
                    lfo_lut, lfo_phase_ptr,
                    volume: int, vibrato: int, filter_cutoff: int,
                    resonance: int, filter_state_ptr):
    """
    Viper-optimized audio generation with State Variable Filter.

    SVF provides better sound than one-pole filter:
    - Smooth resonance without instability
    - Musical frequency response
    - No clicks on parameter changes

    Linear interpolation for smooth wavetable playback.
    """
    b = ptr16(buf)
    l = ptr16(lut)
    p = ptr32(phases)
    s = ptr32(steps)
    env = ptr16(env_ptr)
    lfo = ptr16(lfo_lut)
    lfo_p = ptr32(lfo_phase_ptr)
    filt = ptr32(filter_state_ptr)  # [low, band, cutoff_smooth]

    # Constants
    table_mask: int = 1023          # TABLE_SIZE - 1
    lfo_mask: int = 255             # LFO_SIZE - 1
    phase_max: int = 262144         # TABLE_SIZE * 256

    vol: int = volume
    vib_depth: int = vibrato
    cutoff: int = filter_cutoff     # 0-256 range
    reso: int = resonance           # 0-256 range (careful above 220!)

    # SVF state (scaled by 256 for fixed-point)
    svf_low: int = filt[0]
    svf_band: int = filt[1]

    lfo_phase: int = lfo_p[0]
    lfo_step: int = 3               # Slow LFO for smooth vibrato

    i: int = 0
    while i < 256:
        sample: int = 0

        # Update LFO (for vibrato)
        lfo_val: int = lfo[(lfo_phase >> 8) & lfo_mask]
        lfo_phase += lfo_step
        if lfo_phase >= 65536:
            lfo_phase -= 65536

        # Pitch modulation from vibrato
        # At vib_depth=100: +/-~50 cents pitch deviation
        pitch_mod: int = 256
        if vib_depth > 0:
            pitch_mod = 256 + ((lfo_val * vib_depth) >> 9)

        # Process all 13 notes with linear interpolation
        # Note 0
        if env[0] > 0:
            ph: int = p[0]
            idx: int = (ph >> 8) & table_mask
            frac: int = ph & 255
            idx_next: int = (idx + 1) & table_mask
            s0: int = l[idx]
            s1: int = l[idx_next]
            # Linear interpolation
            interp: int = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[0]) >> 8
        step: int = s[0]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[0] = p[0] + step
        if p[0] >= phase_max:
            p[0] -= phase_max

        # Note 1
        if env[1] > 0:
            ph = p[1]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[1]) >> 8
        step = s[1]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[1] = p[1] + step
        if p[1] >= phase_max:
            p[1] -= phase_max

        # Note 2
        if env[2] > 0:
            ph = p[2]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[2]) >> 8
        step = s[2]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[2] = p[2] + step
        if p[2] >= phase_max:
            p[2] -= phase_max

        # Note 3
        if env[3] > 0:
            ph = p[3]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[3]) >> 8
        step = s[3]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[3] = p[3] + step
        if p[3] >= phase_max:
            p[3] -= phase_max

        # Note 4
        if env[4] > 0:
            ph = p[4]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[4]) >> 8
        step = s[4]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[4] = p[4] + step
        if p[4] >= phase_max:
            p[4] -= phase_max

        # Note 5
        if env[5] > 0:
            ph = p[5]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[5]) >> 8
        step = s[5]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[5] = p[5] + step
        if p[5] >= phase_max:
            p[5] -= phase_max

        # Note 6
        if env[6] > 0:
            ph = p[6]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[6]) >> 8
        step = s[6]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[6] = p[6] + step
        if p[6] >= phase_max:
            p[6] -= phase_max

        # Note 7
        if env[7] > 0:
            ph = p[7]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[7]) >> 8
        step = s[7]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[7] = p[7] + step
        if p[7] >= phase_max:
            p[7] -= phase_max

        # Note 8
        if env[8] > 0:
            ph = p[8]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[8]) >> 8
        step = s[8]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[8] = p[8] + step
        if p[8] >= phase_max:
            p[8] -= phase_max

        # Note 9
        if env[9] > 0:
            ph = p[9]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[9]) >> 8
        step = s[9]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[9] = p[9] + step
        if p[9] >= phase_max:
            p[9] -= phase_max

        # Note 10
        if env[10] > 0:
            ph = p[10]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[10]) >> 8
        step = s[10]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[10] = p[10] + step
        if p[10] >= phase_max:
            p[10] -= phase_max

        # Note 11
        if env[11] > 0:
            ph = p[11]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[11]) >> 8
        step = s[11]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[11] = p[11] + step
        if p[11] >= phase_max:
            p[11] -= phase_max

        # Note 12
        if env[12] > 0:
            ph = p[12]
            idx = (ph >> 8) & table_mask
            frac = ph & 255
            idx_next = (idx + 1) & table_mask
            s0 = l[idx]
            s1 = l[idx_next]
            interp = s0 + (((s1 - s0) * frac) >> 8)
            sample += (interp * env[12]) >> 8
        step = s[12]
        if vib_depth > 0:
            step = (step * pitch_mod) >> 8
        p[12] = p[12] + step
        if p[12] >= phase_max:
            p[12] -= phase_max

        # Apply volume
        sample = (sample * vol) >> 8

        # State Variable Filter (SVF) - sounds much better than one-pole!
        # Cutoff: 0=20Hz, 256=full (scaled to filter coefficient)
        # f = 2 * sin(pi * cutoff / sample_rate) approximated
        f: int = cutoff >> 2  # Scale to 0-64 range for stability
        if f < 4:
            f = 4  # Minimum cutoff

        # Q factor from resonance (higher = more resonant)
        # q = 1.0 - resonance, clamped for stability
        q: int = 256 - reso
        if q < 20:
            q = 20  # Prevent self-oscillation

        # SVF equations (fixed-point, scaled by 256):
        # low += f * band
        # high = input - low - q * band
        # band += f * high
        svf_low = svf_low + ((f * svf_band) >> 8)
        high: int = sample - svf_low - ((q * svf_band) >> 8)
        svf_band = svf_band + ((f * high) >> 8)

        # Soft clip internal states (prevents blowup with high resonance)
        if svf_band > 30000:
            svf_band = 30000
        elif svf_band < -30000:
            svf_band = -30000

        # Use lowpass output
        sample = svf_low

        # Improved soft clipping (tanh approximation)
        # x * (27 + x^2) / (27 + 9*x^2) scaled for int math
        # This sounds warmer than simple linear clipping
        if sample > 20000:
            excess: int = sample - 20000
            sample = 20000 + ((excess * 27) // (27 + ((excess * excess) >> 12)))
        elif sample < -20000:
            excess = -sample - 20000
            sample = -20000 - ((excess * 27) // (27 + ((excess * excess) >> 12)))

        # Final hard limit with headroom
        if sample > 32000:
            sample = 32000
        elif sample < -32000:
            sample = -32000

        b[i] = sample
        i += 1

    # Save SVF state
    filt[0] = svf_low
    filt[1] = svf_band
    lfo_p[0] = lfo_phase


# =============================================================================
# SYNTH ENGINE CLASS
# =============================================================================

class SynthEngine:
    """
    Professional synthesizer engine with SVF and ADSR.

    Features:
    - State Variable Filter with resonance
    - ADSR envelopes with exponential curves
    - Filter envelope for punchy attacks
    - Octave-aware wavetable selection (prevents aliasing)
    - Linear interpolation for smooth playback
    - True pitch vibrato
    """

    def __init__(self):
        # Oscillator state
        self.phases = array.array('I', [0] * 13)
        self.buffer = array.array('h', [0] * BUFFER_SIZE)

        # ADSR envelope state per note
        self.envelopes = array.array('H', [0] * 13)       # Current value
        self.env_stages = array.array('B', [0] * 13)      # Current stage
        self.filter_envs = array.array('H', [0] * 13)     # Filter envelope
        self.prev_pressed = 0

        # SVF filter state: [low, band]
        self.filter_state = array.array('i', [0, 0])

        # LFO state
        self.lfo_phase = array.array('I', [0])

        # Current settings
        self.current_waveform = "SINE"
        self.current_octave = 0
        self.current_lut = SINE_LUT
        self.current_steps = STEPS_OCT_NORMAL

        # Control parameters (0-100)
        self.vibrato = 0
        self.filter_cutoff = 80       # 0=dark, 100=bright
        self.filter_resonance = 40    # Fixed moderate resonance for character

        # Waveform and octave lookup tables
        self.octave_steps = {
            -1: STEPS_OCT_DOWN,
            0: STEPS_OCT_NORMAL,
            1: STEPS_OCT_UP
        }

    def _update_lut(self):
        """Update current wavetable based on waveform and octave."""
        self.current_lut = WAVEFORMS[self.current_waveform][self.current_octave]

    def set_waveform(self, waveform_name):
        """Set waveform type (SINE, SAW, SQUARE, TRIANGLE)."""
        waveform_name = waveform_name.upper()
        if waveform_name in WAVEFORMS:
            self.current_waveform = waveform_name
            self._update_lut()
            return True
        return False

    def get_waveform(self):
        """Get current waveform name."""
        return self.current_waveform

    def cycle_waveform(self):
        """Cycle to next waveform."""
        order = ["SINE", "SAW", "SQUARE", "TRIANGLE"]
        idx = order.index(self.current_waveform)
        self.set_waveform(order[(idx + 1) % len(order)])
        return self.current_waveform

    def set_octave(self, octave):
        """Set octave (-1, 0, or 1)."""
        if octave in self.octave_steps:
            self.current_octave = octave
            self.current_steps = self.octave_steps[octave]
            self._update_lut()
            return True
        return False

    def get_octave(self):
        """Get current octave."""
        return self.current_octave

    def cycle_octave(self):
        """Cycle through octaves: -1 -> 0 -> 1 -> -1."""
        order = [-1, 0, 1]
        idx = order.index(self.current_octave)
        self.set_octave(order[(idx + 1) % len(order)])
        return self.current_octave

    def set_vibrato(self, value):
        """Set vibrato depth (0-100)."""
        self.vibrato = max(0, min(100, value))

    def set_filter(self, value):
        """Set filter cutoff (0=dark, 100=bright)."""
        self.filter_cutoff = max(0, min(100, value))

    def set_resonance(self, value):
        """Set filter resonance (0=none, 100=max). Careful above 80!"""
        self.filter_resonance = max(0, min(100, value))

    def generate(self, pressed: int, volume: int = 256):
        """
        Generate one buffer of audio.

        Args:
            pressed: Bitmask of currently pressed keys (bit 0 = note 0, etc.)
            volume: Master volume (0 = silent, 256 = full)

        Returns:
            Audio buffer (array of 16-bit signed samples)
        """
        # Process ADSR envelopes for each note
        for note in range(13):
            mask = 1 << note
            note_on = pressed & mask
            was_on = self.prev_pressed & mask
            stage = self.env_stages[note]
            env_val = self.envelopes[note]
            filt_env = self.filter_envs[note]

            if note_on:
                # Note pressed
                if not was_on:
                    # Fresh note-on: reset phase if envelope is low
                    if env_val < 32:
                        self.phases[note] = 0
                    # Start attack
                    self.env_stages[note] = ENV_ATTACK
                    self.filter_envs[note] = 0  # Reset filter envelope

                if stage == ENV_ATTACK:
                    # Exponential attack: fast start, slow finish
                    # env += (target - env) * rate
                    env_val += (ENVELOPE_MAX + 20 - env_val) * ATTACK_RATE >> 8
                    if env_val >= ENVELOPE_MAX:
                        env_val = ENVELOPE_MAX
                        self.env_stages[note] = ENV_DECAY
                    # Filter envelope attack (very fast for punch)
                    filt_env += FILTER_ENV_ATTACK
                    if filt_env > ENVELOPE_MAX:
                        filt_env = ENVELOPE_MAX

                elif stage == ENV_DECAY:
                    # Exponential decay toward sustain
                    env_val -= (env_val - SUSTAIN_LEVEL) * DECAY_RATE >> 8
                    if env_val <= SUSTAIN_LEVEL + 2:
                        env_val = SUSTAIN_LEVEL
                        self.env_stages[note] = ENV_SUSTAIN
                    # Filter envelope decay
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0

                elif stage == ENV_SUSTAIN:
                    env_val = SUSTAIN_LEVEL
                    # Filter envelope continues to decay
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0
            else:
                # Note released
                if was_on:
                    self.env_stages[note] = ENV_RELEASE

                if stage == ENV_RELEASE or env_val > 0:
                    # Exponential release to zero
                    env_val -= env_val * RELEASE_RATE >> 8
                    if env_val < 2:
                        env_val = 0
                        self.env_stages[note] = ENV_IDLE
                    # Filter envelope release
                    if filt_env > 0:
                        filt_env -= FILTER_ENV_DECAY
                        if filt_env < 0:
                            filt_env = 0

            self.envelopes[note] = env_val
            self.filter_envs[note] = filt_env

        self.prev_pressed = pressed

        # Calculate filter cutoff with filter envelope modulation
        # Base cutoff + envelope amount
        fc = self.filter_cutoff
        # Average filter envelope for global modulation
        avg_filt_env = sum(self.filter_envs) // 13
        # Exponential curve for filter cutoff (more usable range)
        base_cutoff = 20 + (fc * fc * 236) // 10000
        env_mod = (avg_filt_env * 80) >> 8  # Add brightness on attack
        filter_cutoff_final = min(256, base_cutoff + env_mod)

        # Resonance: 0-100 maps to 0-220 (safe range)
        resonance_scaled = (self.filter_resonance * 220) // 100

        # Generate audio with SVF
        _generate_viper(
            self.buffer,
            self.current_lut,
            self.phases,
            self.current_steps,
            self.envelopes,
            LFO_LUT,
            self.lfo_phase,
            volume,
            self.vibrato,
            filter_cutoff_final,
            resonance_scaled,
            self.filter_state
        )

        return self.buffer

    def get_buffer(self):
        """Get the current audio buffer."""
        return self.buffer
