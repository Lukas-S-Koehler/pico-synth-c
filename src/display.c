/**
 * display.c - ST7735 TFT Display Driver
 *
 * Complete display driver matching the MicroPython synth_display.py
 * Includes 5x7 bitmap font and all UI elements
 */

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "display.h"

// ============================================================
// PIN DEFINITIONS (match MicroPython exactly)
// ============================================================
#define LCD_SCL_PIN     11
#define LCD_MOSI_PIN    10
#define LCD_CS_PIN      21
#define LCD_DC_PIN      19
#define LCD_RST_PIN     20

// Display dimensions
#define LCD_WIDTH       128
#define LCD_HEIGHT      160

// ST7735 Commands
#define ST7735_NOP      0x00
#define ST7735_SWRESET  0x01
#define ST7735_SLPOUT   0x11
#define ST7735_NORON    0x13
#define ST7735_INVOFF   0x20
#define ST7735_INVON    0x21
#define ST7735_DISPOFF  0x28
#define ST7735_DISPON   0x29
#define ST7735_CASET    0x2A
#define ST7735_RASET    0x2B
#define ST7735_RAMWR    0x2C
#define ST7735_MADCTL   0x36
#define ST7735_COLMOD   0x3A
#define ST7735_FRMCTR1  0xB1
#define ST7735_FRMCTR2  0xB2
#define ST7735_FRMCTR3  0xB3
#define ST7735_INVCTR   0xB4
#define ST7735_PWCTR1   0xC0
#define ST7735_PWCTR2   0xC1
#define ST7735_PWCTR3   0xC2
#define ST7735_PWCTR4   0xC3
#define ST7735_PWCTR5   0xC4
#define ST7735_VMCTR1   0xC5
#define ST7735_GMCTRP1  0xE0
#define ST7735_GMCTRN1  0xE1

// GREENTAB offset (this display variant has a 2,1 pixel offset)
#define GREENTAB_OFFSET_X  2
#define GREENTAB_OFFSET_Y  1

// Colors (RGB565)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F

// ============================================================
// 5x7 BITMAP FONT (matching MicroPython)
// ============================================================
static const uint8_t FONT_5X7[][5] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    // " (34)
    {0x00, 0x07, 0x00, 0x07, 0x00},
    // # (35)
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    // $ (36)
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    // % (37)
    {0x23, 0x13, 0x08, 0x64, 0x62},
    // & (38)
    {0x36, 0x49, 0x55, 0x22, 0x50},
    // ' (39)
    {0x00, 0x05, 0x03, 0x00, 0x00},
    // ( (40)
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    // ) (41)
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    // * (42)
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    // + (43)
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    // , (44)
    {0x00, 0x50, 0x30, 0x00, 0x00},
    // - (45)
    {0x08, 0x08, 0x08, 0x08, 0x08},
    // . (46)
    {0x00, 0x60, 0x60, 0x00, 0x00},
    // / (47)
    {0x20, 0x10, 0x08, 0x04, 0x02},
    // 0 (48)
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    // 1 (49)
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    // 2 (50)
    {0x42, 0x61, 0x51, 0x49, 0x46},
    // 3 (51)
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    // 4 (52)
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    // 5 (53)
    {0x27, 0x45, 0x45, 0x45, 0x39},
    // 6 (54)
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    // 7 (55)
    {0x01, 0x71, 0x09, 0x05, 0x03},
    // 8 (56)
    {0x36, 0x49, 0x49, 0x49, 0x36},
    // 9 (57)
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    // : (58)
    {0x00, 0x36, 0x36, 0x00, 0x00},
    // ; (59)
    {0x00, 0x56, 0x36, 0x00, 0x00},
    // < (60)
    {0x08, 0x14, 0x22, 0x41, 0x00},
    // = (61)
    {0x14, 0x14, 0x14, 0x14, 0x14},
    // > (62)
    {0x00, 0x41, 0x22, 0x14, 0x08},
    // ? (63)
    {0x02, 0x01, 0x51, 0x09, 0x06},
    // @ (64)
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    // A (65)
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    // B (66)
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    // C (67)
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    // D (68)
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    // E (69)
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    // F (70)
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    // G (71)
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    // H (72)
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    // I (73)
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    // J (74)
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    // K (75)
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    // L (76)
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    // M (77)
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    // N (78)
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    // O (79)
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    // P (80)
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    // Q (81)
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    // R (82)
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    // S (83)
    {0x46, 0x49, 0x49, 0x49, 0x31},
    // T (84)
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    // U (85)
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    // V (86)
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    // W (87)
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    // X (88)
    {0x63, 0x14, 0x08, 0x14, 0x63},
    // Y (89)
    {0x07, 0x08, 0x70, 0x08, 0x07},
    // Z (90)
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

