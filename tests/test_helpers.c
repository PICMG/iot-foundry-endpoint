
/**
 * @file test_helpers.c
 * @brief Small test helpers for unit tests interacting with `mctp` internals.
 *
 * These helpers allow tests to prepare internal `mctp` buffers and state
 * without adding test-only hooks to production sources.
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
#include "../include/mctp.h"

/* For test-only access to internal mctp state, the variables are non-static when
 * compiled with UNIT_TEST. Declare them here as extern so tests can set up the
 * transmit buffer without embedding helpers in production code.
 */
extern uint8_t mctp_buffer[];
extern uint8_t byte_count;
extern uint8_t buffer_idx;
extern uint8_t rxState;

/**
 * @brief Test helper to populate the internal `mctp` buffer with a frame.
 *
 * Copies `len` bytes from `frame` into the internal `mctp_buffer`, sets
 * the length field if appropriate, and marks the framer state so the
 * test harness can process the prepared frame.
 *
 * @param frame Pointer to the logical (unescaped) frame bytes.
 * @param len Length of the frame in bytes.
 */
void test_prepare_frame(const uint8_t* frame, uint16_t len) {
    const uint16_t MAX_BUF = 256;
    if (len > MAX_BUF) len = MAX_BUF;
    for (uint16_t i = 0; i < len; ++i) mctp_buffer[i] = frame[i];
    /* set byte count field if length is large enough */
    if (len > 2) {
        uint8_t body = (uint8_t)((len >= 6) ? (len - 6) : 0);
        mctp_buffer[2] = body; /* OFFSET_BYTE_COUNT */
    }
    /* mark that a packet is available to be sent */
    rxState = 8; /* MCTPSER_AWAITING_RESPONSE */
    buffer_idx = len;
}
