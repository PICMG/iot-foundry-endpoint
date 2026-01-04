/**
 * @file mctp.c
 * @brief MCTP framer and control message processing implementation.
 *
 * Implements a minimal MCTP framer for serial transport, and handlers for
 * MCTP control messages (set/get endpoint id, version support, message type
 * support).
 *
 * Endpoint Operational constraints and assumptions:
 *   - The endpoint is single-threaded.  It only processes one packet at a time.
 *       - if a new packet is received while processing a previous packet, the new packet will be
 *         silently discarded.
 *       - this allows us to reduce overall buffer requirements.
 *   - The endpoint responds to requests only - it does not initiate any requests on its own.
 *     Although it may send datagram messages triggered by events.
 *   - The following MCTP control requests are supported:
 *       - set endpoint id
 *       - get endpoint id
 *       - get version support
 *       - get message type support
 *   - The endpoint connects to one bus only
 *   - The endpoint does not support the "discovered" flag for endpoint IDs.
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
#include "mctp.h"

#include <stdint.h>

#include "platform.h"

#ifdef PLDM_SUPPORT
#include "pldm_version.h"
#endif

/* forward declarations for private functions */
uint8_t mctp_send_frame(void);
uint16_t calc_fcs(uint16_t f, uint8_t* cp, int len);

/* transmission unit and buffer size management */
#define BASELINE_TRANSMISSION_UNIT 64
#define MCTP_BUFFER_SIZE (BASELINE_TRANSMISSION_UNIT + 6)  // add space for framing bytes and header

/* framer state definitions (single source of truth) */
#include "mctp_framer_states.h"

/* offsets for data within MCTP packet */
#define OFFSET_MSG_MCTP_PROTOCOL_VERSION 1
#define OFFSET_BYTE_COUNT 2
#define OFFSET_MCTP_HEADER_VERSION 3
#define OFFSET_DESTINATION_ENDPOINT_ID 4
#define OFFSET_SOURCE_ENDPOINT_ID 5
#define OFFSET_FLAGS 6
#define OFFSET_MSG_TYPE 7
#define OFFSET_CTRL_INSTANCE_ID 8
#define OFFSET_CTRL_COMMAND_CODE 9
#define OFFSET_CTRL_COMPLETION_CODE 10

/* framing characters */
#define FRAME_CHAR 0x7E
#define ESCAPE_CHAR 0x7D

/* device configuration */
static uint8_t endpoint_id = 0x00;             // set to unprogrammed
static uint8_t byte_count;                     // body bytes left to receive for current frame
#ifdef UNIT_TEST
uint8_t buffer_idx;                     /* index into the receive buffer (exposed to tests) */
uint8_t rxState;                        /* current framer state (exposed to tests) */
uint8_t mctp_buffer[MCTP_BUFFER_SIZE];  /* transmission/reception buffer (exposed to tests) */
#else
static uint8_t buffer_idx;                     // index into the receive buffer
static uint8_t rxState;                        // current framer state
static uint8_t mctp_buffer[MCTP_BUFFER_SIZE];  // transmission/reception buffer
#endif

/* send state for reentrant transmit */
static uint16_t send_total_len = 0;
static uint16_t send_idx = 0;
static uint8_t send_in_progress = 0;
static uint8_t send_escape_pending = 0;
static uint8_t send_pending_byte = 0;

/* Optional single prioritized event TX slot */
#if MCTP_EVENT_TX_ENABLED
static uint8_t tx_buf_event[MCTP_EVENT_TX_BUF_SIZE];
static uint16_t tx_event_len = 0;
static uint16_t tx_event_idx = 0;
static uint8_t tx_event_pending = 0;
static uint8_t tx_event_escape_pending = 0;
static uint8_t tx_event_pending_byte = 0;
#endif

/* current active tx slot: 0 = none, 1 = primary (mctp_buffer), 2 = event */
static uint8_t current_tx_slot = 0;

