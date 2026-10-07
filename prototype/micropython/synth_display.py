"""
synth_display.py - Synthesizer Display Integration

Zeigt Synthesizer-Status auf ST7735 TFT Display.

Neue Pin-Belegung (keine Konflikte mit I2S):
    Display SPI:
        SCL -> GP10, SDA -> GP11
        DC  -> GP19, RES -> GP20, CS -> GP21

    Control Buttons:
        Waveform Button -> GP14
        Mode Button     -> GP15
"""

from machine import Pin, SPI
import time
import math

# 5x7 Bitmap Font Data (each character is 5 bytes, one per column)
# Bit 0 = top row, Bit 6 = bottom row
FONT_5X7 = {
    32: [0x00, 0x00, 0x00, 0x00, 0x00],  # Space
    33: [0x00, 0x00, 0x5F, 0x00, 0x00],  # !
    37: [0x23, 0x13, 0x08, 0x64, 0x62],  # %
    43: [0x08, 0x08, 0x3E, 0x08, 0x08],  # +
    45: [0x08, 0x08, 0x08, 0x08, 0x08],  # -
    48: [0x3E, 0x51, 0x49, 0x45, 0x3E],  # 0
    49: [0x00, 0x42, 0x7F, 0x40, 0x00],  # 1
    50: [0x42, 0x61, 0x51, 0x49, 0x46],  # 2
    51: [0x21, 0x41, 0x45, 0x4B, 0x31],  # 3
    52: [0x18, 0x14, 0x12, 0x7F, 0x10],  # 4
    53: [0x27, 0x45, 0x45, 0x45, 0x39],  # 5
    54: [0x3C, 0x4A, 0x49, 0x49, 0x30],  # 6
    55: [0x01, 0x71, 0x09, 0x05, 0x03],  # 7
    56: [0x36, 0x49, 0x49, 0x49, 0x36],  # 8
    57: [0x06, 0x49, 0x49, 0x29, 0x1E],  # 9
    58: [0x00, 0x36, 0x36, 0x00, 0x00],  # :
    65: [0x7E, 0x11, 0x11, 0x11, 0x7E],  # A
    66: [0x7F, 0x49, 0x49, 0x49, 0x36],  # B
    67: [0x3E, 0x41, 0x41, 0x41, 0x22],  # C
    68: [0x7F, 0x41, 0x41, 0x22, 0x1C],  # D
    69: [0x7F, 0x49, 0x49, 0x49, 0x41],  # E
    70: [0x7F, 0x09, 0x09, 0x09, 0x01],  # F
    71: [0x3E, 0x41, 0x49, 0x49, 0x7A],  # G
    72: [0x7F, 0x08, 0x08, 0x08, 0x7F],  # H
    73: [0x00, 0x41, 0x7F, 0x41, 0x00],  # I
    74: [0x20, 0x40, 0x41, 0x3F, 0x01],  # J
    75: [0x7F, 0x08, 0x14, 0x22, 0x41],  # K
    76: [0x7F, 0x40, 0x40, 0x40, 0x40],  # L
    77: [0x7F, 0x02, 0x0C, 0x02, 0x7F],  # M
    78: [0x7F, 0x04, 0x08, 0x10, 0x7F],  # N
    79: [0x3E, 0x41, 0x41, 0x41, 0x3E],  # O
    80: [0x7F, 0x09, 0x09, 0x09, 0x06],  # P
    81: [0x3E, 0x41, 0x51, 0x21, 0x5E],  # Q
    82: [0x7F, 0x09, 0x19, 0x29, 0x46],  # R
    83: [0x46, 0x49, 0x49, 0x49, 0x31],  # S
    84: [0x01, 0x01, 0x7F, 0x01, 0x01],  # T
    85: [0x3F, 0x40, 0x40, 0x40, 0x3F],  # U
    86: [0x1F, 0x20, 0x40, 0x20, 0x1F],  # V
    87: [0x3F, 0x40, 0x38, 0x40, 0x3F],  # W
    88: [0x63, 0x14, 0x08, 0x14, 0x63],  # X
    89: [0x07, 0x08, 0x70, 0x08, 0x07],  # Y
    90: [0x61, 0x51, 0x49, 0x45, 0x43],  # Z
    97: [0x20, 0x54, 0x54, 0x54, 0x78],  # a
    99: [0x38, 0x44, 0x44, 0x44, 0x20],  # c
    101: [0x38, 0x54, 0x54, 0x54, 0x18], # e
    105: [0x00, 0x44, 0x7D, 0x40, 0x00], # i
    108: [0x00, 0x41, 0x7F, 0x40, 0x00], # l
    110: [0x7C, 0x04, 0x04, 0x04, 0x78], # n
    111: [0x38, 0x44, 0x44, 0x44, 0x38], # o
    115: [0x48, 0x54, 0x54, 0x54, 0x20], # s
    116: [0x04, 0x3F, 0x44, 0x40, 0x20], # t
    118: [0x1C, 0x20, 0x40, 0x20, 0x1C], # v
}