// Lowercase letters (starting at ASCII 97 'a')
static const uint8_t FONT_LOWER[][5] = {
    // a (97)
    {0x20, 0x54, 0x54, 0x54, 0x78},
    // b (98)
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    // c (99)
    {0x38, 0x44, 0x44, 0x44, 0x20},
    // d (100)
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    // e (101)
    {0x38, 0x54, 0x54, 0x54, 0x18},
    // f (102)
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    // g (103)
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    // h (104)
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    // i (105)
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    // j (106)
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    // k (107)
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    // l (108)
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    // m (109)
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    // n (110)
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    // o (111)
    {0x38, 0x44, 0x44, 0x44, 0x38},
    // p (112)
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    // q (113)
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    // r (114)
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    // s (115)
    {0x48, 0x54, 0x54, 0x54, 0x20},
    // t (116)
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    // u (117)
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    // v (118)
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    // w (119)
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    // x (120)
    {0x44, 0x28, 0x10, 0x28, 0x44},
    // y (121)
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    // z (122)
    {0x44, 0x64, 0x54, 0x4C, 0x44},
};

// ============================================================
// LOW-LEVEL SPI FUNCTIONS
// ============================================================

static inline void lcd_write_cmd(uint8_t cmd) {
    gpio_put(LCD_DC_PIN, 0);  // Command mode
    gpio_put(LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &cmd, 1);
    gpio_put(LCD_CS_PIN, 1);
}

static inline void lcd_write_data(uint8_t data) {
    gpio_put(LCD_DC_PIN, 1);  // Data mode
    gpio_put(LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &data, 1);
    gpio_put(LCD_CS_PIN, 1);
}

static inline void lcd_write_data_bulk(const uint8_t* data, size_t len) {
    gpio_put(LCD_DC_PIN, 1);
    gpio_put(LCD_CS_PIN, 0);
    spi_write_blocking(spi1, data, len);
    gpio_put(LCD_CS_PIN, 1);
}

// ============================================================
// DISPLAY INITIALIZATION
// ============================================================