/* FCS calculation moved to src/fcs.c for testability */
#include "fcs.h"

/**********************************************************************************
 * static functions.  These are only visible within this file.
 **********************************************************************************/

/**
 * @brief Calculate Frame Check Sequence (FCS) for a byte buffer.
 *
 * @param fcs Initial FCS value (typically @c INITFCS).
 * @param cp Pointer to the byte buffer to compute the FCS over.
 * @param len Number of bytes in the buffer to include in the calculation.
 * @return uint16_t The updated FCS value after processing the buffer.
 */
/* calc_fcs() implemented in src/fcs.c */

/**
 * @brief Validate the most recently received MCTP frame.
 *
 * This checks that the received buffer contains a minimally-sized
 * frame, that the length field matches the received size, and that
 * the calculated FCS matches the frame FCS.
 *
 * @return uint8_t Returns 1 if the received frame is valid, 0 otherwise.
 */
static uint8_t validate_rx() {
    // minimum valid frame is 11 bytes:
    if (buffer_idx < 11) return 0;

    // get the byte count from the length field
    byte_count = mctp_buffer[2];

    // verify the byte count matches the received length
    if ((uint16_t)byte_count != (uint16_t)buffer_idx - 6) return 0;

    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, buffer_idx - 4);

    // get the expected FCS from the message
    uint16_t msg_fcs = mctp_buffer[buffer_idx - 3];
    msg_fcs = msg_fcs << 8;
    msg_fcs += mctp_buffer[buffer_idx - 2];

    // return the result of the comparison
    return msg_fcs == fcs;
}

/**
 * @brief Handle a Set Endpoint ID control request.
 *
 * Reads the request payload, validates the requested endpoint ID and
 * produces an appropriate response which is transmitted using
 * @c mctp_send_frame().
 *
 */
static void process_set_endpoint_id_control_message() {
    // dont process packet if not ready
    if (!mctp_is_packet_available()) return;

    // get the requested endpoint id from the message payload
    uint16_t idx = OFFSET_CTRL_COMPLETION_CODE;
    uint8_t operation = mctp_buffer[idx++] & 0x02;
    uint8_t eid = mctp_buffer[idx++];
    uint8_t completion_code;
    uint8_t endpoint_acceptance_status = 0x10;  // EID rejected by default
    if (operation == 0x02) {
        // this is a request to reset static EID value.  Since this endpoint does
        // not support static ID values, the proper response is to send an
        // ERROR_INVALID_DATA response
        completion_code = CONTROL_COMPLETE_INVALID_DATA;
    } else if (operation == 0x03) {
        // this is a request to set discovery flag.  Since this endpoint does
        // not support discovery flag, the proper response is to send an
        // ERROR_INVALID_DATA response
        completion_code = CONTROL_COMPLETE_INVALID_DATA;
    } else {
        if ((eid == 0x00) || (eid == 0xff)) {
            // These are invalid EID values
            completion_code = CONTROL_COMPLETE_INVALID_DATA;
        } else {
            completion_code = CONTROL_COMPLETE_SUCCESS;
            endpoint_acceptance_status = 0x00;  // EID accepted
        }
    }

    // message body for response
    idx = OFFSET_CTRL_COMPLETION_CODE;
    mctp_buffer[idx++] = completion_code;
    mctp_buffer[idx++] = endpoint_acceptance_status;
    mctp_buffer[idx++] = endpoint_id;
    mctp_buffer[idx++] = 0x00;  // eid pool size

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // toggle the Tag Owner (TO) bit for responses
    mctp_buffer[OFFSET_FLAGS] ^= 0x08;

    //===========
    // set the som/eom bits to indicate single frame response
    mctp_buffer[OFFSET_FLAGS] |= 0xC0;

    //===========
    // updates to the media independent header
    // reverse the source and destination EID values
    uint8_t source_eid = mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID];
    uint8_t dest_eid = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
    mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID] = dest_eid;
    mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID] = source_eid;

    // recalculate the byte count
    mctp_buffer[OFFSET_BYTE_COUNT] = idx - OFFSET_BYTE_COUNT - 1;

    //==========
    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, idx);
    mctp_buffer[idx++] = (fcs >> 8);
    mctp_buffer[idx++] = (fcs & 0x00FF);

    // add the frame end character
    mctp_buffer[idx++] = FRAME_CHAR;

    mctp_send_frame();

    // set the endpoint id
    if (completion_code == CONTROL_COMPLETE_SUCCESS) {
        endpoint_id = eid;
    }
}

