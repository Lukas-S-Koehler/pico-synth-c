"""
main.py - Dual-Core Polyphonic Synthesizer

Architecture:
    Core 0 (this file): UI, buttons, potentiometers, display
    Core 1 (audio_core.py): Dedicated real-time audio generation

This separation prevents UI updates from causing audio glitches.
Core 0 writes to shared_state, Core 1 reads from it.

Hardware:
    - 13 Tasten (GP0-GP9, GP12, GP13, GP22) für Noten
    - Volume Potentiometer (GP27)
    - Vibrato Potentiometer (GP26)
    - Filter Potentiometer (GP28)
    - Waveform Button (GP14)
    - Octave Button (GP15)
    - Display (GP10, 11, 19, 20, 21)
"""

import _thread
import time
from machine import Pin
from hardware import Potentiometer, Buttons
from synth_display import create_display
from shared_state import writer
from audio_core import audio_core_main


# =============================================================================
# CONFIGURATION
# =============================================================================

# Note button pins (13 keys for C4-C5)
BUTTON_PINS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 22]

# Potentiometer pins
VOLUME_POTI_PIN = 27
VIBRATO_POTI_PIN = 26
FILTER_POTI_PIN = 28

# Control button pins
WAVEFORM_BUTTON_PIN = 14
OCTAVE_BUTTON_PIN = 15

# Update intervals
DISPLAY_UPDATE_INTERVAL = 100  # ms
POTI_UPDATE_INTERVAL = 5       # Loop iterations


# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def count_notes(pressed_mask):
    """Count number of pressed keys."""
    count = 0
    for i in range(13):
        if pressed_mask & (1 << i):
            count += 1
    return count


# =============================================================================
# MAIN - CORE 0 (UI CORE)
# =============================================================================