void display_init(void) {
    // Initialize SPI at 8MHz (matching MicroPython: polarity=0, phase=0)
    spi_init(spi1, 8000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(LCD_SCL_PIN, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI_PIN, GPIO_FUNC_SPI);

    // Initialize control pins
    gpio_init(LCD_CS_PIN);
    gpio_init(LCD_DC_PIN);
    gpio_init(LCD_RST_PIN);
    gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
    gpio_set_dir(LCD_DC_PIN, GPIO_OUT);
    gpio_set_dir(LCD_RST_PIN, GPIO_OUT);

    gpio_put(LCD_CS_PIN, 1);

    // Hardware reset - extended timing for reliability
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(50);
    gpio_put(LCD_RST_PIN, 0);
    sleep_ms(50);
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(200);

    // ========================================
    // ST7735R GREENTAB Initialization Sequence
    // ========================================

    // Software reset
    lcd_write_cmd(ST7735_SWRESET);
    sleep_ms(150);

    // Exit sleep mode
    lcd_write_cmd(ST7735_SLPOUT);
    sleep_ms(500);

    // Frame rate control - normal mode
    lcd_write_cmd(ST7735_FRMCTR1);
    lcd_write_data(0x01);
    lcd_write_data(0x2C);
    lcd_write_data(0x2D);

    // Frame rate control - idle mode
    lcd_write_cmd(ST7735_FRMCTR2);
    lcd_write_data(0x01);
    lcd_write_data(0x2C);
    lcd_write_data(0x2D);

    // Frame rate control - partial mode
    lcd_write_cmd(ST7735_FRMCTR3);
    lcd_write_data(0x01);
    lcd_write_data(0x2C);
    lcd_write_data(0x2D);
    lcd_write_data(0x01);
    lcd_write_data(0x2C);
    lcd_write_data(0x2D);

    // Display inversion control
    lcd_write_cmd(ST7735_INVCTR);
    lcd_write_data(0x07);

    // Power control 1
    lcd_write_cmd(ST7735_PWCTR1);
    lcd_write_data(0xA2);
    lcd_write_data(0x02);
    lcd_write_data(0x84);

    // Power control 2
    lcd_write_cmd(ST7735_PWCTR2);
    lcd_write_data(0xC5);

    // Power control 3
    lcd_write_cmd(ST7735_PWCTR3);
    lcd_write_data(0x0A);
    lcd_write_data(0x00);

    // Power control 4
    lcd_write_cmd(ST7735_PWCTR4);
    lcd_write_data(0x8A);
    lcd_write_data(0x2A);

    // Power control 5
    lcd_write_cmd(ST7735_PWCTR5);
    lcd_write_data(0x8A);
    lcd_write_data(0xEE);

    // VCOM control
    lcd_write_cmd(ST7735_VMCTR1);
    lcd_write_data(0x0E);

    // Inversion off for GREENTAB
    lcd_write_cmd(ST7735_INVOFF);

    // Memory access control (orientation)
    lcd_write_cmd(ST7735_MADCTL);
    lcd_write_data(0x00);  // RGB order, no mirroring

    // 16-bit color mode (RGB565)
    lcd_write_cmd(ST7735_COLMOD);
    lcd_write_data(0x05);

    // Positive gamma correction
    lcd_write_cmd(ST7735_GMCTRP1);
    lcd_write_data(0x02);
    lcd_write_data(0x1C);
    lcd_write_data(0x07);
    lcd_write_data(0x12);
    lcd_write_data(0x37);
    lcd_write_data(0x32);
    lcd_write_data(0x29);
    lcd_write_data(0x2D);
    lcd_write_data(0x29);
    lcd_write_data(0x25);
    lcd_write_data(0x2B);
    lcd_write_data(0x39);
    lcd_write_data(0x00);
    lcd_write_data(0x01);
    lcd_write_data(0x03);
    lcd_write_data(0x10);

    // Negative gamma correction
    lcd_write_cmd(ST7735_GMCTRN1);
    lcd_write_data(0x03);
    lcd_write_data(0x1D);
    lcd_write_data(0x07);
    lcd_write_data(0x06);
    lcd_write_data(0x2E);
    lcd_write_data(0x2C);
    lcd_write_data(0x29);
    lcd_write_data(0x2D);
    lcd_write_data(0x2E);
    lcd_write_data(0x2E);
    lcd_write_data(0x37);
    lcd_write_data(0x3F);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    lcd_write_data(0x02);
    lcd_write_data(0x10);

    // Normal display mode on
    lcd_write_cmd(ST7735_NORON);
    sleep_ms(10);

    // Display on
    lcd_write_cmd(ST7735_DISPON);
    sleep_ms(100);
}

// ============================================================
// DRAWING PRIMITIVES
// ============================================================

static void lcd_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    uint8_t data[4];

    // Apply GREENTAB offsets
    x0 += GREENTAB_OFFSET_X;
    x1 += GREENTAB_OFFSET_X;
    y0 += GREENTAB_OFFSET_Y;
    y1 += GREENTAB_OFFSET_Y;

    lcd_write_cmd(ST7735_CASET);
    data[0] = 0x00;
    data[1] = x0;
    data[2] = 0x00;
    data[3] = x1;
    lcd_write_data_bulk(data, 4);

    lcd_write_cmd(ST7735_RASET);
    data[0] = 0x00;
    data[1] = y0;
    data[2] = 0x00;
    data[3] = y1;
    lcd_write_data_bulk(data, 4);

    lcd_write_cmd(ST7735_RAMWR);
}