/**
 * @brief Handle a Get Endpoint ID control request.
 *
 * Fills the response payload with the current endpoint ID and
 * sends the response frame.
 *
 */
void process_get_endpoint_id_control_message() {
    // dont process packet if not ready
    if (!mctp_is_packet_available()) return;

    uint16_t idx = OFFSET_CTRL_COMPLETION_CODE;
    mctp_buffer[idx++] = CONTROL_COMPLETE_SUCCESS;
    mctp_buffer[idx++] = endpoint_id;
    mctp_buffer[idx++] = 0x00;  // endpoint type = simple endpoint;

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // updates to the media independent header
    // reverse the source and destination EID values
    uint8_t source_eid = mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID];
    uint8_t dest_eid = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
    mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID] = dest_eid;
    mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID] = source_eid;

    // recalculate the byte count
    mctp_buffer[OFFSET_BYTE_COUNT] = idx - OFFSET_BYTE_COUNT - 1;

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // toggle the Tag Owner (TO) bit for responses
    mctp_buffer[OFFSET_FLAGS] ^= 0x08;

    //===========
    // set the som/eom bits to indicate single frame response
    mctp_buffer[OFFSET_FLAGS] |= 0xC0;

    //==========
    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, idx);
    mctp_buffer[idx++] = (fcs >> 8);
    mctp_buffer[idx++] = (fcs & 0x00FF);

    // add the frame end character
    mctp_buffer[idx++] = FRAME_CHAR;

    mctp_send_frame();
}

/**
 * @brief Handle a Get MCTP Version Support control request.
 *
 * Determines the supported version(s) for the requested message type
 * and constructs a response containing version entries.
 *
 */
static void process_get_mctp_version_support_control_message() {
    // dont process packet if not ready
    if (!mctp_is_packet_available()) return;

    // get the message type from the message payload
    uint8_t msg_type = mctp_buffer[OFFSET_CTRL_COMPLETION_CODE];
    uint16_t idx = OFFSET_CTRL_COMPLETION_CODE;
    if (msg_type == 0x00) {
        // control protocol message version information
        mctp_buffer[idx++] = CONTROL_COMPLETE_SUCCESS;
        mctp_buffer[idx++] = 1;  // version entry count
        // current version of the specification (1.3.1)
        mctp_buffer[idx++] = 0x01;  // major version
        mctp_buffer[idx++] = 0x03;  // minor version
        mctp_buffer[idx++] = 0x01;  // update version
        mctp_buffer[idx++] = 0x00;  // alpha version
    } else if (msg_type == 0xff) {
        // base specification version information
        mctp_buffer[idx++] = CONTROL_COMPLETE_SUCCESS;
        mctp_buffer[idx++] = 1;  // version entry count
        // current version of the specification (1.3.1)
        mctp_buffer[idx++] = 0x01;  // major version
        mctp_buffer[idx++] = 0x03;  // minor version
        mctp_buffer[idx++] = 0x01;  // update version
        mctp_buffer[idx++] = 0x00;  // alpha version
    }
#ifdef PLDM_SUPPORT
    else if (msg_type == 0x01) {
        // MCTP message type for pldm support
        mctp_rx_buffer[idx++] = CONTROL_COMPLETE_SUCCESS;
        mctp_rx_buffer[idx++] = 1;  // version entry count
        // current version of the specification (1.3.1)
        mctp_rx_buffer[idx++] = pldm_major_version;   // major version
        mctp_rx_buffer[idx++] = pldm_minor_version;   // minor version
        mctp_rx_buffer[idx++] = pldm_update_version;  // update version
        mctp_rx_buffer[idx++] = 0x00;                 // alpha version
    }
#endif
    else {
        // unsupported message type
        mctp_buffer[idx++] = 0x80;  // message type number not supported
        mctp_buffer[idx++] = 0x00;  // version number entry count = 0;
    }

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // toggle the Tag Owner (TO) bit for responses
    mctp_buffer[OFFSET_FLAGS] ^= 0x08;

    //===========
    // set the som/eom bits to indicate single frame response
    mctp_buffer[OFFSET_FLAGS] |= 0xC0;

    //===========
    // updates to the media independent header
    // reverse the source and destination EID values
    uint8_t source_eid = mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID];
    uint8_t dest_eid = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
    mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID] = dest_eid;
    mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID] = source_eid;

    // recalculate the byte count
    mctp_buffer[OFFSET_BYTE_COUNT] = idx - OFFSET_BYTE_COUNT - 1;

    //==========
    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, idx);
    mctp_buffer[idx++] = (fcs >> 8);
    mctp_buffer[idx++] = (fcs & 0x00FF);

    // add the frame end character
    mctp_buffer[idx++] = FRAME_CHAR;

    mctp_send_frame();
}

