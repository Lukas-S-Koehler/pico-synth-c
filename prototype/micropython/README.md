# MicroPython prototype

This is the first version of the synth, kept for reference. It was replaced by the C firmware in the repository root, because MicroPython could not keep up with real-time polyphonic audio.

It has the same architecture as the C version. Core 0 (`main.py`) handles keys, pots and the ST7735 display. Core 1 (`audio_core.py`, started with `_thread`) renders 22.05 kHz audio to the MAX98357A over I2S. The two cores communicate through `shared_state.py`.

**Pin assignment differs from the C version:**

| Function | Pin |
|---|---|
| 13 keys, C4 to C5 | GP0–GP9, GP12, GP13, GP22 |
| Waveform / octave buttons | GP14 / GP15 |
| Vibrato / volume / filter pots | GP26 / GP27 / GP28 |
| I2S BCLK / LRC / DIN | GP16 / GP17 / GP18 |
| ST7735 SCK / MOSI / DC / RST / CS | GP10 / GP11 / GP19 / GP20 / GP21 |

## Files

| File | Purpose |
|---|---|
| `main.py` | Entry point, UI loop on core 0 |
| `audio_core.py` | Audio loop on core 1 |
| `audio_engine.py` | Viper-optimised synthesis engine |
| `hardware.py` | Button, potentiometer and I2S helpers |
| `shared_state.py` | Inter-core state |
| `synth_display.py` | ST7735 display UI |
| `wavetables.py` | Generated band-limited wavetables |
| `tools/generate_wavetables.py` | Runs on a PC (NumPy + SciPy) and regenerates `wavetables.py` |
| `tests/` | Hardware tests for pots and volume |

The early MicroPython work had contributions from Hauke Döllefeld, Samuel Marxer and Elias Maier.

To run it, copy the `.py` files (and the test you want) to the root of a Pico W running MicroPython, for example with `mpremote` or Thonny.