void display_fill(uint16_t color) {
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    // Use chunked writes for speed
    uint8_t chunk[512];
    for (int i = 0; i < 256; i++) {
        chunk[i * 2] = hi;
        chunk[i * 2 + 1] = lo;
    }

    gpio_put(LCD_DC_PIN, 1);
    gpio_put(LCD_CS_PIN, 0);

    int total_pixels = LCD_WIDTH * LCD_HEIGHT;
    int chunks = total_pixels / 256;
    for (int i = 0; i < chunks; i++) {
        spi_write_blocking(spi1, chunk, 512);
    }

    gpio_put(LCD_CS_PIN, 1);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    uint8_t pixel[2] = {hi, lo};

    gpio_put(LCD_DC_PIN, 1);
    gpio_put(LCD_CS_PIN, 0);

    for (int i = 0; i < w * h; i++) {
        spi_write_blocking(spi1, pixel, 2);
    }

    gpio_put(LCD_CS_PIN, 1);
}

void display_pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    lcd_set_window(x, y, x, y);
    uint8_t pixel[2] = {color >> 8, color & 0xFF};
    lcd_write_data_bulk(pixel, 2);
}

void display_hline(int x, int y, int w, uint16_t color) {
    display_fill_rect(x, y, w, 1, color);
}

void display_vline(int x, int y, int h, uint16_t color) {
    display_fill_rect(x, y, 1, h, color);
}

void display_rect(int x, int y, int w, int h, uint16_t color) {
    display_hline(x, y, w, color);
    display_hline(x, y + h - 1, w, color);
    display_vline(x, y, h, color);
    display_vline(x + w - 1, y, h, color);
}

// ============================================================
// TEXT RENDERING
// ============================================================