def main():
    print("=" * 50)
    print("DUAL-CORE POLYPHONIC SYNTHESIZER")
    print("=" * 50)
    print("Core 0: UI, buttons, display")
    print("Core 1: Dedicated audio generation")
    print("=" * 50)

    # -------------------------------------------------------------------------
    # INITIALIZE HARDWARE
    # -------------------------------------------------------------------------
    print("Initializing hardware...")

    try:
        note_buttons = Buttons(BUTTON_PINS)
        print("  [OK] Note buttons (13 keys)")
    except Exception as e:
        print(f"  [FAIL] Note buttons: {e}")
        raise

    try:
        volume_poti = Potentiometer(VOLUME_POTI_PIN)
        vibrato_poti = Potentiometer(VIBRATO_POTI_PIN)
        filter_poti = Potentiometer(FILTER_POTI_PIN)
        print("  [OK] Potentiometers (Vol, Vib, Flt)")
    except Exception as e:
        print(f"  [FAIL] Potentiometers: {e}")
        raise

    try:
        waveform_btn = Pin(WAVEFORM_BUTTON_PIN, Pin.IN, Pin.PULL_UP)
        octave_btn = Pin(OCTAVE_BUTTON_PIN, Pin.IN, Pin.PULL_UP)
        print("  [OK] Control buttons (Wave, Oct)")
    except Exception as e:
        print(f"  [FAIL] Control buttons: {e}")
        raise

    # Initialize display
    print("Initializing display...")
    display = None
    try:
        display = create_display()
        print("  [OK] Display")
    except Exception as e:
        print(f"  [WARN] Display init failed: {e}")
        print("  Running in headless mode...")

    # -------------------------------------------------------------------------
    # INITIALIZE SHARED STATE
    # -------------------------------------------------------------------------
    print("Setting up shared state...")

    # Read initial potentiometer values
    vol_percent = volume_poti.percent()
    vibrato_percent = vibrato_poti.percent()
    filter_percent = filter_poti.percent()

    # Write initial values to shared state
    vol_mult = (vol_percent * 256) // 100
    writer.set_volume(vol_mult)
    writer.set_vibrato(vibrato_percent)
    writer.set_filter(filter_percent)
    writer.set_waveform("SINE")
    writer.set_octave(0)

    print(f"  Initial: Vol={vol_percent}% Vib={vibrato_percent}% Flt={filter_percent}%")

    # Update display with initial values
    if display:
        display.update_volume_display(vol_percent)
        display.update_vibrato_display(vibrato_percent)
        display.update_filter_display(filter_percent)
        display.update_notes_display(0)

    # -------------------------------------------------------------------------
    # START AUDIO CORE (Core 1)
    # -------------------------------------------------------------------------
    print("Starting audio core on Core 1...")
    writer.start_audio()

    try:
        _thread.start_new_thread(audio_core_main, ())
        print("  [OK] Audio core started")
    except Exception as e:
        print(f"  [FAIL] Audio core: {e}")
        raise

    # Small delay to let audio core initialize
    time.sleep_ms(100)

    # -------------------------------------------------------------------------
    # MAIN LOOP - CORE 0 (UI)
    # -------------------------------------------------------------------------
    print("=" * 50)
    print("Synthesizer ready! Press keys to play.")
    print("=" * 50)

    # Button debounce state
    last_waveform_state = 1
    last_octave_state = 1
    last_waveform_time = 0
    last_octave_time = 0
    debounce_delay = 200  # ms

    # Display update tracking
    last_display_update = 0
    last_vol_percent = vol_percent
    last_vib_percent = vibrato_percent
    last_flt_percent = filter_percent
    last_note_count = 0

    # Potentiometer update counter (don't read every loop)
    poti_counter = 0

    while True:
        current_time = time.ticks_ms()

        # ---------------------------------------------------------------------
        # READ NOTE BUTTONS (every loop - fast)
        # ---------------------------------------------------------------------
        pressed = note_buttons.read_mask()
        writer.set_pressed(pressed)

        # ---------------------------------------------------------------------
        # SLOW UPDATES (potentiometers, control buttons, display)
        # ---------------------------------------------------------------------
        poti_counter += 1

        if poti_counter >= POTI_UPDATE_INTERVAL:
            poti_counter = 0

            # Read potentiometers
            vol_percent = volume_poti.percent()
            vibrato_percent = vibrato_poti.percent()
            filter_percent = filter_poti.percent()

            # Update shared state
            vol_mult = (vol_percent * 256) // 100
            writer.set_volume(vol_mult)
            writer.set_vibrato(vibrato_percent)
            writer.set_filter(filter_percent)

            # Count notes for display
            note_count = count_notes(pressed)

            # -----------------------------------------------------------------
            # WAVEFORM BUTTON
            # -----------------------------------------------------------------
            waveform_state = waveform_btn.value()
            if waveform_state == 0 and last_waveform_state == 1:
                if time.ticks_diff(current_time, last_waveform_time) > debounce_delay:
                    new_waveform = writer.cycle_waveform()
                    if display:
                        display.set_waveform(new_waveform)
                    print(f"Waveform: {new_waveform}")
                    last_waveform_time = current_time
            last_waveform_state = waveform_state

            # -----------------------------------------------------------------
            # OCTAVE BUTTON
            # -----------------------------------------------------------------
            octave_state = octave_btn.value()
            if octave_state == 0 and last_octave_state == 1:
                if time.ticks_diff(current_time, last_octave_time) > debounce_delay:
                    new_octave = writer.cycle_octave()
                    if display:
                        display.set_octave(new_octave)
                    oct_names = {-1: "LOW", 0: "MID", 1: "HIGH"}
                    print(f"Octave: {oct_names[new_octave]}")
                    last_octave_time = current_time
            last_octave_state = octave_state

            # -----------------------------------------------------------------
            # DISPLAY UPDATE
            # -----------------------------------------------------------------
            if display and time.ticks_diff(current_time, last_display_update) >= DISPLAY_UPDATE_INTERVAL:
                # Only update changed values (saves time)
                if abs(vol_percent - last_vol_percent) > 2:
                    display.update_volume_display(vol_percent)
                    last_vol_percent = vol_percent

                if abs(vibrato_percent - last_vib_percent) > 2:
                    display.update_vibrato_display(vibrato_percent)
                    last_vib_percent = vibrato_percent

                if abs(filter_percent - last_flt_percent) > 2:
                    display.update_filter_display(filter_percent)
                    last_flt_percent = filter_percent

                if note_count != last_note_count:
                    display.update_notes_display(note_count)
                    last_note_count = note_count

                last_display_update = current_time

        # Small sleep to prevent tight polling (saves power, still responsive)
        time.sleep_us(100)


# =============================================================================
# ENTRY POINT
# =============================================================================

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        writer.stop_audio()
        time.sleep_ms(100)
        print("Synthesizer stopped.")
    except Exception as e:
        print(f"\nError: {e}")
        writer.stop_audio()
        import sys
        sys.print_exception(e)
