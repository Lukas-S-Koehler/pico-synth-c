"""
test_volume.py - Teste Lautstärkeregelung mit Poti

Verkabelung:
    Poti:
        links   -> GND
        mitte   -> GP26 (ADC0)
        rechts  -> 3.3V
    
    Tasten: GP0, GP1 (zum Testen)
    
    Audio: wie immer (GP16, 17, 18)
"""

from machine import Pin, I2S
from hardware import Potentiometer
from audio_engine import SynthEngine, BUFFER_SIZE, SAMPLE_RATE
import array

# Pins
POTI_PIN = 27
TEST_BUTTONS = [0, 1]  # Nur 2 Tasten zum Testen

def main():
    print("=" * 50)
    print("VOLUME CONTROL TEST")
    print("=" * 50)
    print("Drehe am Poti für Lautstärke")
    print(f"Tasten an GP{TEST_BUTTONS}")
    print("Ctrl+C zum Beenden")
    print("=" * 50)
    
    # Hardware
    poti = Potentiometer(POTI_PIN)
    buttons = [Pin(p, Pin.IN, Pin.PULL_UP) for p in TEST_BUTTONS]
    
    audio = I2S(
        0,
        sck=Pin(16),
        ws=Pin(17),
        sd=Pin(18),
        mode=I2S.TX,
        bits=16,
        format=I2S.MONO,
        rate=SAMPLE_RATE,
        ibuf=BUFFER_SIZE * 4
    )
    
    # Synth Engine
    engine = SynthEngine()

    last_percent = -1

    while True:
        # Poti lesen (0-100%) mit Mittelung über 5 Samples für Stabilität
        percent_sum = 0
        for _ in range(5):
            percent_sum += poti.percent()
        percent = percent_sum // 5

        # Umwandeln in Volume Multiplier (0-256 Bereich)
        # 0% -> 0 (stumm), 100% -> 256 (max)
        vol_mult = (percent * 256) // 100

        # Debug-Ausgabe wenn sich Lautstärke signifikant ändert (>2% Unterschied)
        if abs(percent - last_percent) > 2:
            print(f"Volume: {percent}% (multiplier={vol_mult})")
            last_percent = percent

        # Tasten lesen
        pressed = 0
        for i, btn in enumerate(buttons):
            if btn.value() == 0:
                pressed |= (1 << i)

        # Audio generieren mit korrektem 0-256 Volume Multiplier
        buf = engine.generate(pressed, vol_mult)
        
        # Ausgeben
        audio.write(buf)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nTest beendet.")