static void display_draw_char(char c, int x, int y, uint16_t color, uint8_t scale) {
    const uint8_t* font_data;

    if (c >= 32 && c <= 90) {
        font_data = FONT_5X7[c - 32];
    } else if (c >= 97 && c <= 122) {
        font_data = FONT_LOWER[c - 97];
    } else {
        font_data = FONT_5X7[0];  // Space for unknown
    }

    for (int col = 0; col < 5; col++) {
        uint8_t col_bits = font_data[col];
        for (int row = 0; row < 7; row++) {
            if (col_bits & (1 << row)) {
                if (scale == 1) {
                    display_pixel(x + col, y + row, color);
                } else {
                    display_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void display_text(const char* text, int x, int y, uint16_t color, uint8_t scale) {
    while (*text) {
        display_draw_char(*text, x, y, color, scale);
        x += 6 * scale;
        text++;
    }
}

// ============================================================
// SYNTH UI ELEMENTS (matching MicroPython)
// ============================================================

// Graph area for waveform
#define GRAPH_X     10
#define GRAPH_Y     38
#define GRAPH_W     108
#define GRAPH_H     40

static SynthDisplay synth_display;

void synth_display_init(void) {
    display_init();

    synth_display.current_waveform = WAVE_SINE;
    synth_display.current_octave = 0;
    synth_display.volume = 50;
    synth_display.vibrato = 0;
    synth_display.filter = 80;
    synth_display.notes_playing = 0;

    // Clear screen
    display_fill(COLOR_BLACK);

    // Title
    display_text("SYNTHESIZER", 15, 5, COLOR_CYAN, 1);

    // Waveform graph frame
    display_rect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, COLOR_WHITE);

    // Labels
    display_text("Wave:", 5, 25, COLOR_WHITE, 1);
    display_text("Oct:", 85, 25, COLOR_WHITE, 1);

    // Potentiometer labels
    display_text("Vol", 3, 85, COLOR_WHITE, 1);
    display_text("Vib", 3, 100, COLOR_CYAN, 1);
    display_text("Flt", 3, 115, COLOR_MAGENTA, 1);

    // Notes label
    display_text("Notes:", 3, 132, COLOR_WHITE, 1);

    // Initial display updates
    synth_display_update_waveform();
    synth_display_update_octave();
    synth_display_update_volume(50);
    synth_display_update_vibrato(0);
    synth_display_update_filter(80);
    synth_display_update_notes(0);
}

static const char* waveform_names[] = {"SINE", "SAW", "SQUARE", "TRI"};

void synth_display_draw_waveform(void) {
    // Clear graph area
    display_fill_rect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, COLOR_BLACK);

    // Center line (dotted)
    int mid_y = GRAPH_Y + GRAPH_H / 2;
    for (int i = 0; i < GRAPH_W; i += 4) {
        display_pixel(GRAPH_X + i, mid_y, COLOR_CYAN);
    }

    // Draw waveform
    uint16_t color = COLOR_GREEN;
    int prev_y = -1;

    for (int x = 0; x < GRAPH_W; x++) {
        float y_val;

        switch (synth_display.current_waveform) {
            case WAVE_SINE: {
                float angle = (float)x / GRAPH_W * 4.0f * 3.14159f;
                y_val = (sinf(angle) + 1.0f) / 2.0f;
                break;
            }
            case WAVE_SQUARE:
                y_val = (x % (GRAPH_W / 2)) < (GRAPH_W / 4) ? 1.0f : 0.0f;
                break;
            case WAVE_SAW:
                y_val = (float)(x % (GRAPH_W / 2)) / (GRAPH_W / 2);
                break;
            case WAVE_TRIANGLE: {
                int period = GRAPH_W / 2;
                int pos = x % period;
                y_val = (pos < period / 2)
                    ? (2.0f * pos / period)
                    : (2.0f - 2.0f * pos / period);
                break;
            }
            default:
                y_val = 0.5f;
        }

        int y_pos = GRAPH_Y + (int)((1.0f - y_val) * (GRAPH_H - 1));

        // Draw line between points
        if (prev_y >= 0) {
            int y_start = (prev_y < y_pos) ? prev_y : y_pos;
            int y_end = (prev_y < y_pos) ? y_pos : prev_y;
            for (int y = y_start; y <= y_end; y++) {
                display_pixel(GRAPH_X + x, y, color);
            }
        } else {
            display_pixel(GRAPH_X + x, y_pos, color);
        }

        prev_y = y_pos;
    }
}

void synth_display_update_waveform(void) {
    // Clear waveform name area
    display_fill_rect(45, 23, 40, 10, COLOR_BLACK);

    uint16_t color;
    switch (synth_display.current_waveform) {
        case WAVE_SINE:     color = COLOR_GREEN; break;
        case WAVE_SAW:      color = COLOR_CYAN; break;
        case WAVE_SQUARE:   color = COLOR_YELLOW; break;
        case WAVE_TRIANGLE: color = COLOR_MAGENTA; break;
        default:            color = COLOR_WHITE;
    }

    display_text(waveform_names[synth_display.current_waveform], 45, 25, color, 1);
    synth_display_draw_waveform();
}

void synth_display_update_octave(void) {
    // Clear octave area
    display_fill_rect(110, 23, 18, 10, COLOR_BLACK);

    char oct_text[4];
    uint16_t color;

    if (synth_display.current_octave == -1) {
        strcpy(oct_text, "-1");
        color = COLOR_CYAN;
    } else if (synth_display.current_octave == 0) {
        strcpy(oct_text, "0");
        color = COLOR_GREEN;
    } else {
        strcpy(oct_text, "+1");
        color = COLOR_YELLOW;
    }

    display_text(oct_text, 110, 25, color, 1);
}

static void draw_bar(int y, int percent, uint16_t bar_color) {
    int bar_x = 25;
    int bar_w = 70;
    int bar_h = 6;

    // Frame
    display_rect(bar_x - 1, y - 1, bar_w + 2, bar_h + 2, COLOR_WHITE);

    // Clear fill area
    display_fill_rect(bar_x, y, bar_w, bar_h, COLOR_BLACK);

    // Draw fill
    int fill_w = (percent * bar_w) / 100;
    if (fill_w > 0) {
        display_fill_rect(bar_x, y, fill_w, bar_h, bar_color);
    }

    // Percentage text
    display_fill_rect(bar_x + bar_w + 3, y - 2, 30, 10, COLOR_BLACK);
    char percent_text[8];
    snprintf(percent_text, sizeof(percent_text), "%d%%", percent);
    display_text(percent_text, bar_x + bar_w + 4, y - 1, COLOR_WHITE, 1);
}

void synth_display_update_volume(int volume_percent) {
    synth_display.volume = volume_percent;
    uint16_t color = (volume_percent > 50) ? COLOR_GREEN :
                     (volume_percent > 20) ? COLOR_YELLOW : COLOR_RED;
    draw_bar(87, volume_percent, color);
}

void synth_display_update_vibrato(int vibrato_percent) {
    synth_display.vibrato = vibrato_percent;
    draw_bar(102, vibrato_percent, COLOR_CYAN);
}

void synth_display_update_filter(int filter_percent) {
    synth_display.filter = filter_percent;
    draw_bar(117, filter_percent, COLOR_MAGENTA);
}

void synth_display_update_notes(int note_count) {
    synth_display.notes_playing = note_count;

    // Clear notes area
    display_fill_rect(45, 130, 80, 10, COLOR_BLACK);

    if (note_count == 0) {
        display_text("---", 45, 132, COLOR_WHITE, 1);
    } else {
        uint16_t color = (note_count <= 3) ? COLOR_GREEN :
                         (note_count <= 6) ? COLOR_YELLOW : COLOR_RED;
        char text[4];
        snprintf(text, sizeof(text), "%d", note_count);
        display_text(text, 45, 132, color, 1);
    }
}

void synth_display_set_waveform(WaveformType waveform) {
    synth_display.current_waveform = waveform;
    synth_display_update_waveform();
}

void synth_display_set_octave(int8_t octave) {
    synth_display.current_octave = octave;
    synth_display_update_octave();
}

WaveformType synth_display_cycle_waveform(void) {
    synth_display.current_waveform = (synth_display.current_waveform + 1) % 4;
    synth_display_update_waveform();
    return synth_display.current_waveform;
}

int8_t synth_display_cycle_octave(void) {
    synth_display.current_octave++;
    if (synth_display.current_octave > 1) {
        synth_display.current_octave = -1;
    }
    synth_display_update_octave();
    return synth_display.current_octave;
}

WaveformType synth_display_get_waveform(void) {
    return synth_display.current_waveform;
}

int8_t synth_display_get_octave(void) {
    return synth_display.current_octave;
}

void synth_display_show_ready(bool ready) {
    // Show status at bottom of screen
    display_fill_rect(0, 148, 128, 12, COLOR_BLACK);
    if (ready) {
        display_text("READY", 45, 150, COLOR_GREEN, 1);
    } else {
        display_text("LOADING...", 30, 150, COLOR_YELLOW, 1);
    }
}
