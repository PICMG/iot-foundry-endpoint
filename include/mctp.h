/**
 * @file mctp.h
 * @brief Public MCTP API declarations and compile-time knobs.
 *
 * Minimal header shared by production code and unit tests.  Public
 * function declarations are provided here; heavy documentation for
 * implementations belongs in the C source files per project policy.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>

/* control message codes */
#define CONTROL_MSG_SET_ENDPOINT_ID 0x01
#define CONTROL_MSG_GET_ENDPOINT_ID 0x02
#define CONTROL_MSG_GET_MCTP_VERSION_SUPPORT 0x04
#define CONTROL_MSG_GET_MESSAGE_TYPE_SUPPORT 0x05

/* Control message completion codes */
#define CONTROL_COMPLETE_SUCCESS 0x00
#define CONTROL_COMPLETE_ERROR 0x01
#define CONTROL_COMPLETE_INVALID_DATA 0x02
#define CONTROL_COMPLETE_INVALID_LENGTH 0x03
#define CONTROL_COMPLETE_NOT_READY 0x04
#define CONTROL_COMPLETE_UNSUPPORTED_CMD 0x05
#define CONTROL_COMPLETE_COMMAND_SPECIFIC_START 0x80
#define CONTROL_COMPLETE_COMMAND_SPECIFIC_END 0xFF

/* function declarations */
void mctp_init(void);
void mctp_update(void);
uint8_t mctp_is_packet_available(void);
uint8_t mctp_is_control_packet(void);
uint8_t mctp_is_pldm_packet(void);
void mctp_process_control_message(void);
void mctp_ignore_packet(void);
int mctp_send_event(const uint8_t* data, uint16_t len);
uint8_t mctp_is_event_queue_empty(void);

/* Compile-time option to enable a single prioritized event TX slot.
 * Set to 1 to enable an extra TX buffer for endpoint-originated datagrams.
 * When enabled the event slot is preferred at frame boundaries (no byte
 * interleaving of frames is performed). Default is disabled (0).
 */
#ifndef MCTP_EVENT_TX_ENABLED
#define MCTP_EVENT_TX_ENABLED 0
#endif

/* Size of the event TX buffer when enabled. Defaults to the main MCTP buffer
 * size; can be overridden at compile time. */
#ifndef MCTP_EVENT_TX_BUF_SIZE
#define MCTP_EVENT_TX_BUF_SIZE 128
#endif


