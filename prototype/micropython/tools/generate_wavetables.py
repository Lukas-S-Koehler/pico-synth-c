"""
generate_wavetables.py - High-Quality Wavetable Generator

Generates anti-aliased, optimized wavetables for the Pico synthesizer.
Run this on your PC, then copy the output to the Pico.

Features:
- Oversampled generation (16x) with proper anti-aliasing
- Gibbs phenomenon reduction using sigma approximation
- Multiple wavetables per waveform for different frequency ranges
- Exports as MicroPython-compatible arrays

Usage:
    python generate_wavetables.py

Output:
    wavetables.py - Import this on your Pico
"""

import numpy as np
from scipy import signal
import math

# =============================================================================
# CONFIGURATION
# =============================================================================

TABLE_SIZE = 1024          # Samples per wavetable
SAMPLE_RATE = 22050        # Target sample rate
OVERSAMPLING = 16          # Oversampling factor for generation
NUM_OCTAVE_TABLES = 4      # Number of mipmap levels (for different freq ranges)

# Amplitude (16-bit signed, leave headroom for 13-voice polyphony)
# Max safe amplitude = 32767 / 13 ≈ 2520
AMPLITUDE = 2400

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def lanczos_sigma(n, N):
    """
    Lanczos sigma factor to reduce Gibbs phenomenon (ringing at discontinuities).
    This smooths the Fourier series without losing too much sharpness.
    """
    if n == 0:
        return 1.0
    x = n * math.pi / N
    return math.sin(x) / x


def generate_bandlimited_saw(table_size, num_harmonics, apply_sigma=True):
    """
    Generate band-limited sawtooth wave using additive synthesis.

    Sawtooth Fourier series: sum of sin(n*x)/n for n=1,2,3,...
    """
    table = np.zeros(table_size, dtype=np.float64)

    for n in range(1, num_harmonics + 1):
        # Sigma factor reduces Gibbs ringing
        sigma = lanczos_sigma(n, num_harmonics) if apply_sigma else 1.0

        # Generate harmonic
        phase = np.linspace(0, 2 * np.pi * n, table_size, endpoint=False)
        harmonic = np.sin(phase) / n

        # Alternating sign for sawtooth
        table += sigma * harmonic * ((-1) ** (n + 1))

    # Normalize to [-1, 1]
    table *= 2 / np.pi

    return table


def generate_bandlimited_square(table_size, num_harmonics, apply_sigma=True):
    """
    Generate band-limited square wave using additive synthesis.

    Square Fourier series: sum of sin((2n-1)*x)/(2n-1) for n=1,2,3,...
    """
    table = np.zeros(table_size, dtype=np.float64)

    for n in range(1, num_harmonics + 1):
        harmonic_num = 2 * n - 1  # Odd harmonics only: 1, 3, 5, 7...

        if harmonic_num > num_harmonics:
            break

        sigma = lanczos_sigma(harmonic_num, num_harmonics) if apply_sigma else 1.0

        phase = np.linspace(0, 2 * np.pi * harmonic_num, table_size, endpoint=False)
        harmonic = np.sin(phase) / harmonic_num

        table += sigma * harmonic

    # Normalize to [-1, 1]
    table *= 4 / np.pi

    return table


def generate_bandlimited_triangle(table_size, num_harmonics, apply_sigma=True):
    """
    Generate band-limited triangle wave using additive synthesis.

    Triangle Fourier series: sum of (-1)^n * sin((2n+1)*x)/(2n+1)^2
    """
    table = np.zeros(table_size, dtype=np.float64)

    sign = 1
    for n in range(num_harmonics):
        harmonic_num = 2 * n + 1  # Odd harmonics: 1, 3, 5, 7...

        if harmonic_num > num_harmonics:
            break

        sigma = lanczos_sigma(harmonic_num, num_harmonics) if apply_sigma else 1.0

        phase = np.linspace(0, 2 * np.pi * harmonic_num, table_size, endpoint=False)
        harmonic = sign * np.sin(phase) / (harmonic_num ** 2)

        table += sigma * harmonic
        sign = -sign

    # Normalize to [-1, 1]
    table *= 8 / (np.pi ** 2)

    return table


