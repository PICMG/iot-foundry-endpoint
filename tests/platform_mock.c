/**
 * @file platform_mock.c
 * @brief Mock platform serial I/O used by unit tests.
 *
 * Provides a lightweight in-memory implementation of the platform serial
 * primitives (`platform_serial_read_byte`, `platform_serial_write_byte`,
 * etc.) which tests drive to simulate RX/TX activity.
 *
 * @author Douglas Sandy
 *
 * MIT No Attribution
 *
 * Copyright (c) 2025 Doug
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdint.h>
#include <string.h>

/* Platform mock state */
static uint8_t tx_buffer[1024];
static uint16_t tx_len = 0;
static uint8_t can_write_state = 1; /* default: writable */
static uint8_t rx_buffer[1024];
static uint16_t rx_len = 0;
static uint16_t rx_pos = 0;

/**
 * @brief Initialize the mock platform state.
 *
 * Clears TX/RX buffers and resets positions.
 */
void platform_init() {
    tx_len = 0;
    rx_len = rx_pos = 0;
    memset(tx_buffer, 0, sizeof(tx_buffer));
    memset(rx_buffer, 0, sizeof(rx_buffer));
}

/**
 * @brief Query whether the mock RX buffer contains unread data.
 *
 * @return uint8_t Returns 1 if data is available to read, 0 otherwise.
 */
uint8_t platform_serial_has_data() {
    return (rx_pos < rx_len) ? 1 : 0;
}

/**
 * @brief Read a byte from the mock RX buffer.
 *
 * Advances the mock RX position. When no data is available this returns 0.
 *
 * @return uint8_t The next byte from the mock RX buffer or 0 when empty.
 */
uint8_t platform_serial_read_byte() {
    return (rx_pos < rx_len) ? rx_buffer[rx_pos++] : 0;
}

/**
 * @brief Write a byte to the mock TX buffer.
 *
 * This simulates the platform serial transmit primitive used by the
 * production code. Writes are appended into an in-memory buffer.
 *
 * @param byte The byte to write to the mock TX buffer.
 */
void platform_serial_write_byte(uint8_t byte) {
    if (tx_len < sizeof(tx_buffer)) tx_buffer[tx_len++] = byte;
    can_write_state++;
}

/**
 * @brief Query whether the mock serial interface can accept writes.
 *
 * @return uint8_t Returns non-zero when writes are currently allowed.
 */
uint8_t platform_serial_can_write() {
    return can_write_state < 5;
}

/* Test helpers for mock */

/**
 * @brief write a backpressure value into the mock transmitter.
 *
 * @return uint16_t Number of bytes in `tx_buffer`.
 */
void mock_set_can_write(uint8_t v) {
    can_write_state = v;
}

/**
 * @brief Return the number of bytes currently in the mock TX buffer.
 *
 * @return uint16_t Number of bytes in `tx_buffer`.
 */
uint16_t mock_tx_len(void) {
    return tx_len;
}

/**
 * @brief Return a pointer to the mock TX buffer contents.
 *
 * The returned pointer points into an internal buffer; do not modify it
 * from tests unless you know what you are doing.
 *
 * @return const uint8_t* Pointer to the mock TX buffer.
 */
const uint8_t* mock_tx_buffer(void) {
    return tx_buffer;
}

/**
 * @brief Clear the mock TX buffer.
 */
void mock_clear_tx(void) {
    tx_len = 0;
    memset(tx_buffer, 0, sizeof(tx_buffer));
}

/* RX helpers */

/**
 * @brief Set the mock RX buffer contents.
 *
 * Copies `len` bytes from `buf` into the mock RX buffer and resets
 * the read position to the start.
 *
 * @param buf Pointer to the data to copy into the mock RX buffer.
 * @param len Number of bytes to copy from `buf`.
 */
void mock_set_rx_buffer(const uint8_t* buf, uint16_t len) {
    if (len > sizeof(rx_buffer)) len = sizeof(rx_buffer);
    memcpy(rx_buffer, buf, len);
    rx_len = len;
    rx_pos = 0;
}

/**
 * @brief Clear the mock RX buffer and reset read position.
 */
void mock_clear_rx(void) {
    rx_len = rx_pos = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));
}

/**
 * @brief Return the current length of the mock RX buffer.
 *
 * @return uint16_t Number of bytes available in the mock RX buffer.
 */
uint16_t mock_rx_len(void) {
    return rx_len;
}