/**
 * @brief Handle a Get Message Type Support control request.
 *
 * Responds with a list of MCTP message types supported by this
 * endpoint (control, PLDM if enabled, etc.).
 *
 */
void process_get_message_type_support_control_message() {
    // dont process packet if not ready
    if (!mctp_is_packet_available()) return;

    uint16_t idx = OFFSET_CTRL_COMPLETION_CODE;
    // control protocol message version information
    mctp_buffer[idx++] = CONTROL_COMPLETE_SUCCESS;
    mctp_buffer[idx++] = 4;  // total message types supported
    mctp_buffer[idx++] = CONTROL_MSG_SET_ENDPOINT_ID;
    mctp_buffer[idx++] = CONTROL_MSG_GET_ENDPOINT_ID;
    mctp_buffer[idx++] = CONTROL_MSG_GET_MCTP_VERSION_SUPPORT;
    mctp_buffer[idx++] = CONTROL_MSG_GET_MESSAGE_TYPE_SUPPORT;

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // toggle the Tag Owner (TO) bit for responses
    mctp_buffer[OFFSET_FLAGS] ^= 0x08;

    //===========
    // set the som/eom bits to indicate single frame response
    mctp_buffer[OFFSET_FLAGS] |= 0xC0;

    //===========
    // updates to the media independent header
    // reverse the source and destination EID values
    uint8_t source_eid = mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID];
    uint8_t dest_eid = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
    mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID] = dest_eid;
    mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID] = source_eid;

    // recalculate the byte count
    mctp_buffer[OFFSET_BYTE_COUNT] = idx - OFFSET_BYTE_COUNT - 1;

    //==========
    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, idx);
    mctp_buffer[idx++] = (fcs >> 8);
    mctp_buffer[idx++] = (fcs & 0x00FF);

    // add the frame end character
    mctp_buffer[idx++] = FRAME_CHAR;

    mctp_send_frame();
}

/**
 * @brief Handle an unsupported control command.
 *
 * Sends an unsupported command response back to the requester.
 *
 */
