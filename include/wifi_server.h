/**
 * WiFi Server for Pico Synth
 *
 * Creates an Access Point and serves a web interface
 * for displaying synth status and waveform visualization.
 */

#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <stdbool.h>
#include <stdint.h>

// WiFi AP configuration
#define WIFI_SSID "PicoSynth"
#define WIFI_PASSWORD "synth1234"  // Min 8 characters for WPA2
#define WIFI_CHANNEL 6

// Server configuration
#define HTTP_PORT 80

/**
 * Initialize WiFi in Access Point mode and start the HTTP server.
 *
 * @return true if initialization successful, false otherwise
 */
bool wifi_server_init(void);

/**
 * Poll the WiFi stack and handle any pending HTTP requests.
 * Must be called regularly from the main loop.
 */
void wifi_server_poll(void);

/**
 * Check if WiFi is initialized and running.
 *
 * @return true if WiFi server is active
 */
bool wifi_server_is_running(void);

/**
 * Get the IP address of the access point.
 *
 * @return IP address string (e.g., "192.168.4.1")
 */
const char* wifi_server_get_ip(void);

#endif // WIFI_SERVER_H
