"""
test_potentiometer.py - Teste den Potentiometer einzeln

ALPS RS Series Slide Potentiometer - Volume Control Wiring:
    Terminal 1 (Poti links)   -> GND (0V)
    Terminal 2 (Poti mitte)   -> GP26 (ADC0)
    Terminal 3 (Poti rechts)  -> 3.3V

Audio Taper (15A) wird zu linearer Kontrolle korrigiert.
Bottom = 0%, Top = 100%
"""

import time
from hardware import Potentiometer

# ADC Pin (GP26 = ADC0, GP27 = ADC1, GP28 = ADC2)
POTI_PIN = 27

def main():
    print("=" * 50)
    print("POTENTIOMETER TEST - Audio Taper zu Linear")
    print("=" * 50)
    print(f"Poti an GP{POTI_PIN}")
    print("Drehe am Poti - Werte sollten LINEAR von 0-100% gehen")
    print("Ctrl+C zum Beenden")
    print("=" * 50)
    print()
    
    # Potentiometer initialisieren (mit Linearisierung)
    poti = Potentiometer(POTI_PIN)
    
    while True:
        # Rohwert (0-65535)
        raw = poti.read_raw()
        
        # Linearisierter Prozent (0-100)
        percent = poti.percent()
        
        # Balken-Anzeige
        bar_len = percent // 5
        bar = "█" * bar_len + "░" * (20 - bar_len)
        
        print(f"\rRaw: {raw:5d} | {percent:3d}% |{bar}|", end="")
        
        time.sleep(0.1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nTest beendet.")
