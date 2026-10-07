# pico-synth-c

A 12-voice polyphonic synthesizer for the Raspberry Pi Pico W, written in C with the Pico SDK. It has twelve hardware keys, three potentiometers, I2S audio out to a MAX98357A amplifier, and a web UI you open on your phone over the synth's own Wi-Fi.

> Developed as a course project (Programmieren III) at MCI, winter semester 2025/26; uploaded October 2026.

![The finished build](docs/images/build.jpg)

## Features

- **Real-time polyphony in fixed-point C**: 12 voices, each with its own ADSR envelope. It uses wavetable synthesis with linear interpolation, four waveforms (sine, saw, square, triangle), vibrato, and a low-pass filter. Audio is 16-bit stereo at 22.05 kHz.
- **Dual-core design**: Core 1 only renders audio. It streams triple-buffered audio to the DAC through a PIO I2S program and DMA. Core 0 reads keys and pots and serves Wi-Fi, so network traffic never interrupts the audio.
- **Phone control without a network**: the Pico W runs its own access point and HTTP server. The web UI is embedded in the firmware. From it you can change waveform, octave, vibrato and filter, choose one of 7 presets (plus a custom slot), and tune envelope, detune and saturation live.

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43 Wi-Fi |
| MAX98357A I2S amplifier | Drives a 4–8 Ω speaker |
| 12 + 2 push buttons | Note keys plus waveform and octave buttons |
| 3 × 10 kΩ potentiometers | Volume, vibrato, filter |

## Wiring

All buttons connect the GPIO to **GND** when pressed (the internal pull-ups are used, so no resistors are needed).

| Function | Pico pin |
|---|---|
| Keys C4, C#4, D4, D#4, E4, F4 | GP0, GP1, GP2, GP3, GP4, GP5 |
| Keys F#4, G4, G#4, A4 | GP12, GP13, GP14, GP15 |
| Keys A#4, B4 | GP19, GP20 |
| Waveform button (sine → saw → square → triangle) | GP21 |
| Octave button (−1 / 0 / +1) | GP22 |
| Vibrato pot wiper | GP26 (ADC0) |
| Volume pot wiper | GP27 (ADC1) |
| Filter pot wiper | GP28 (ADC2) |
| Pot outer legs | 3V3 and GND |
| MAX98357A BCLK | GP16 |
| MAX98357A LRC | GP17 |
| MAX98357A DIN | GP18 |
| MAX98357A VIN / GND | 3V3 / GND |

The filter reading is inverted in software (`main.c`) to match how the pot was wired on the prototype board. If your pot works backwards, remove the `100 -` in front of the filter reading.

**Optional display:** `src/display.c` contains an ST7735 driver (SCK GP10, MOSI GP11, DC GP19, RST GP20, CS GP21). The display broke during development, so the phone UI replaced it. Its control pins overlap with keys A#/B and the waveform button, so the display is **compiled out by default**. To use it, pass `-DSYNTH_ENABLE_DISPLAY=ON` and move those keys to free pins.

## Build and flash

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.x, the Arm GNU toolchain, CMake and Ninja. The easiest way to get all of these is the official **Raspberry Pi Pico** VS Code extension: open this folder and choose *Compile*.

From the command line:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G Ninja      # board is set to pico_w in CMakeLists.txt
cmake --build build
```

To flash:

1. Hold **BOOTSEL** while plugging the Pico W in over USB.
2. Copy `build/pico_synth.uf2` to the `RPI-RP2` drive that appears.

The LED blinks five times at boot. Debug output goes to USB serial.

## Using the web UI

1. Join the Wi-Fi network **`PicoSynth`** (password `synth1234`). This is the synth's own access point, set in `include/wifi_server.h`.
2. Open **http://192.168.4.1/** for the live controls, or **http://192.168.4.1/tune** for presets and sound tuning.

JSON endpoints: `/api`, `/set?...`, `/tune/api`, `/tune/set?...`.

## Project layout

```
src/
  main.c           Core 0 UI loop and the Core 1 audio engine (the active synth path)
  audio_driver.c   PIO I2S + DMA triple-buffer streaming
  i2s.pio          PIO program generating BCLK/LRCLK/DATA
  wifi_server.c    Access point, DHCP, HTTP server and embedded web UI
  shared_state.c   Lock-free state shared between the two cores
  synth.c          Earlier float-based engine (SVF filter, supersaw); initialised but not used for rendering
  display.c        ST7735 driver (optional, see above)
include/           Headers, presets (synth_params.h), lwIP config
prototype/         Original MicroPython version (see below)
```

## Why C?

The first version ran on MicroPython (in [`prototype/micropython`](prototype/micropython)). Even with Viper-optimised code and a dedicated audio thread, it ran out of CPU at more than a few voices, and garbage-collector pauses caused audible dropouts. Porting it to C with the Pico SDK made full 12-voice polyphony possible. The C port also gave direct access to PIO and DMA, and deterministic timing on the audio core.

## Credits

Group project at MCI with Hauke Döllefeld, Samuel Marxer and Elias Maier.

- **C firmware** (everything outside `prototype/`): Lukas K.
- **MicroPython prototype**: Lukas K., with early contributions from Hauke Döllefeld, Samuel Marxer and Elias Maier.