static void process_unsupported_control_message() {
    // dont process packet if not ready
    if (!mctp_is_packet_available()) return;

    uint16_t idx = OFFSET_CTRL_COMPLETION_CODE;
    mctp_buffer[idx++] = CONTROL_COMPLETE_UNSUPPORTED_CMD;

    //===========
    // updates to the control message header
    // clear the rq bit in the instance id byte
    mctp_buffer[OFFSET_CTRL_INSTANCE_ID] &= ~0x80;

    //===========
    // toggle the Tag Owner (TO) bit for responses
    mctp_buffer[OFFSET_FLAGS] ^= 0x08;

    //===========
    // set the som/eom bits to indicate single frame response
    mctp_buffer[OFFSET_FLAGS] |= 0xC0;

    //===========
    // updates to the media independent header
    // reverse the source and destination EID values
    uint8_t source_eid = mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID];
    uint8_t dest_eid = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
    mctp_buffer[OFFSET_SOURCE_ENDPOINT_ID] = dest_eid;
    mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID] = source_eid;

    // recalculate the byte count
    mctp_buffer[OFFSET_BYTE_COUNT] = idx - OFFSET_BYTE_COUNT - 1;

    //==========
    // calculate the FCS
    uint16_t fcs = calc_fcs(INITFCS, mctp_buffer + 1, idx);
    mctp_buffer[idx++] = (fcs >> 8);
    mctp_buffer[idx++] = (fcs & 0x00FF);

    // add the frame end character
    mctp_buffer[idx++] = FRAME_CHAR;

    mctp_send_frame();
}

/**********************************************************************************
 * public functions.  These are visible outside this file.
 **********************************************************************************/
/**
 * @brief Initialize MCTP framer state.
 *
 * Resets the receiver state machine and buffer index to prepare for
 * receiving frames.  Initializes platform hardware as needed.
 *
 */
void mctp_init() {
    rxState = MCTPSER_WAITING_FOR_SYNC;
    buffer_idx = 0;

    /* Set up mctp-related hardware */
    platform_init();
}

/**
 * @brief Process incoming serial data and advance the framer state.
 *
 * Called regularly from the main loop; reads bytes from the platform
 * serial interface when available and updates internal receive state.
 *
 */
