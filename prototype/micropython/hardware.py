"""
hardware.py - Hardware Module für den Synthesizer

Enthält wiederverwendbare Klassen für:
- Potentiometer (ADC)
- Buttons
- Audio Output (I2S)

Importiere in anderen Dateien:
    from hardware import Potentiometer, Buttons, AudioOut
"""

from machine import Pin, ADC, I2S
import array
import time


class Potentiometer:
    """
    ALPS RS Series Slide Potentiometer für Volume Control.

    Pinbelegung:
        Terminal 1 (links)   -> GND
        Terminal 2 (mitte)   -> ADC Pin
        Terminal 3 (rechts)  -> 3.3V

    Linearisierung: Wenn das Poti Audio Taper ist, wird ein Power-Exponent
    angewendet um echte 0–100% linearen Verlauf zu erhalten.

    Jitter-Filterung: Averaging und Hysterese verhindern Rauschen.
    """

    # Configuration constants
    ADC_HYSTERESIS = 512      # Minimum ADC change to register (out of 65535)
    AVERAGING_SAMPLES = 2     # Number of samples to average (reduced for speed)

    def __init__(self, pin):
        """
        Args:
            pin: GPIO ADC Pin Nummer (26, 27, oder 28 für ADC0-2)
        """
        self.adc = ADC(Pin(pin))
        # Initialize with actual reading to avoid stuck-at-zero issue
        initial_raw = self.adc.read_u16()
        self._last_raw = initial_raw
        # Calculate initial percent
        normalized = initial_raw / 65535.0
        linearized = normalized ** 0.4
        self._last_percent = int(linearized * 100)

    def read_raw(self):
        """Rohwert vom ADC (0–65535, unverändert)"""
        return self.adc.read_u16()

    def _read_averaged(self):
        """Read ADC with averaging to reduce noise"""
        # Discard first reading to allow ADC to settle after channel switch
        _ = self.adc.read_u16()
        time.sleep_us(50)  # Small delay for ADC settling

        total = 0
        for _ in range(self.AVERAGING_SAMPLES):
            total += self.adc.read_u16()
        return total // self.AVERAGING_SAMPLES

    def percent(self):
        """
        Linearer Prozentwert 0–100 mit Jitter-Filterung.

        Features:
        - Averaging: Reduces high-frequency noise
        - Hysteresis: Only updates if change exceeds threshold
        - Audio taper linearization via power transformation
        """
        raw = self._read_averaged()

        # Apply hysteresis: only update if change is significant
        if abs(raw - self._last_raw) >= self.ADC_HYSTERESIS:
            self._last_raw = raw

            # Normalisieren zu 0.0–1.0
            normalized = raw / 65535.0

            # Audio Taper korrigieren: Exponent < 1 expandiert niedrige Werte
            linearized = normalized ** 0.4

            self._last_percent = int(linearized * 100)

        return self._last_percent





class Buttons:
    """
    Verwaltet mehrere Tasten mit Pull-up.
    
    Verwendung:
        btns = Buttons([0, 1, 2, 3])  # GP0-GP3
        pressed = btns.read_mask()    # Bitmaske
        if btns.is_pressed(0):        # Einzelne Taste
            ...
    """
    
    def __init__(self, pins: list):
        """
        Args:
            pins: Liste von GPIO Pin Nummern
        """
        self.pins = pins
        self.buttons = [Pin(p, Pin.IN, Pin.PULL_UP) for p in pins]
        self.count = len(pins)
    
    def is_pressed(self, index: int) -> bool:
        """Ist Taste an Index gedrückt?"""
        return self.buttons[index].value() == 0
    
    def read_mask(self) -> int:
        """Gibt Bitmaske zurück (Bit 0 = erste Taste, etc.)"""
        mask = 0
        for i in range(self.count):
            if self.buttons[i].value() == 0:
                mask |= (1 << i)
        return mask
    
    def read_list(self) -> list:
        """Gibt Liste von booleans zurück"""
        return [self.buttons[i].value() == 0 for i in range(self.count)]


class AudioOut:
    """
    I2S Audio Ausgabe.
    
    Verwendung:
        audio = AudioOut()
        audio.write(buffer)
    """
    
    def __init__(self, sck=16, ws=17, sd=18, rate=22050, buf_size=256):
        """
        Args:
            sck: Bit Clock Pin (default GP16)
            ws: Word Select Pin (default GP17)
            sd: Serial Data Pin (default GP18)
            rate: Sample Rate in Hz (default 22.05 kHz für bessere Qualität)
            buf_size: Buffer Größe
        """
        self.rate = rate
        self.buf_size = buf_size

        self.i2s = I2S(
            0,
            sck=Pin(sck),
            ws=Pin(ws),
            sd=Pin(sd),
            mode=I2S.TX,
            bits=16,
            format=I2S.MONO,
            rate=rate,
            ibuf=buf_size * 16  # Larger buffer to prevent underruns
        )
    
    def write(self, buffer):
        """Schreibt Buffer zum Audio-Ausgang"""
        self.i2s.write(buffer)
    
    def stop(self):
        """Stoppt Audio"""
        self.i2s.deinit()