def generate_sine(table_size):
    """Generate pure sine wave (already band-limited)."""
    phase = np.linspace(0, 2 * np.pi, table_size, endpoint=False)
    return np.sin(phase)


def generate_with_oversampling(generator_func, table_size, num_harmonics, oversample=16):
    """
    Generate wavetable with oversampling and anti-aliasing filter.

    1. Generate at higher sample rate
    2. Apply low-pass anti-aliasing filter
    3. Downsample to target size
    """
    # Generate oversampled table
    oversample_size = table_size * oversample

    if generator_func == generate_sine:
        oversampled = generator_func(oversample_size)
    else:
        # Use more harmonics for oversampled generation
        oversampled = generator_func(oversample_size, num_harmonics * oversample, apply_sigma=True)

    # Design anti-aliasing filter (steep low-pass at Nyquist/2)
    nyquist = 0.5 / oversample
    cutoff = nyquist * 0.9  # Slight margin

    # Use a high-order FIR filter for clean cutoff
    num_taps = oversample * 8 + 1
    fir = signal.firwin(num_taps, cutoff, window='blackman')

    # Apply filter (zero-phase for no delay)
    filtered = signal.filtfilt(fir, 1, oversampled)

    # Downsample
    downsampled = filtered[::oversample]

    # Ensure exact table size
    if len(downsampled) != table_size:
        downsampled = downsampled[:table_size]

    return downsampled