void mctp_update() {
    uint8_t byte_value;
    if (!platform_serial_has_data()) {
        return;
    }
    byte_value = platform_serial_read_byte();
    switch (rxState) {
        case MCTPSER_WAITING_FOR_SYNC:
            if (byte_value == FRAME_CHAR) {
                byte_count = 0;
                buffer_idx = 0;
                mctp_buffer[buffer_idx++] = FRAME_CHAR;
                rxState = MCTPSER_HEADER1;
            }
            break;
        case MCTPSER_HEADER1:
            // this should have the protocol version byte.  Just add it to the buffer
            mctp_buffer[buffer_idx++] = byte_value;
            rxState = MCTPSER_HEADER2;
            break;
        case MCTPSER_HEADER2:
            // this should have the length byte.  Add it to the buffer
            mctp_buffer[buffer_idx++] = byte_value;
            byte_count = byte_value;  // number of bytes in the body

            // if the body size will push the buffer over its limit, drop the frame
            if ((uint16_t)(byte_count + buffer_idx + 5) > MCTP_BUFFER_SIZE) {
                rxState = MCTPSER_WAITING_FOR_SYNC;
                break;
            }
            rxState = MCTPSER_BODY;
            break;
        case MCTPSER_BODY:
            if (byte_value == ESCAPE_CHAR) {
                // the next byte is escaped and needs to be unescaped
                rxState = MCTPSER_ESCAPE;
                break;
            } else if (byte_value == FRAME_CHAR) {
                // unexpected FRAME_CHAR - restart frame
                byte_count = 0;
                buffer_idx = 0;
                mctp_buffer[buffer_idx++] = FRAME_CHAR;
                rxState = MCTPSER_HEADER1;
                break;
            } else {
                // this is a regular byte - add it to the buffer
                mctp_buffer[buffer_idx++] = byte_value;
                // keep track of how many bytes are left in the body
                byte_count--;
                if (byte_count == 0) {
                    rxState = MCTPSER_FCS1;
                }
            }
            break;
        case MCTPSER_FCS1:
            mctp_buffer[buffer_idx++] = byte_value;
            rxState = MCTPSER_FCS2;
            break;
        case MCTPSER_FCS2:
            mctp_buffer[buffer_idx++] = byte_value;
            rxState = MCTPSER_END;
            break;
        case MCTPSER_END:
            if (byte_value != FRAME_CHAR) {
                // invalid end of frame - drop it
                rxState = MCTPSER_WAITING_FOR_SYNC;
                break;
            }
            mctp_buffer[buffer_idx++] = byte_value;

            // complete frame received - validate it
            if (validate_rx()) {
                /* Only accept frames addressed to this endpoint (or broadcast/all endpoints)
                   Destination EID must be 0x00 (broadcast), 0xFF (all endpoints),
                   or match the configured `endpoint_id`. Otherwise drop the frame. */
                uint8_t dest = mctp_buffer[OFFSET_DESTINATION_ENDPOINT_ID];
                if ((dest == 0x00) || (dest == 0xFF) || (dest == endpoint_id)) {
                    rxState = MCTPSER_AWAITING_RESPONSE;
                } else {
                    rxState = MCTPSER_WAITING_FOR_SYNC;
                }
            } else {
                rxState = MCTPSER_WAITING_FOR_SYNC;
            }
            break;
        case MCTPSER_ESCAPE:
            if ((byte_value == (ESCAPE_CHAR - 0x20)) || (byte_value == (FRAME_CHAR - 0x20))) {
                byte_value = (uint8_t)(byte_value + 0x20);
                mctp_buffer[buffer_idx++] = byte_value;
                byte_count--;
                if (byte_count == 0) {
                    rxState = MCTPSER_FCS1;
                } else {
                    rxState = MCTPSER_BODY;
                }
                break;
            } else if (byte_value == FRAME_CHAR) {
                // UNEXPECTED FRAME_CHAR - restart frame
                byte_count = 0;
                buffer_idx = 0;
                mctp_buffer[buffer_idx++] = FRAME_CHAR;
                rxState = MCTPSER_HEADER1;
            } else {
                // invalid escape sequence - drop frame
                rxState = MCTPSER_WAITING_FOR_SYNC;
            }
            break;
        case MCTPSER_AWAITING_RESPONSE:
            // here if we are waiting for the response to be sent
            // this state transition will occur at first beginning of an mctp_send_frame()
            // call
        case SENDING_RESPONSE:
            // continue sending the response frame
            // this state transition will occur after the frame has been completely sent.
            mctp_send_frame();
            break;
    }
}

/**
 * @brief Query whether a complete MCTP packet is available.
 *
 * @return uint8_t Returns 1 if a complete packet is available, 0 otherwise.
 */
uint8_t mctp_is_packet_available() {
    return rxState == MCTPSER_AWAITING_RESPONSE;
}

/**
 * @brief Determine if the available packet is a control packet.
 *
 * Control packets have message type 0x00 in the message type field.
 *
 * @return uint8_t Returns 1 if the available packet is a control packet, 0 otherwise.
 */
uint8_t mctp_is_control_packet() {
    // control packets have message type 0x00 in byte 0 (after the initial FRAME_CHAR)
    return (mctp_buffer[OFFSET_MSG_TYPE] & 0x0F) == 0x00;
}

/**
 * @brief Determine if the available packet is a pldm packet.
 *
 * Pldm packets have message type 0x01 in the message type field.
 *
 * @return uint8_t Returns 1 if the available packet is a pldm packet, 0 otherwise.
 */
uint8_t mctp_is_pldm_packet() {
    // pldm packets have message type 0x01 in byte 0 (after the initial FRAME_CHAR)
    return (mctp_buffer[OFFSET_MSG_TYPE] & 0x0F) == 0x01;
}

/**
 * @brief Ignore the current packet and reset the framer.
 *
 * Resets the receiver state so the next incoming frame can be processed.
 *
 */
void mctp_ignore_packet() {
    // simply reset the framer state to wait for the next packet
    rxState = MCTPSER_WAITING_FOR_SYNC;
}