class ST7735:
    """Treiber für ST7735 TFT Display"""

    # Display Kommandos
    SWRESET = 0x01
    SLPOUT = 0x11
    COLMOD = 0x3A
    MADCTL = 0x36
    CASET = 0x2A
    RASET = 0x2B
    RAMWR = 0x2C
    DISPON = 0x29
    INVON = 0x21

    def __init__(self, spi, dc, cs, rst, width=128, height=160):
        self.spi = spi
        self.dc = dc
        self.cs = cs
        self.rst = rst
        self.width = width
        self.height = height

        self.dc.init(Pin.OUT)
        self.cs.init(Pin.OUT)
        self.rst.init(Pin.OUT)

        self.cs(1)
        self.reset()
        self.init_display()

    def reset(self):
        """Hardware Reset durchführen"""
        self.rst(1)
        time.sleep_ms(5)
        self.rst(0)
        time.sleep_ms(20)
        self.rst(1)
        time.sleep_ms(150)

    def write_cmd(self, cmd):
        """Kommando senden (DC=LOW)"""
        self.cs(0)
        self.dc(0)
        self.spi.write(bytearray([cmd]))
        self.cs(1)

    def write_data(self, data):
        """Daten senden (DC=HIGH)"""
        self.cs(0)
        self.dc(1)
        self.spi.write(data if isinstance(data, (bytes, bytearray)) else bytearray([data]))
        self.cs(1)

    def init_display(self):
        """Display konfigurieren"""
        self.write_cmd(self.SWRESET)
        time.sleep_ms(150)

        self.write_cmd(self.SLPOUT)
        time.sleep_ms(255)

        # 16-bit Farbmodus
        self.write_cmd(self.COLMOD)
        self.write_data(0x05)

        self.write_cmd(self.MADCTL)
        self.write_data(0x00)

        self.write_cmd(self.INVON)
        self.write_cmd(self.DISPON)
        time.sleep_ms(100)

    def set_window(self, x0, y0, x1, y1):
        """Zeichenbereich festlegen"""
        self.write_cmd(self.CASET)
        self.write_data(bytearray([0x00, x0, 0x00, x1]))
        self.write_cmd(self.RASET)
        self.write_data(bytearray([0x00, y0, 0x00, y1]))
        self.write_cmd(self.RAMWR)

    def fill(self, color):
        """Ganzes Display mit Farbe füllen"""
        self.set_window(0, 0, self.width - 1, self.height - 1)
        chunk = bytearray([color >> 8, color & 0xFF] * 512)
        chunks = (self.width * self.height) // 512

        self.cs(0)
        self.dc(1)
        for _ in range(chunks):
            self.spi.write(chunk)
        self.cs(1)

    def pixel(self, x, y, color):
        """Einzelnen Pixel setzen"""
        if 0 <= x < self.width and 0 <= y < self.height:
            self.set_window(x, y, x, y)
            self.write_data(bytearray([color >> 8, color & 0xFF]))

    def hline(self, x, y, w, color):
        """Horizontale Linie"""
        if 0 <= y < self.height:
            x = max(0, x)
            w = min(w, self.width - x)
            self.set_window(x, y, x + w - 1, y)
            self.write_data(bytearray([color >> 8, color & 0xFF] * w))

    def fill_rect(self, x, y, w, h, color):
        """Gefülltes Rechteck"""
        for i in range(h):
            self.hline(x, y + i, w, color)

    def rect(self, x, y, w, h, color):
        """Rechteck Rahmen"""
        self.hline(x, y, w, color)
        self.hline(x, y + h - 1, w, color)
        for i in range(h):
            self.pixel(x, y + i, color)
            self.pixel(x + w - 1, y + i, color)

    def text(self, text, x, y, color, scale=1):
        """Text ausgeben (einfach, 6x8 Font)"""
        for char in text:
            self.draw_char(char, x, y, color, scale)
            x += 6 * scale

    def draw_char(self, char, x, y, color, scale=1):
        """Einzelnes Zeichen zeichnen mit Bitmap-Font"""
        c = ord(char)

        # Font-Daten holen (Space als Fallback)
        if c in FONT_5X7:
            font_data = FONT_5X7[c]
        else:
            font_data = FONT_5X7[32]  # Space for unknown chars

        # 5 Spalten, 7 Zeilen
        for col in range(5):
            col_bits = font_data[col]
            for row in range(7):
                if col_bits & (1 << row):
                    if scale == 1:
                        self.pixel(x + col, y + row, color)
                    else:
                        for sy in range(scale):
                            for sx in range(scale):
                                self.pixel(x + col * scale + sx, y + row * scale + sy, color)