def generate_mipmap_tables(waveform_name, table_size, base_harmonics):
    """
    Generate multiple wavetables for different frequency ranges (mipmapping).

    Lower frequencies can use more harmonics without aliasing.
    Higher frequencies need fewer harmonics.

    Returns dict of tables keyed by max frequency.
    """
    tables = {}

    # Frequency ranges (Hz) and corresponding max harmonics
    # At 22050 Hz sample rate, Nyquist = 11025 Hz
    ranges = [
        ("LOW",    130,   base_harmonics),      # C3 and below: full harmonics
        ("MID",    260,   base_harmonics // 2), # C3-C4: half harmonics
        ("HIGH",   520,   base_harmonics // 4), # C4-C5: quarter harmonics
        ("VHIGH", 1040,   base_harmonics // 8), # C5+: minimal harmonics
    ]

    generators = {
        "SINE": generate_sine,
        "SAW": generate_bandlimited_saw,
        "SQUARE": generate_bandlimited_square,
        "TRIANGLE": generate_bandlimited_triangle,
    }

    gen_func = generators[waveform_name]

    for name, max_freq, num_harm in ranges:
        if gen_func == generate_sine:
            table = generate_sine(table_size)
        else:
            # Ensure at least 4 harmonics
            num_harm = max(4, num_harm)
            table = generate_with_oversampling(gen_func, table_size, num_harm, OVERSAMPLING)

        tables[name] = table

    return tables


def table_to_int16(table, amplitude):
    """Convert float table [-1, 1] to int16 with given amplitude."""
    # Normalize to ensure [-1, 1] range
    max_val = np.max(np.abs(table))
    if max_val > 0:
        table = table / max_val

    # Scale and convert
    scaled = table * amplitude
    return np.clip(scaled, -32768, 32767).astype(np.int16)


def format_array_micropython(arr, name, items_per_line=12):
    """Format numpy array as MicroPython array.array('h', [...])."""
    lines = [f"{name} = array.array('h', ["]

    values = arr.tolist()
    for i in range(0, len(values), items_per_line):
        chunk = values[i:i + items_per_line]
        line = "    " + ", ".join(f"{v:6d}" for v in chunk) + ","
        lines.append(line)

    lines.append("])")
    return "\n".join(lines)


# =============================================================================
# MAIN GENERATION
# =============================================================================

def main():
    print("=" * 60)
    print("HIGH-QUALITY WAVETABLE GENERATOR")
    print("=" * 60)
    print(f"Table size: {TABLE_SIZE} samples")
    print(f"Sample rate: {SAMPLE_RATE} Hz")
    print(f"Oversampling: {OVERSAMPLING}x")
    print(f"Amplitude: {AMPLITUDE}")
    print()

    # Generate all waveforms
    waveforms = ["SINE", "SAW", "SQUARE", "TRIANGLE"]
    base_harmonics = 64  # For lowest frequency range

    all_tables = {}

    for wf in waveforms:
        print(f"Generating {wf}...")
        tables = generate_mipmap_tables(wf, TABLE_SIZE, base_harmonics)
        all_tables[wf] = tables

        # Print stats
        for range_name, table in tables.items():
            rms = np.sqrt(np.mean(table ** 2))
            peak = np.max(np.abs(table))
            print(f"  {range_name}: RMS={rms:.3f}, Peak={peak:.3f}")

    print()
    print("Generating MicroPython output...")

    # Generate output file
    output_lines = [
        '"""',
        'wavetables.py - Pre-generated Anti-Aliased Wavetables',
        '',
        'Generated by generate_wavetables.py',
        'DO NOT EDIT - regenerate using the tool instead.',
        '',
        'Features:',
        '- 16x oversampled generation with anti-aliasing',
        '- Gibbs phenomenon reduction (Lanczos sigma)',
        '- Multiple tables per waveform for different frequency ranges',
        '"""',
        '',
        'import array',
        '',
        f'TABLE_SIZE = {TABLE_SIZE}',
        f'AMPLITUDE = {AMPLITUDE}',
        '',
        '# Frequency range thresholds (Hz)',
        '# Use LOW for f < 130, MID for f < 260, HIGH for f < 520, VHIGH for higher',
        'FREQ_THRESHOLDS = [130, 260, 520]',
        '',
    ]

    # For simplicity on Pico, we'll use a single high-quality table per waveform
    # (the MID range which works well for C4-C5)
    for wf in waveforms:
        table = all_tables[wf]["MID"]
        int_table = table_to_int16(table, AMPLITUDE)

        output_lines.append(f"# {wf} wavetable (optimized for mid frequencies)")
        output_lines.append(format_array_micropython(int_table, f"{wf}_LUT"))
        output_lines.append("")

    # Also generate LOW tables for bass notes (octave down)
    output_lines.append("# LOW frequency tables (for octave-down mode)")
    for wf in waveforms:
        if wf == "SINE":
            continue  # Sine doesn't need separate low table
        table = all_tables[wf]["LOW"]
        int_table = table_to_int16(table, AMPLITUDE)
        output_lines.append(format_array_micropython(int_table, f"{wf}_LOW_LUT"))
        output_lines.append("")

    # Write output
    output_file = "wavetables.py"
    with open(output_file, "w") as f:
        f.write("\n".join(output_lines))

    print(f"Written to: {output_file}")
    print()

    # Also generate a simpler single-table version for testing
    print("Generating simplified single-table version...")

    simple_lines = [
        '"""',
        'wavetables_simple.py - Single optimized wavetable per waveform',
        '',
        'Use this if memory is tight or for testing.',
        '"""',
        '',
        'import array',
        '',
        f'TABLE_SIZE = {TABLE_SIZE}',
        '',
    ]

    for wf in waveforms:
        # Use HIGH table which works across most frequencies
        table = all_tables[wf]["HIGH"]
        int_table = table_to_int16(table, AMPLITUDE)
        simple_lines.append(format_array_micropython(int_table, f"{wf}_LUT"))
        simple_lines.append("")

    with open("wavetables_simple.py", "w") as f:
        f.write("\n".join(simple_lines))

    print("Written to: wavetables_simple.py")
    print()
    print("=" * 60)
    print("DONE! Copy wavetables.py or wavetables_simple.py to your Pico")
    print("=" * 60)


if __name__ == "__main__":
    main()