/**
 * @brief Dispatch the received control message to the appropriate handler.
 *
 * Examines the control command code and calls the matching
 * process_* helper to build and send the response.
 *
 */
void mctp_process_control_message() {
    if (mctp_buffer[OFFSET_CTRL_COMMAND_CODE] == CONTROL_MSG_SET_ENDPOINT_ID) {
        process_set_endpoint_id_control_message();
    } else if (mctp_buffer[OFFSET_CTRL_COMMAND_CODE] == CONTROL_MSG_GET_ENDPOINT_ID) {
        process_get_endpoint_id_control_message();
    } else if (mctp_buffer[OFFSET_CTRL_COMMAND_CODE] == CONTROL_MSG_GET_MCTP_VERSION_SUPPORT) {
        process_get_mctp_version_support_control_message();
    } else if (mctp_buffer[OFFSET_CTRL_COMMAND_CODE] == CONTROL_MSG_GET_MESSAGE_TYPE_SUPPORT) {
        process_get_message_type_support_control_message();
    } else {
        // unsupported command - ignore it for now
        process_unsupported_control_message();
    }
}

/**
 * @brief Send the response frame found within the mctp_buffer.
 *
 * - will attempt to write as many bytes as platform_serial_can_write() allows
 * - caller should call repeatedly (mctp_update will call when awaiting response)
 *
 * @return uint8_t the number of bytes sent in this call.
 *
 */
uint8_t mctp_send_frame() {
    uint8_t bytes_sent = 0;

    /* If no active slot, select one. Priority: event slot (if pending) then primary response. */
    if (current_tx_slot == 0) {
#if MCTP_EVENT_TX_ENABLED
        if (tx_event_pending) {
            current_tx_slot = 2;         /* start event transmit */
            tx_event_idx = tx_event_idx; /* keep existing index if resuming */
        } else
#endif
            if (rxState == MCTPSER_AWAITING_RESPONSE) {
            /* initialize primary response transmit */
            uint16_t body_size = mctp_buffer[OFFSET_BYTE_COUNT];
            send_total_len = (uint16_t)body_size + 6; /* header + body + fcs + trailer */
            send_idx = 0;
            send_escape_pending = 0;
            rxState = SENDING_RESPONSE;
            current_tx_slot = 1;
        } else {
            return 0; /* nothing to send */
        }
    }

    /* send bytes while the platform indicates writes are possible for the active slot */
    while (1) {
        /* determine active slot length/check */
        if (current_tx_slot == 1) {
            if (send_idx >= send_total_len) break;
        }
#if MCTP_EVENT_TX_ENABLED
        else if (current_tx_slot == 2) {
            if (tx_event_idx >= tx_event_len) break;
        }
#endif

        if (!platform_serial_can_write()) {
            /* cannot write more now */
            return bytes_sent;
        }

        /* Handle escape continuation for active slot */
        if (current_tx_slot == 1) {
            if (send_escape_pending) {
                platform_serial_write_byte(send_pending_byte);
                send_escape_pending = 0;
                send_idx++; /* complete original buffer byte */
                bytes_sent++;
                continue;
            }

            uint16_t i = send_idx;
            uint8_t data = mctp_buffer[i];

            /* header/trailer bytes are transmitted raw; only payload bytes are escaped */
            uint16_t body_size = mctp_buffer[OFFSET_BYTE_COUNT];
            if ((i < 3) || (i > (uint16_t)(body_size + 3))) {
                platform_serial_write_byte(data);
                send_idx++;
                bytes_sent++;
                continue;
            }

            /* payload bytes: escape FRAME_CHAR and ESCAPE_CHAR */
            if ((data == FRAME_CHAR) || (data == ESCAPE_CHAR)) {
                platform_serial_write_byte(ESCAPE_CHAR);
                send_pending_byte = (uint8_t)(data - 0x20);
                if (!platform_serial_can_write()) {
                    send_escape_pending = 1;
                    return bytes_sent;
                }
                platform_serial_write_byte(send_pending_byte);
                send_idx++;
                bytes_sent++;
                continue;
            }

            /* normal payload byte */
            platform_serial_write_byte(data);
            send_idx++;
            bytes_sent++;
        }
#if MCTP_EVENT_TX_ENABLED
        else if (current_tx_slot == 2) {
            if (tx_event_escape_pending) {
                platform_serial_write_byte(tx_event_pending_byte);
                tx_event_escape_pending = 0;
                tx_event_idx++;
                bytes_sent++;
                continue;
            }

            uint16_t i = tx_event_idx;
            uint8_t data = tx_buf_event[i];

            /* Determine header/body/trailer regions using the length field at offset 2
             * and avoid escaping header/trailer bytes (same behavior as primary slot).
             */
            uint16_t body_size = 0;
            if (tx_event_len > OFFSET_BYTE_COUNT) {
                body_size = tx_buf_event[OFFSET_BYTE_COUNT];
            }

            if ((i < 3) || (i > (uint16_t)(body_size + 3))) {
                /* header/trailer: send raw */
                platform_serial_write_byte(data);
                tx_event_idx++;
                bytes_sent++;
                continue;
            }

            /* payload bytes: escape FRAME_CHAR and ESCAPE_CHAR */
            if ((data == FRAME_CHAR) || (data == ESCAPE_CHAR)) {
                platform_serial_write_byte(ESCAPE_CHAR);
                tx_event_pending_byte = (uint8_t)(data - 0x20);
                if (!platform_serial_can_write()) {
                    tx_event_escape_pending = 1;
                    return bytes_sent;
                }
                platform_serial_write_byte(tx_event_pending_byte);
                tx_event_idx++;
                bytes_sent++;
                continue;
            }

            /* normal payload byte */
            platform_serial_write_byte(data);
            tx_event_idx++;
            bytes_sent++;
        }
#endif
        else {
            /* unknown slot, bail out */
            return bytes_sent;
        }
    }

    /* Completed current frame -- clear active slot state */
    if (current_tx_slot == 1) {
        send_in_progress = 0;
        send_idx = 0;
        send_total_len = 0;
        send_escape_pending = 0;
        /* reset the framer state to wait for the next packet */
        rxState = MCTPSER_WAITING_FOR_SYNC;
    }
#if MCTP_EVENT_TX_ENABLED
    else if (current_tx_slot == 2) {
        tx_event_pending = 0;
        tx_event_idx = 0;
        tx_event_len = 0;
        tx_event_escape_pending = 0;
    }
#endif

    current_tx_slot = 0;
    return bytes_sent;
}


