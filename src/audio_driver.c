/**
 * audio_driver.c - Clean I2S audio driver using PIO + DMA
 *
 * Properly synchronized double-buffered audio for MAX98357A
 * Uses interrupt-driven DMA with clean handoff between buffers
 */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include <string.h>

#include "synth.h"
#include "i2s.pio.h"

// Audio configuration
#define AUDIO_SAMPLE_RATE   22050   // Lower sample rate for reduced CPU load
#define AUDIO_BUFFER_FRAMES 256
#define NUM_BUFFERS         3       // Triple buffering for safety

// Pin definitions (matching MicroPython)
#define I2S_DATA_PIN    18  // DIN
#define I2S_BCLK_PIN    16  // BCLK (LRCLK = 17)

// Audio buffer structure
typedef struct {
    int32_t samples[AUDIO_BUFFER_FRAMES];  // Packed stereo samples
    volatile bool ready;                    // Buffer has valid data
    volatile bool playing;                  // Currently being played by DMA
} AudioBuffer;

static AudioBuffer buffers[NUM_BUFFERS];
static volatile int current_play_buffer = 0;
static volatile int current_fill_buffer = 0;
static volatile bool audio_active = false;

// PIO and DMA
static PIO audio_pio;
static uint audio_sm;
static int dma_chan = -1;

// Synth pointer (optional)
static Synth* audio_synth = NULL;

// Spin lock for thread safety
static spin_lock_t* audio_spin_lock;

// ============================================================
// DMA INTERRUPT HANDLER
// ============================================================

static void __isr __time_critical_func(dma_irq_handler)(void) {
    // Clear interrupt
    dma_hw->ints0 = 1u << dma_chan;

    if (!audio_active) return;

    // Current buffer finished
    int finished = current_play_buffer;
    buffers[finished].playing = false;
    buffers[finished].ready = false;

    // Find next ready buffer
    bool found = false;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        int idx = (finished + 1 + i) % NUM_BUFFERS;
        if (buffers[idx].ready && !buffers[idx].playing) {
            current_play_buffer = idx;
            buffers[idx].playing = true;

            // Start DMA transfer
            dma_channel_set_read_addr(dma_chan, buffers[idx].samples, true);
            found = true;
            break;
        }
    }

    // If no buffer ready, output silence (underrun)
    if (!found) {
        // Keep current_play_buffer but don't start DMA
        // The fill function will restart it
    }
}

// ============================================================
// INITIALIZATION
// ============================================================

void audio_init(Synth* synth) {
    audio_synth = synth;

    // Get spin lock
    int spin_lock_num = spin_lock_claim_unused(true);
    audio_spin_lock = spin_lock_init(spin_lock_num);

    // Initialize buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        memset(buffers[i].samples, 0, sizeof(buffers[i].samples));
        buffers[i].ready = false;
        buffers[i].playing = false;
    }

    // Setup PIO for I2S
    audio_pio = pio0;
    audio_sm = pio_claim_unused_sm(audio_pio, true);

    uint offset = pio_add_program(audio_pio, &i2s_out_program);
    i2s_program_init(audio_pio, audio_sm, offset,
                     I2S_DATA_PIN, I2S_BCLK_PIN,
                     AUDIO_SAMPLE_RATE);

    // Setup DMA
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(audio_pio, audio_sm, true));

    dma_channel_configure(
        dma_chan,
        &c,
        &audio_pio->txf[audio_sm],
        buffers[0].samples,
        AUDIO_BUFFER_FRAMES,
        false
    );

    // Setup DMA interrupt
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Pre-fill first buffer with silence and start
    buffers[0].ready = true;
    buffers[0].playing = true;
    current_play_buffer = 0;
    current_fill_buffer = 1;
    audio_active = true;

    // Start DMA
    dma_channel_set_read_addr(dma_chan, buffers[0].samples, true);
}

// ============================================================
// BUFFER MANAGEMENT
// ============================================================

// Pack stereo 16-bit samples into 32-bit I2S format
static inline int32_t pack_samples(int16_t left, int16_t right) {
    return ((int32_t)left << 16) | ((uint16_t)right);
}

// Write a buffer of audio samples
// buffer: AUDIO_BUFFER_FRAMES * 2 interleaved int16_t samples
void audio_write_buffer(const int16_t* buffer) {
    // Find a free buffer (not playing, not ready)
    int target = -1;

    // Try to find next buffer in sequence
    for (int i = 0; i < NUM_BUFFERS; i++) {
        int idx = (current_fill_buffer + i) % NUM_BUFFERS;
        if (!buffers[idx].ready && !buffers[idx].playing) {
            target = idx;
            break;
        }
    }

    // If all buffers busy, wait for one
    if (target < 0) {
        // Wait for any buffer to become available
        for (int tries = 0; tries < 1000; tries++) {
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (!buffers[i].ready && !buffers[i].playing) {
                    target = i;
                    break;
                }
            }
            if (target >= 0) break;
            sleep_us(50);
        }
    }

    if (target < 0) {
        // Emergency: overwrite buffer 0
        target = 0;
        buffers[0].ready = false;
    }

    // Fill the buffer
    for (int i = 0; i < AUDIO_BUFFER_FRAMES; i++) {
        buffers[target].samples[i] = pack_samples(
            buffer[i * 2],
            buffer[i * 2 + 1]
        );
    }

    // Mark as ready (atomic)
    uint32_t save = spin_lock_blocking(audio_spin_lock);
    buffers[target].ready = true;
    current_fill_buffer = (target + 1) % NUM_BUFFERS;

    // If DMA stopped, restart it
    if (!dma_channel_is_busy(dma_chan) && audio_active) {
        // Find a ready buffer to play
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (buffers[i].ready && !buffers[i].playing) {
                buffers[i].playing = true;
                current_play_buffer = i;
                dma_channel_set_read_addr(dma_chan, buffers[i].samples, true);
                break;
            }
        }
    }
    spin_unlock(audio_spin_lock, save);
}

// Alternative: render using synth engine
void audio_update(void) {
    if (!audio_synth) return;

    int16_t temp[AUDIO_BUFFER_FRAMES * 2];
    synth_render(audio_synth, temp, AUDIO_BUFFER_FRAMES);
    audio_write_buffer(temp);
}

// ============================================================
// CONTROL
// ============================================================

void audio_stop(void) {
    audio_active = false;
    dma_channel_abort(dma_chan);
    pio_sm_set_enabled(audio_pio, audio_sm, false);
}

uint32_t audio_get_sample_rate(void) {
    return AUDIO_SAMPLE_RATE;
}

uint32_t audio_get_buffer_size(void) {
    return AUDIO_BUFFER_FRAMES;
}