class SynthDisplay:
    """Synthesizer Display - zeigt Waveform, Volume, etc."""

    def __init__(self, display):
        self.display = display

        # Farben (RGB565)
        self.BLACK = 0x0000
        self.WHITE = 0xFFFF
        self.RED = 0xF800
        self.GREEN = 0x07E0
        self.BLUE = 0x001F
        self.YELLOW = 0xFFE0
        self.CYAN = 0x07FF
        self.MAGENTA = 0xF81F

        # Graph Bereich für Wellenform (kompakter für Platz)
        self.graph_x = 10
        self.graph_y = 38
        self.graph_w = 108
        self.graph_h = 40

        self.current_waveform = "SINE"  # Muss mit audio_engine übereinstimmen
        self.current_octave = 0  # -1, 0, +1
        self.volume = 0
        self.vibrato = 0
        self.filter = 100
        self.notes_playing = 0

    def init_ui(self):
        """Initiales UI zeichnen"""
        self.display.fill(self.BLACK)
        self.display.text("SYNTHESIZER", 15, 5, self.CYAN, 1)

        # Rahmen für Waveform-Anzeige (kleiner für mehr Platz)
        self.display.rect(self.graph_x - 1, self.graph_y - 1,
                         self.graph_w + 2, self.graph_h + 2, self.WHITE)

        # Labels Row 1: Wave und Octave
        self.display.text("Wave:", 5, 25, self.WHITE)
        self.display.text("Oct:", 85, 25, self.WHITE)

        # Labels für Potis (unterhalb Graph, graph endet bei y=78)
        self.display.text("Vol", 3, 85, self.WHITE)
        self.display.text("Vib", 3, 100, self.CYAN)
        self.display.text("Flt", 3, 115, self.MAGENTA)

        # Notes Anzeige unten
        self.display.text("Notes:", 3, 132, self.WHITE)

        self.update_waveform_display()
        self.update_octave_display()
        self.update_volume_display(0)
        self.update_vibrato_display(0)
        self.update_filter_display(0)
        self.update_notes_display(0)

    def update_waveform_display(self):
        """Wellenform-Name und Graph aktualisieren"""
        # Name löschen und neu schreiben
        self.display.fill_rect(45, 23, 80, 10, self.BLACK)

        color = self.GREEN
        if self.current_waveform == "SINE":
            color = self.GREEN
        elif self.current_waveform == "SQUARE":
            color = self.YELLOW
        elif self.current_waveform == "SAW":
            color = self.CYAN
        elif self.current_waveform == "TRIANGLE":
            color = self.MAGENTA

        self.display.text(self.current_waveform, 45, 25, color)

        # Wellenform zeichnen
        self.draw_waveform()

    def draw_waveform(self):
        """Wellenform im Graph zeichnen"""
        # Graph löschen
        self.display.fill_rect(self.graph_x, self.graph_y,
                              self.graph_w, self.graph_h, self.BLACK)

        # Mittellinie
        mid_y = self.graph_y + self.graph_h // 2
        for i in range(0, self.graph_w, 4):
            self.display.pixel(self.graph_x + i, mid_y, self.CYAN)

        # Wellenform zeichnen
        color = self.GREEN
        prev_y = None

        for x in range(self.graph_w):
            # Berechne Y basierend auf Wellenform
            if self.current_waveform == "SINE":
                angle = (x / self.graph_w) * 4 * math.pi
                y_val = (math.sin(angle) + 1) / 2
            elif self.current_waveform == "SQUARE":
                y_val = 1 if (x % (self.graph_w // 2)) < (self.graph_w // 4) else 0
            elif self.current_waveform == "SAW":
                y_val = (x % (self.graph_w // 2)) / (self.graph_w // 2)
            elif self.current_waveform == "TRIANGLE":
                period = self.graph_w // 2
                pos = x % period
                y_val = (2 * pos / period) if pos < period // 2 else (2 - 2 * pos / period)

            y_pos = self.graph_y + int((1 - y_val) * (self.graph_h - 1))

            # Linie zwischen Punkten
            if prev_y is not None:
                y_start = min(prev_y, y_pos)
                y_end = max(prev_y, y_pos)
                for y in range(y_start, y_end + 1):
                    self.display.pixel(self.graph_x + x, y, color)
            else:
                self.display.pixel(self.graph_x + x, y_pos, color)

            prev_y = y_pos

    def update_octave_display(self):
        """Oktave-Anzeige aktualisieren"""
        # Bereich löschen
        self.display.fill_rect(110, 23, 18, 10, self.BLACK)

        # Oktave als Text
        if self.current_octave == -1:
            oct_text = "-1"
            color = self.CYAN
        elif self.current_octave == 0:
            oct_text = "0"
            color = self.GREEN
        else:
            oct_text = "+1"
            color = self.YELLOW

        self.display.text(oct_text, 110, 25, color)

    def set_octave(self, octave):
        """Oktave setzen und Display aktualisieren"""
        self.current_octave = octave
        self.update_octave_display()

    def _draw_bar(self, y, percent, bar_color):
        """Hilfsfunktion: Zeichnet einen Balken mit Prozentanzeige"""
        bar_x = 25
        bar_w = 70
        bar_h = 6

        # Rahmen
        self.display.rect(bar_x - 1, y - 1, bar_w + 2, bar_h + 2, self.WHITE)

        # Füllung löschen
        self.display.fill_rect(bar_x, y, bar_w, bar_h, self.BLACK)

        # Neue Füllung
        fill_w = int((percent / 100.0) * bar_w)
        if fill_w > 0:
            self.display.fill_rect(bar_x, y, fill_w, bar_h, bar_color)

        # Prozent-Text (rechts vom Balken)
        self.display.fill_rect(bar_x + bar_w + 3, y - 2, 30, 10, self.BLACK)
        self.display.text(f"{percent}%", bar_x + bar_w + 4, y - 1, self.WHITE)

    def update_volume_display(self, volume_percent):
        """Volume-Anzeige aktualisieren"""
        self.volume = volume_percent
        color = self.GREEN if volume_percent > 50 else self.YELLOW if volume_percent > 20 else self.RED
        self._draw_bar(87, volume_percent, color)

    def update_notes_display(self, note_count):
        """Anzahl spielender Noten anzeigen"""
        self.notes_playing = note_count

        # Bereich löschen
        self.display.fill_rect(45, 130, 80, 10, self.BLACK)

        # Text schreiben
        if note_count == 0:
            self.display.text("---", 45, 132, self.WHITE)
        else:
            color = self.GREEN if note_count <= 3 else self.YELLOW if note_count <= 6 else self.RED
            self.display.text(f"{note_count}", 45, 132, color)

    def update_vibrato_display(self, vibrato_percent):
        """Vibrato-Anzeige aktualisieren"""
        self.vibrato = vibrato_percent
        self._draw_bar(102, vibrato_percent, self.CYAN)

    def update_filter_display(self, filter_percent):
        """Filter-Anzeige aktualisieren"""
        self.filter = filter_percent
        self._draw_bar(117, filter_percent, self.MAGENTA)

    def set_waveform(self, waveform_name):
        """Wellenform setzen und Display aktualisieren"""
        self.current_waveform = waveform_name.upper()
        self.update_waveform_display()


def create_display():
    """Display initialisieren und zurückgeben"""
    # SPI für Display (neue Pins ohne Konflikt)
    spi = SPI(1, baudrate=8000000, polarity=0, phase=0, sck=Pin(10), mosi=Pin(11))

    # Display Control Pins (NEUE PINS - kein Konflikt mit I2S)
    dc = Pin(19, Pin.OUT)
    cs = Pin(21, Pin.OUT)
    rst = Pin(20, Pin.OUT)

    display = ST7735(spi, dc, cs, rst)
    synth_display = SynthDisplay(display)
    synth_display.init_ui()

    return synth_display