/**
 * @brief Enqueue an event frame for prioritized transmit.
 *
 * The event slot is a single prioritized transmit buffer used for
 * asynchronous event/notification frames. This call is non-blocking and
 * will return immediately if the event slot is already occupied.
 *
 * @param data Pointer to the logical (unescaped) event frame bytes.
 * @param len Length of the frame in bytes.
 * @return int 0 on success, -1 if the event slot is occupied, -2 if the
 *             provided frame is too large for the event buffer.
 */
int mctp_send_event(const uint8_t* data, uint16_t len) {
#if MCTP_EVENT_TX_ENABLED
    if (len > MCTP_EVENT_TX_BUF_SIZE) return -2;
    if (tx_event_pending) return -1;
    for (uint16_t i = 0; i < len; ++i) tx_buf_event[i] = data[i];
    tx_event_len = len;
    tx_event_idx = 0;
    tx_event_pending = 1;
    tx_event_escape_pending = 0;
    return 0;
#else
    return -1;
#endif
}

/**
 * @brief Return whether the event transmit queue is empty.
 *
 * @return uint8_t Returns 1 if the event queue is empty, 0 if an event is pending.
 */
uint8_t mctp_is_event_queue_empty(void) {
#if MCTP_EVENT_TX_ENABLED
    return tx_event_pending ? 0 : 1;
#else
    return 1;
#endif
}

