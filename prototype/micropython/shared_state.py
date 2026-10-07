"""
shared_state.py - Shared State Between Cores

This module provides thread-safe shared memory for communication
between Core 0 (UI) and Core 1 (Audio).

Core 0 writes: pressed_mask, volume, vibrato, filter, waveform, octave
Core 1 reads: all of the above for audio generation

Using array.array for atomic-ish access (single word reads/writes).
"""

import array

# Shared state indices
IDX_PRESSED = 0       # Button bitmask (13 bits)
IDX_VOLUME = 1        # Volume 0-256
IDX_VIBRATO = 2       # Vibrato depth 0-100
IDX_FILTER = 3        # Filter cutoff 0-100
IDX_WAVEFORM = 4      # 0=SINE, 1=SAW, 2=SQUARE, 3=TRIANGLE
IDX_OCTAVE = 5        # -1, 0, +1 (stored as 0, 1, 2)
IDX_RUNNING = 6       # 1 = audio core running, 0 = stop

# Waveform name mapping
WAVEFORM_NAMES = ["SINE", "SAW", "SQUARE", "TRIANGLE"]
WAVEFORM_TO_IDX = {"SINE": 0, "SAW": 1, "SQUARE": 2, "TRIANGLE": 3}

# Shared state array (7 integers)
# Using 'i' (signed int) for compatibility
shared_state = array.array('i', [
    0,    # pressed mask
    128,  # volume (50%)
    0,    # vibrato
    80,   # filter cutoff
    0,    # waveform (SINE)
    1,    # octave (0, stored as 1)
    0,    # running flag
])


class SharedStateWriter:
    """
    Writer interface for Core 0 (UI core).
    Provides methods to update shared state that Core 1 reads.
    """

    def __init__(self, state_array):
        self.state = state_array

    def set_pressed(self, mask: int):
        """Set the currently pressed buttons bitmask."""
        self.state[IDX_PRESSED] = mask

    def set_volume(self, vol: int):
        """Set volume (0-256)."""
        self.state[IDX_VOLUME] = vol

    def set_vibrato(self, vib: int):
        """Set vibrato depth (0-100)."""
        self.state[IDX_VIBRATO] = vib

    def set_filter(self, flt: int):
        """Set filter cutoff (0-100)."""
        self.state[IDX_FILTER] = flt

    def set_waveform(self, waveform_name: str):
        """Set waveform by name (SINE, SAW, SQUARE, TRIANGLE)."""
        idx = WAVEFORM_TO_IDX.get(waveform_name.upper(), 0)
        self.state[IDX_WAVEFORM] = idx

    def set_octave(self, octave: int):
        """Set octave (-1, 0, +1)."""
        # Store as 0, 1, 2 internally
        self.state[IDX_OCTAVE] = octave + 1

    def start_audio(self):
        """Signal audio core to start."""
        self.state[IDX_RUNNING] = 1

    def stop_audio(self):
        """Signal audio core to stop."""
        self.state[IDX_RUNNING] = 0

    def get_waveform_name(self) -> str:
        """Get current waveform name."""
        idx = self.state[IDX_WAVEFORM]
        return WAVEFORM_NAMES[idx]

    def get_octave(self) -> int:
        """Get current octave (-1, 0, +1)."""
        return self.state[IDX_OCTAVE] - 1

    def cycle_waveform(self) -> str:
        """Cycle to next waveform, returns new name."""
        idx = (self.state[IDX_WAVEFORM] + 1) % 4
        self.state[IDX_WAVEFORM] = idx
        return WAVEFORM_NAMES[idx]

    def cycle_octave(self) -> int:
        """Cycle octave: -1 -> 0 -> 1 -> -1, returns new octave."""
        stored = self.state[IDX_OCTAVE]
        stored = (stored % 3) + 1  # 1->2->3->1 (which is 0->1->2->0 then +1)
        if stored > 2:
            stored = 0
        self.state[IDX_OCTAVE] = stored
        return stored - 1


class SharedStateReader:
    """
    Reader interface for Core 1 (Audio core).
    Provides methods to read shared state that Core 0 writes.
    """

    def __init__(self, state_array):
        self.state = state_array

    def get_pressed(self) -> int:
        """Get currently pressed buttons bitmask."""
        return self.state[IDX_PRESSED]

    def get_volume(self) -> int:
        """Get volume (0-256)."""
        return self.state[IDX_VOLUME]

    def get_vibrato(self) -> int:
        """Get vibrato depth (0-100)."""
        return self.state[IDX_VIBRATO]

    def get_filter(self) -> int:
        """Get filter cutoff (0-100)."""
        return self.state[IDX_FILTER]

    def get_waveform_idx(self) -> int:
        """Get waveform index (0-3)."""
        return self.state[IDX_WAVEFORM]

    def get_octave(self) -> int:
        """Get octave (-1, 0, +1)."""
        return self.state[IDX_OCTAVE] - 1

    def is_running(self) -> bool:
        """Check if audio should be running."""
        return self.state[IDX_RUNNING] == 1


# Global instances
writer = SharedStateWriter(shared_state)
reader = SharedStateReader(shared_state)
