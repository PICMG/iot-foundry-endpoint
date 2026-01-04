/**
 * @file test_mctp.c
 * @brief Test runner for MCTP tests.
 *
 * This file implements the tests for mctp using an in-repo test runner producing
 * console output and a JUnit XML file for CI consumption.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../include/mctp.h"
#include "../src/fcs.h"
#include "mctp_testhooks.h"

/* test-side constants used by the tests */
#ifndef FRAME_CHAR
#define FRAME_CHAR 0x7E
#endif
#ifndef ESCAPE_CHAR
#define ESCAPE_CHAR 0x7D
#endif
#ifndef MCTP_BUFFER_SIZE
#define MCTP_BUFFER_SIZE (64 + 6)
#endif

/* forward-declare mock helpers from platform_mock.c */
void mock_clear_tx(void);
void mock_set_can_write(uint8_t v);
uint16_t mock_tx_len(void);
const uint8_t* mock_tx_buffer(void);
extern uint8_t mctp_send_frame(void);
void mock_set_rx_buffer(const uint8_t* buf, uint16_t len);
void mock_clear_rx(void);
uint16_t mock_rx_len(void);
extern uint8_t platform_serial_has_data(void);

/* Test runner bookkeeping */
static char last_failure_msg[512];
static const char* last_failure_file = NULL;
static int last_failure_line = 0;

/**
 * @brief Record a failure message for the test runner.
 *
 * Stores a formatted failure message and the file/line where it occurred
 * for later reporting by the test runner.
 *
 * @param file Source file where the failure occurred.
 * @param line Line number where the failure occurred.
 * @param fmt printf-style format string for the failure message.
 */
static void record_failure(const char* file, int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_failure_msg, sizeof(last_failure_msg), fmt, ap);
    va_end(ap);
    last_failure_file = file;
    last_failure_line = line;
}

/**
 * @brief Assert-like helper for tests.
 *
 * If `cond` is false, records a formatted failure and returns 1.
 * Otherwise returns 0.
 *
 * @param cond Condition that must be true.
 * @param fmt printf-style format string used when the assertion fails.
 * @return int 0 on success, 1 on failure.
 */
static int require(int cond, const char* fmt, ...) {
    if (cond) return 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_failure_msg, sizeof(last_failure_msg), fmt, ap);
    va_end(ap);
    last_failure_file = __FILE__;
    last_failure_line = __LINE__;
    return 1;
}

/**
 * @brief Assert that two byte arrays are equal for `len` bytes.
 *
 * On the first mismatch records a failure describing the index and values.
 *
 * @param expected Pointer to expected bytes.
 * @param actual Pointer to actual bytes.
 * @param len Number of bytes to compare.
 * @return int 0 on success, 1 on failure.
 */
static int require_u8_array_eq(const uint8_t* expected, const uint8_t* actual, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (expected[i] != actual[i]) {
            record_failure(__FILE__, __LINE__, "Array mismatch at %zu: exp=0x%02x got=0x%02x", i, expected[i], actual[i]);
            return 1;
        }
    }
    return 0;
}


/**
 * @brief Unescape a transmitted buffer into its logical form.
 *
 * Converts escape sequences used on the wire back into their logical
 * byte values and copies the result into `out` up to `out_max` bytes.
 *
 * @param tx Pointer to the raw transmitted bytes.
 * @param tx_len Length of the `tx` buffer.
 * @param out Destination buffer for the unescaped payload.
 * @param out_max Maximum bytes available in `out`.
 * @return uint16_t Logical payload length (body length + framing bytes).
 */
static uint16_t unescape_tx(const uint8_t* tx, uint16_t tx_len, uint8_t* out, uint16_t out_max) {
    uint8_t body_len = tx[2];
    uint16_t o = 0;
    for (uint16_t i = 0; i < tx_len && o < out_max; ++i) {
        uint8_t b = tx[i];
        if ((o < 3) || (o >= body_len + 3)) {
            out[o++] = b;
            continue;
        }
        if (b == 0x7D) {
            if (i + 1 < tx_len) {
                uint8_t nb = tx[++i];
                out[o++] = (uint8_t)(nb + 0x20);
            }
        } else {
            out[o++] = b;
        }
    }
    return (uint16_t)(body_len + 6);
}

/**
 * @brief Test known FCS result for a small data vector.
 *
 * Verifies `calc_fcs()` against a precomputed value.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_calc_fcs_known(void) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t f = calc_fcs(0xffff, data, 4);
    if (require(f == 50798, "calc_fcs mismatch: %u", (unsigned)f)) return 1;
    return 0;
}

/**
 * @brief Test transmitting a frame that requires escaping and resuming.
 *
 * Ensures partial sends and escape handling behave as expected.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_send_frame_escape_and_resume(void) {
    uint8_t frame[10] = {0x7E, 0x01, 0x02, 0x00, 0x7E, 0x12, 0x34, 0x7E};
    mock_clear_tx();
    {
        uint16_t _len = 8;
        if (_len > MCTP_BUFFER_SIZE) _len = MCTP_BUFFER_SIZE;
        for (uint16_t _i = 0; _i < _len; ++_i) mctp_buffer[_i] = frame[_i];
        if (_len > 2) {
            uint8_t body = (uint8_t)((_len >= 6) ? (_len - 6) : 0);
            mctp_buffer[2] = body;
        }
        buffer_idx = (uint8_t)_len;
        rxState = MCTPSER_AWAITING_RESPONSE;
    }
    mock_set_can_write(1);
    mctp_send_frame();
    if (mock_tx_len() >= 8) {
        record_failure(__FILE__, __LINE__, "entire frame sent in one call, expected partial");
        return 1;
    }
    mctp_send_frame();
    if (mock_tx_len() >= 8) {
        record_failure(__FILE__, __LINE__, "entire frame sent in one call, expected partial");
        return 1;
    }
    while (mock_tx_len() < 9) {
        mock_set_can_write(1);
        mctp_send_frame();
    }
    uint8_t expected[] = {0x7E, 0x01, 0x02, 0x00, 0x7D, 0x5E, 0x12, 0x34, 0x7E};
    if (require_u8_array_eq(expected, mock_tx_buffer(), sizeof(expected))) return 1;
    return 0;
}

/**
 * @brief Test send-frame reentrancy and final unescaped output.
 *
 * Ensures `mctp_send_frame()` handles reentrancy and produces the
 * expected final unescaped frame.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_send_frame_reentrancy(void) {
    mock_clear_tx();
    uint8_t frame[8] = {0x7E, 0x01, 0x02, 0x00, 0x7E, 0x12, 0x34, 0x7E};
    {
        uint16_t _len = 8;
        if (_len > MCTP_BUFFER_SIZE) _len = MCTP_BUFFER_SIZE;
        for (uint16_t _i = 0; _i < _len; ++_i) mctp_buffer[_i] = frame[_i];
        if (_len > 2) {
            uint8_t body = (uint8_t)((_len >= 6) ? (_len - 6) : 0);
            mctp_buffer[2] = body;
        }
        buffer_idx = (uint8_t)_len;
        rxState = MCTPSER_AWAITING_RESPONSE;
    }
    mock_set_can_write(4);
    mctp_send_frame();
    if (require(mock_tx_len() == 1, "mock_tx_len expected 1 got %u", (unsigned)mock_tx_len())) return 1;
    mock_set_can_write(5);
    mctp_send_frame();
    if (require(mock_tx_len() == 1, "mock_tx_len expected 1 got %u", (unsigned)mock_tx_len())) return 1;
    int iter = 0;
    mock_set_can_write(1);
    while (mctp_send_frame() != 0 && iter++ < 100) mock_set_can_write(1);
    uint8_t out[64];
    unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require_u8_array_eq(frame, out, 8)) return 1;
    return 0;
}

/**
 * @brief Test that a valid RX frame results in a packet being available.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_validate_rx_valid(void) {
    mctp_init();
    uint8_t total_len = 11;
    uint8_t frame[11];
    frame[0] = 0x7E; frame[1] = 0x01; frame[2] = 5;
    frame[3] = 0x10; frame[4] = 0x00; frame[5] = 0x30; frame[6] = 0x40; frame[7] = 0x50;
    uint16_t fcs = calc_fcs(0xffff, &frame[1], 7);
    frame[8] = (uint8_t)(fcs >> 8);
    frame[9] = (uint8_t)(fcs & 0x00FF);
    frame[10] = 0x7E;
    mock_clear_rx();
    mock_set_rx_buffer(frame, total_len);
    extern void mctp_update(void);
    int iter = 0;
    while (!mctp_is_packet_available() && iter++ < 20) mctp_update();
    if (require(mctp_is_packet_available(), "expected packet available")) return 1;
    return 0;
}

/**
 * @brief Test that a frame with bad FCS is rejected.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_validate_rx_bad_fcs(void) {
    mctp_init();
    uint8_t total_len = 11; uint8_t frame[11];
    frame[0] = 0x7E; frame[1] = 0x01; frame[2] = 5; frame[3] = 1; frame[4] = 0x00; frame[5] = 3; frame[6] = 4; frame[7] = 5;
    uint16_t fcs = calc_fcs(0xffff, &frame[1], 7); fcs ^= 0x1234;
    frame[8] = (uint8_t)(fcs >> 8); frame[9] = (uint8_t)(fcs & 0x00FF); frame[10] = 0x7E;
    mock_clear_rx(); mock_set_rx_buffer(frame, total_len);
    extern void mctp_update(void);
    int iter = 0; while (!mctp_is_packet_available() && iter++ < 20) mctp_update();
    if (require(!mctp_is_packet_available(), "expected no packet available")) return 1;
    return 0;
}

/**
 * @brief Test initialization helpers and packet-type helpers.
 *
 * Exercises `mctp_init()` and packet inspection helpers.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_init_and_helpers(void) {
    extern void mctp_init(void);
    mctp_init();
    if (require(!mctp_is_packet_available(), "expected no packet available")) return 1;
    uint8_t frame[11]; frame[0]=0x7E; frame[1]=0x01; frame[2]=5; frame[3]=0; frame[4]=0; frame[5]=0; frame[6]=0; frame[7]=0x00;
    uint16_t fcs = calc_fcs(0xffff, &frame[1], 7); frame[8]=(uint8_t)(fcs>>8); frame[9]=(uint8_t)(fcs&0xFF); frame[10]=0x7E;
    {
        uint16_t _len = 11; if (_len > MCTP_BUFFER_SIZE) _len = MCTP_BUFFER_SIZE;
        for (uint16_t _i = 0; _i < _len; ++_i) mctp_buffer[_i] = frame[_i];
        if (_len > 2) mctp_buffer[2] = (uint8_t)((_len >= 6) ? (_len - 6) : 0);
        buffer_idx = (uint8_t)_len; rxState = MCTPSER_AWAITING_RESPONSE;
    }
    if (require(mctp_is_control_packet(), "expected control packet")) return 1;
    frame[7] = 0x01;
    {
        uint16_t _len = 11; if (_len > MCTP_BUFFER_SIZE) _len = MCTP_BUFFER_SIZE;
        for (uint16_t _i = 0; _i < _len; ++_i) mctp_buffer[_i] = frame[_i];
        if (_len > 2) mctp_buffer[2] = (uint8_t)((_len >= 6) ? (_len - 6) : 0);
        buffer_idx = (uint8_t)_len; rxState = MCTPSER_AWAITING_RESPONSE;
    }
    if (require(mctp_is_pldm_packet(), "expected PLDM packet")) return 1;
    mctp_ignore_packet();
    if (require(!mctp_is_packet_available(), "expected no packet available")) return 1;
    return 0;
}

/* Helper used by many control tests: send GET_ENDPOINT_ID to `dest` and return 1 if we
 * transmitted a response */
static int send_and_check(uint8_t dest) {
    uint8_t hdr_version = 0x01;
    uint8_t source_id = 8;
    uint8_t som_eom = 0xC8; /* single frame */
    uint8_t message_type = 0x00;
    uint8_t instance_id = 0x80;
    uint8_t command_code = 0x02; /* GET_ENDPOINT_ID */

    uint8_t byte_count = 7;
    uint16_t total_len = (uint16_t)byte_count + 6;
    uint8_t frame[64] = {0x7E,      0x01,    byte_count,   hdr_version, dest,
                         source_id, som_eom, message_type, instance_id, command_code};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len - 3] = (uint8_t)(fcs >> 8);
    frame[total_len - 2] = (uint8_t)(fcs & 0xFF);
    frame[total_len - 1] = 0x7E;

    /* reuse the control helper to process and wait for response */
    extern int test_send_control_message_and_wait_for_response(uint8_t frame[], uint16_t total_len);
    test_send_control_message_and_wait_for_response(frame, total_len);
    return (mock_tx_len() > 0) ? 1 : 0;
}

/**
 * @brief Inject a control frame and wait for a response.
 *
 * Processes a control message and verifies the resulting transmitted
 * control response buffer meets basic expectations.
 *
 * @param frame Frame bytes to inject.
 * @param total_len Length of `frame`.
 * @return int 0 on success, 1 on failure.
 */
int test_send_control_message_and_wait_for_response(uint8_t frame[], uint16_t total_len) {
    mctp_init();
    mock_clear_tx();

    uint8_t source_id = frame[5];
    uint8_t destination_id = frame[4];
    uint8_t seq_tag = frame[6];
    uint8_t msg_type = frame[7];
    uint8_t instance_id = frame[8];
    uint8_t command_code = frame[9];

    mock_set_rx_buffer(frame, total_len);
    int iter = 0;
    while (!mctp_is_packet_available() && iter++ < 200) mctp_update();
    if (!mctp_is_packet_available()) {
        record_failure(__FILE__, __LINE__, "packet never became available");
        return 1;
    }

    mctp_process_control_message();
    mock_set_can_write(1);
    iter = 0;
    while (mctp_send_frame() != 0 && iter++ < 100) mock_set_can_write(1);

    uint8_t out[256];
    uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out[0] == 0x7E, "frame char invalid")) return 1;
    if (require(out[1] == 0x01, "protocol version invalid")) return 1;
    if (require((out[2] + 6) == out_len, "payload length mismatch")) return 1;
    if (require(out[4] == source_id, "dst id mismatch")) return 1;
    if (require(out[5] == destination_id, "src id mismatch")) return 1;
    if (require(((out[6] & ~0xC0) == ((seq_tag ^ 0x8) & ~0xC0)), "seq tag mismatch")) return 1;
    if (require(out[7] == msg_type, "message type mismatch")) return 1;
    if (require(out[8] == (instance_id & 0x7F), "instance id mismatch")) return 1;
    if (require(out[9] == command_code, "command code mismatch")) return 1;
    if (require(out[out_len - 1] == 0x7E, "trailer missing")) return 1;
    return 0;
}

/**
 * @brief Test GET_ENDPOINT_ID control message handling.
 *
 * Sends a GET_ENDPOINT_ID and checks the returned completion/response.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_get_endpoint_id(void) {
    uint8_t hdr_version = 0x01; uint8_t source_id = 8; uint8_t destination_id = 0; uint8_t som_eom = 0xC8;
    uint8_t message_type = 0x00; uint8_t instance_id = 0x80; uint8_t command_code = 0x02;
    uint8_t byte_count = 7; uint16_t total_len = (uint16_t)byte_count + 6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len - 3] = (uint8_t)(fcs >> 8); frame[total_len - 2] = (uint8_t)(fcs & 0xFF); frame[total_len - 1] = 0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out_len > 10, "response too short")) return 1;
    if (require(out[10] == 0x00, "completion code not success")) return 1;
    return 0;
}

/**
 * @brief Test SET_ENDPOINT_ID with invalid parameters.
 *
 * Expects an invalid-data completion code.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_set_endpoint_id_invalid(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code=0x01, operation=0x01, eid=0x00;
    uint8_t byte_count=9; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,operation,eid};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len - 3] = (uint8_t)(fcs>>8); frame[total_len - 2] = (uint8_t)(fcs&0xFF); frame[total_len - 1] = 0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out_len > 10, "response too short")) return 1;
    if (require(out[10] == 0x02, "expected invalid data completion")) return 1;
    return 0;
}

/**
 * @brief Test successful SET_ENDPOINT_ID and subsequent behavior.
 *
 * Verifies the endpoint update and that new EID receives messages.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_set_endpoint_id_success(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code = CONTROL_MSG_SET_ENDPOINT_ID; uint8_t operation = 0x01; uint8_t eid = 0x09;
    uint8_t byte_count = 9; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,operation,eid};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len - 3] = (uint8_t)(fcs>>8); frame[total_len - 2] = (uint8_t)(fcs&0xFF); frame[total_len - 1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "set eid response failed")) return 1;
    uint8_t out[256]; unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out[10] == CONTROL_COMPLETE_SUCCESS, "completion not success")) return 1;
    if (require(out[11] == 0x00, "endpoint acceptance not accepted")) return 1;
    if (require(out[12] == 0x00, "returned previous eid mismatch")) return 1;
    /* Now send a GET_ENDPOINT_ID to new eid and check we respond */
    if (require(send_and_check(eid) == 1, "did not respond to new eid")) return 1;
    return 0;
}

/**
 * @brief Test GET_MESSAGE_TYPE_SUPPORT control command.
 *
 * Verifies reported supported message types include expected ones.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_get_message_type_support(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code=0x05; uint8_t byte_count=7; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; (void)unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out[10] == 0x00, "completion not success")) return 1;
    if (require(out[11] > 0, "no message types reported")) return 1;
    uint8_t set_eid_supported=0,get_eid_supported=0,get_version_supported=0,get_msgtype_supported=0;
    for (uint8_t i=0;i<out[11];++i) {
        uint8_t mt = out[12+i];
        if (mt == CONTROL_MSG_SET_ENDPOINT_ID) set_eid_supported = 1;
        else if (mt == CONTROL_MSG_GET_ENDPOINT_ID) get_eid_supported = 1;
        else if (mt == CONTROL_MSG_GET_MCTP_VERSION_SUPPORT) get_version_supported = 1;
        else if (mt == CONTROL_MSG_GET_MESSAGE_TYPE_SUPPORT) get_msgtype_supported = 1;
    }
    if (require(set_eid_supported && get_eid_supported && get_version_supported && get_msgtype_supported, "expected support bits missing")) return 1;
    return 0;
}

/**
 * @brief Test GET_MCTP_VERSION_SUPPORT control command.
 *
 * Verifies version bytes are returned as expected.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_get_mctp_version_support(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code = CONTROL_MSG_GET_MCTP_VERSION_SUPPORT; uint8_t byte_count=8; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,0x00};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
    frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out[10] == CONTROL_COMPLETE_SUCCESS, "completion not success")) return 1;
    if (require(out_len > 11, "no version bytes")) return 1;
    return 0;
}

/**
 * @brief Test behavior for an unsupported control command.
 *
 * Expects completion code indicating unsupported command.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_unsupported_command(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code=0xFF; uint8_t byte_count=7; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4); frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; (void)unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require(out[10] == CONTROL_COMPLETE_UNSUPPORTED_CMD, "expected unsupported completion")) return 1;
    return 0;
}

/**
 * @brief Test that sequence tag and instance ID are propagated correctly.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_sequence_tag_instance(void) {
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, message_type=0x00;
    uint8_t instance_id=0x81, seq_tag=0x0A, command_code=CONTROL_MSG_GET_ENDPOINT_ID;
    uint8_t byte_count=8; uint16_t total_len=(uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,seq_tag,message_type,instance_id,command_code};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4); frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "control response failed")) return 1;
    uint8_t out[256]; (void)unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out));
    if (require((out[6] & ~0xC0) == ((seq_tag ^ 0x8) & ~0xC0), "seq tag mismatch")) return 1;
    if (require(out[8] == (instance_id & 0x7F), "instance id mismatch")) return 1;
    return 0;
}

/**
 * @brief Test EID acceptance and behavior after setting an EID.
 *
 * Exercises discovery and set EID operations and verifies responses.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_endpoint_eid_acceptance(void) {
    mctp_init();
    uint8_t test_eids[] = {0x00, 0xFF, 0x08};
    if (require(send_and_check(test_eids[0]) == 1, "did not respond to 0x00")) return 1;
    if (require(send_and_check(test_eids[1]) == 1, "did not respond to 0xFF")) return 1;
    if (require(send_and_check(test_eids[2]) == 0, "unexpected response to 0x08 before set")) return 1;
    /* set endpoint to 0x08 */
    mock_clear_tx(); mock_clear_rx();
    uint8_t command_code_set = CONTROL_MSG_SET_ENDPOINT_ID; uint8_t operation = 0x01; uint8_t eid = 0x08; uint8_t byte_count = 9; uint16_t total_len = (uint16_t)byte_count+6;
    uint8_t frame[64] = {0x7E,0x01,byte_count,0x01,0x00,8,0xC8,0x00,0x80,command_code_set,operation,eid};
    uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4); frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
    if (require(test_send_control_message_and_wait_for_response(frame, total_len) == 0, "set eid failed")) return 1;
    if (require(send_and_check(test_eids[0]) == 1, "did not respond to 0x00 after set")) return 1;
    if (require(send_and_check(test_eids[1]) == 1, "did not respond to 0xFF after set")) return 1;
    if (require(send_and_check(test_eids[2]) == 1, "did not respond to 0x08 after set")) return 1;
    return 0;
}

/**
 * @brief Test RX handling when payload ends with an escaped frame char.
 *
 * Verifies the framer accepts an escaped FRAME_CHAR in payload.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_rx_escape_end_payload(void) {
    mock_clear_rx(); mock_clear_tx();
    uint8_t hdr=0x01, src=8, som_eom=0xC8, msg_type=0x00, instance=0x80;
    uint8_t body_logical[] = {hdr,0x00,src,som_eom,msg_type,instance,FRAME_CHAR};
    uint8_t byte_count = sizeof(body_logical); uint16_t total_logical = (uint16_t)byte_count + 6;
    uint8_t wire[128]; int wi=0; wire[wi++]=FRAME_CHAR; wire[wi++]=hdr; wire[wi++]=byte_count;
    for (size_t i=0;i<sizeof(body_logical);++i) { uint8_t b=body_logical[i]; if ((b==FRAME_CHAR)||(b==ESCAPE_CHAR)) { wire[wi++]=ESCAPE_CHAR; wire[wi++]=(uint8_t)(b-0x20);} else wire[wi++]=b; }
    uint8_t temp[128]; temp[0]=FRAME_CHAR; temp[1]=hdr; temp[2]=byte_count; for (size_t i=0;i<sizeof(body_logical);++i) temp[3+i]=body_logical[i]; uint16_t fcs = calc_fcs(0xffff, &temp[1], total_logical - 4);
    wire[wi++]=(uint8_t)(fcs>>8); wire[wi++]=(uint8_t)(fcs&0xFF); wire[wi++]=FRAME_CHAR;
    mock_set_rx_buffer(wire, wi);
    while (platform_serial_has_data()) mctp_update();
    if (require(mctp_is_packet_available(), "expected packet available")) return 1;
    return 0;
}

/**
 * @brief Test handling of an invalid escape sequence in RX.
 *
 * Ensures invalid sequences are rejected or produce non-success completions.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_rx_invalid_escape_sequence(void) {
    mock_clear_rx();
    uint8_t hdr=0x01; uint8_t byte_count=3; uint8_t wire[64]; int wi=0; wire[wi++]=FRAME_CHAR; wire[wi++]=hdr; wire[wi++]=byte_count; wire[wi++]=0x10; wire[wi++]=ESCAPE_CHAR; wire[wi++]=0x00;
    uint8_t logical[5]; logical[0]=FRAME_CHAR; logical[1]=hdr; logical[2]=byte_count; logical[3]=0x10; logical[4]=0x00; uint16_t fcs = calc_fcs(0xffff, &logical[1], (int)(3+1)); wire[wi++]=(uint8_t)(fcs>>8); wire[wi++]=(uint8_t)(fcs&0xFF); wire[wi++]=FRAME_CHAR;
    mock_set_rx_buffer(wire, wi); while (platform_serial_has_data()) mctp_update();
    if (mctp_is_packet_available()) { mctp_process_control_message(); mock_set_can_write(1); int iter=0; while (mctp_send_frame() != 0 && iter++ < 100) mock_set_can_write(1); }
    if (mock_tx_len() > 0) { uint8_t out[256]; uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out)); if (require(out_len > 10, "response too short")) return 1; if (require(out[10] != CONTROL_COMPLETE_SUCCESS, "unexpected success completion")) return 1; } else { if (require(mock_tx_len() == 0, "expected no tx")) return 1; }
    return 0;
}

/**
 * @brief Test that RX frames at the buffer boundary are accepted.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_rx_buffer_boundary_accept(void) {
    mock_clear_rx(); uint8_t hdr=0x01; const int max_body = MCTP_BUFFER_SIZE - 8; int byte_count = max_body; int total_len = byte_count + 6; uint8_t frame[256]; int i=0; frame[i++]=FRAME_CHAR; frame[i++]=hdr; frame[i++]=(uint8_t)byte_count; frame[i++]=hdr; frame[i++]=0x00; for (int k=2;k<byte_count;k++) frame[i++]=(uint8_t)(k&0xFF); uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4); frame[i++]=(uint8_t)(fcs>>8); frame[i++]=(uint8_t)(fcs&0xFF); frame[i++]=FRAME_CHAR;
    mock_set_rx_buffer(frame, i); while (platform_serial_has_data()) mctp_update(); if (require(mctp_is_packet_available() || mock_tx_len() > 0, "expected packet or tx")) return 1; return 0;
}

/**
 * @brief Test that overly large RX frames are rejected.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_rx_buffer_boundary_reject(void) {
    mock_clear_rx(); uint8_t hdr=0x01; const int over_body = (MCTP_BUFFER_SIZE - 8) + 1; int byte_count = over_body; int total_len = byte_count + 6; uint8_t frame[512]; int i=0; frame[i++]=FRAME_CHAR; frame[i++]=hdr; frame[i++]=(uint8_t)byte_count; frame[i++]=hdr; frame[i++]=0x00; for (int k=2;k<byte_count;k++) frame[i++]=(uint8_t)(k&0xFF); uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4); frame[i++]=(uint8_t)(fcs>>8); frame[i++]=(uint8_t)(fcs&0xFF); frame[i++]=FRAME_CHAR;
    mock_set_rx_buffer(frame, i); while (platform_serial_has_data()) mctp_update(); if (mctp_is_packet_available()) { mctp_process_control_message(); mock_set_can_write(1); int iter=0; while (mctp_send_frame() != 0 && iter++ < 100) mock_set_can_write(1); }
    if (mock_tx_len() > 0) { uint8_t out[256]; uint16_t out_len = unescape_tx(mock_tx_buffer(), mock_tx_len(), out, sizeof(out)); if (require(out_len > 10, "response too short")) return 1; if (require(out[10] != CONTROL_COMPLETE_SUCCESS, "unexpected success")) return 1; } else { if (require(mock_tx_len() == 0, "expected no tx")) return 1; }
    return 0;
}

/**
 * @brief Test rejection of frames that are too short to be valid.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_malformed_too_short(void) {
    mock_clear_rx(); mock_clear_tx(); uint8_t short_frame[6] = {FRAME_CHAR,0x01,0x01,0x02,0x03,FRAME_CHAR}; mock_set_rx_buffer(short_frame, sizeof(short_frame)); while (platform_serial_has_data()) mctp_update(); if (require(!mctp_is_packet_available(), "packet should be rejected")) return 1; if (require(mock_tx_len() == 0, "no tx expected")) return 1; return 0;
}

/**
 * @brief Test rejection of frames with a bad length field.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_malformed_bad_length_field(void) {
    mock_clear_rx(); mock_clear_tx(); uint8_t hdr=0x01; uint8_t body[] = {0x10,0x11,0x12}; uint8_t declared_count = 9; uint8_t buf[32]; int i=0; buf[i++]=FRAME_CHAR; buf[i++]=hdr; buf[i++]=declared_count; for (size_t k=0;k<sizeof(body);++k) buf[i++]=body[k]; uint16_t fcs = calc_fcs(0xffff, &buf[1], (int)(3 + sizeof(body) - 1)); buf[i++]=(uint8_t)(fcs>>8); buf[i++]=(uint8_t)(fcs&0xFF); buf[i++]=FRAME_CHAR; mock_set_rx_buffer(buf, i); while (platform_serial_has_data()) mctp_update(); if (require(!mctp_is_packet_available(), "packet incorrectly accepted")) return 1; if (require(mock_tx_len() == 0, "no tx expected")) return 1; return 0;
}

/**
 * @brief Test rejection of frames missing the trailer byte.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_malformed_missing_trailer(void) {
    mock_clear_rx(); mock_clear_tx(); uint8_t hdr=0x01; uint8_t byte_count=5; uint8_t buf[32]; int i=0; buf[i++]=FRAME_CHAR; buf[i++]=hdr; buf[i++]=byte_count; for (int k=0;k<byte_count;k++) buf[i++]=(uint8_t)k; uint16_t fcs = calc_fcs(0xffff, &buf[1], (int)(byte_count + 2)); buf[i++]=(uint8_t)(fcs>>8); buf[i++]=(uint8_t)(fcs&0xFF); /* omit trailer */ mock_set_rx_buffer(buf, i); while (platform_serial_has_data()) mctp_update(); if (require(!mctp_is_packet_available(), "packet incorrectly accepted")) return 1; if (require(mock_tx_len() == 0, "no tx expected")) return 1; return 0;
}

/**
 * @brief Test rejection of frames with truncated FCS bytes.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_malformed_truncated_fcs(void) {
    mock_clear_rx(); mock_clear_tx(); uint8_t hdr=0x01; uint8_t byte_count=5; uint8_t buf[32]; int i=0; buf[i++]=FRAME_CHAR; buf[i++]=hdr; buf[i++]=byte_count; for (int k=0;k<byte_count;k++) buf[i++]=(uint8_t)k; uint16_t fcs = calc_fcs(0xffff, &buf[1], (int)(byte_count + 2)); buf[i++]=(uint8_t)(fcs>>8); /* only MSB */ mock_set_rx_buffer(buf, i); while (platform_serial_has_data()) mctp_update(); if (require(!mctp_is_packet_available(), "packet incorrectly accepted")) return 1; if (require(mock_tx_len() == 0, "no tx expected")) return 1; return 0;
}

/**
 * @brief Test the concatenation property of the FCS algorithm.
 *
 * Verifies that computing FCS in pieces matches computing it over the
 * concatenated buffer.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_calc_fcs_concat_property(void) {
    uint8_t a[] = {0x10, 0x20, 0x30};
    uint8_t b[] = {0x40, 0x50};
    uint8_t ab[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint16_t f_ab = calc_fcs(0xffff, ab, sizeof(ab));
    uint16_t f_a = calc_fcs(0xffff, a, sizeof(a));
    uint16_t f_a_b = calc_fcs(f_a, b, sizeof(b));
    if (require(f_ab == f_a_b, "FCS concatenation property failed")) return 1;
    return 0;
}

/**
 * @brief Test SET_ENDPOINT_ID operations that should be invalid (reset/discovery).
 *
 * Verifies invalid operations produce the expected invalid-data completion.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_set_endpoint_id_reset_and_discovery(void) {
    /* operation 0x02 = reset static EID, 0x03 = set discovery flag -> both invalid */
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code = CONTROL_MSG_SET_ENDPOINT_ID;
    for (int op = 0x02; op <= 0x03; ++op) {
        uint8_t operation = (uint8_t)op, eid = 0x05;
        uint8_t byte_count = 9; uint16_t total_len=(uint16_t)byte_count+6;
        uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,operation,eid};
        uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
        frame[total_len-3] = (uint8_t)(fcs>>8); frame[total_len-2] = (uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;

        /* Inject the frame and let the framer parse it */
        mctp_init(); mock_clear_tx(); mock_set_rx_buffer(frame, total_len);
        int iter = 0; while (!mctp_is_packet_available() && iter++ < 200) mctp_update();
        if (require(mctp_is_packet_available(), "packet not available for op %d", op)) return 1;

        /* Process control message and verify the completion code in the buffer */
        mctp_process_control_message();
        uint8_t completion = mctp_buffer[10]; /* OFFSET_CTRL_COMPLETION_CODE */
        if (require(completion == CONTROL_COMPLETE_INVALID_DATA, "expected invalid data completion for op %d", op)) return 1;
    }
    return 0;
}

/**
 * @brief Test GET_MCTP_VERSION_SUPPORT for base and unsupported message types.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_control_get_mctp_version_support_ff_and_unsupported(void) {
    /* test msg_type == 0xff (base spec) and unsupported msg_type (0x02) */
    uint8_t hdr_version=0x01, source_id=8, destination_id=0, som_eom=0xC8, message_type=0x00, instance_id=0x80;
    uint8_t command_code = CONTROL_MSG_GET_MCTP_VERSION_SUPPORT;
    /* msg_type 0xff */
    {
        uint8_t byte_count=8; uint16_t total_len=(uint16_t)byte_count+6;
        uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,0xFF};
        uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
        frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
        mctp_init(); mock_clear_tx(); mock_set_rx_buffer(frame, total_len);
        int iter = 0; while (!mctp_is_packet_available() && iter++ < 200) mctp_update();
        if (require(mctp_is_packet_available(), "packet not available for get version 0xff")) return 1;
        mctp_process_control_message();
        uint8_t completion = mctp_buffer[10];
        if (require(completion == CONTROL_COMPLETE_SUCCESS, "completion not success for 0xff")) return 1;
    }
    /* unsupported msg_type 0x02 */
    {
        uint8_t byte_count=8; uint16_t total_len=(uint16_t)byte_count+6;
        uint8_t frame[64] = {0x7E,0x01,byte_count,hdr_version,destination_id,source_id,som_eom,message_type,instance_id,command_code,0x02};
        uint16_t fcs = calc_fcs(0xffff, &frame[1], total_len - 4);
        frame[total_len-3]=(uint8_t)(fcs>>8); frame[total_len-2]=(uint8_t)(fcs&0xFF); frame[total_len-1]=0x7E;
        mctp_init(); mock_clear_tx(); mock_set_rx_buffer(frame, total_len);
        int iter = 0; while (!mctp_is_packet_available() && iter++ < 200) mctp_update();
        if (require(mctp_is_packet_available(), "packet not available for get version unsupported")) return 1;
        mctp_process_control_message();
        uint8_t completion = mctp_buffer[10];
        if (require(completion == 0x80, "expected 0x80 unsupported message type")) return 1;
    }
    return 0;
}


#if MCTP_EVENT_TX_ENABLED

/**
 * @brief Test behavior when the event queue fills.
 *
 * Enqueues two events and expects the second to fail.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_slot_full(void) {
    mock_clear_tx(); uint8_t evt_frame[9] = {0x7E,0x01,0x02,0x00,0x11,0x22,0x33,0x44,0x7E}; uint16_t fcs = calc_fcs(0xffff, &evt_frame[1], 5); evt_frame[5]=(uint8_t)(fcs>>8); evt_frame[6]=(uint8_t)(fcs&0xFF);
    int r1 = mctp_send_event(evt_frame, 9); int r2 = mctp_send_event(evt_frame,9); if (require(r1==0, "enqueue failed")) return 1; if (require(r2!=0, "second enqueue should fail")) return 1; mock_set_can_write(1); while (mctp_send_frame() != 0) mock_set_can_write(1); return 0; }


/**
 * @brief Test that an event waits for the current primary frame to finish.
 *
 * Ensures event frames are sent as a second frame following the primary.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_waits_for_current_frame(void) {
    mock_clear_tx(); uint8_t prim[9] = {0x7E,0x01,0x02,0x00,0xAA,0xBB,0xCC,0xDD,0x7E}; uint16_t fcs = calc_fcs(0xffff, &prim[1], 5); prim[5]=(uint8_t)(fcs>>8); prim[6]=(uint8_t)(fcs&0xFF);
    { uint16_t _len = 9; if (_len > MCTP_BUFFER_SIZE) _len = MCTP_BUFFER_SIZE; for (uint16_t _i=0; _i<_len; ++_i) mctp_buffer[_i]=prim[_i]; if (_len>2) mctp_buffer[2] = (uint8_t)((_len>=6)?(_len-6):0); buffer_idx=(uint8_t)_len; rxState = MCTPSER_AWAITING_RESPONSE; }
    mock_set_can_write(1); mctp_send_frame(); uint8_t evt[9] = {0x7E,0x01,0x02,0x00,0x10,0x20,0x30,0x40,0x7E}; fcs = calc_fcs(0xffff,&evt[1],5); evt[5]=(uint8_t)(fcs>>8); evt[6]=(uint8_t)(fcs&0xFF); int r = mctp_send_event(evt,9); if (require(r==0, "enqueue event failed")) return 1; mock_set_can_write(1); while (mctp_send_frame() != 0) mock_set_can_write(1);
    const uint8_t* tx = mock_tx_buffer(); uint16_t txlen = mock_tx_len(); if (require(txlen >= 18, "tx too short")) return 1; if (require(tx[0] == 0x7E, "first frame missing")) return 1; int found=0; for (uint16_t i=1;i<txlen;++i) if (tx[i]==0x7E) { found=1; break; } if (require(found, "second frame not found")) return 1; return 0; }


/**
 * @brief Test event priority when primary is idle.
 *
 * Verifies events are sent before primary frames when idle.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_priority_before_primary_when_idle(void) {
    mock_clear_tx(); uint8_t prim[9]={0x7E,0x01,0x02,0x00,0xAA,0xBB,0xCC,0xDD,0x7E}; uint16_t fcs=calc_fcs(0xffff,&prim[1],5); prim[5]=(uint8_t)(fcs>>8); prim[6]=(uint8_t)(fcs&0xFF);
    { uint16_t _len=9; if (_len>MCTP_BUFFER_SIZE) _len=MCTP_BUFFER_SIZE; for (uint16_t _i=0;_i<_len;++_i) mctp_buffer[_i]=prim[_i]; if (_len>2) mctp_buffer[2]=(uint8_t)((_len>=6)?(_len-6):0); buffer_idx=(uint8_t)_len; rxState=MCTPSER_AWAITING_RESPONSE; }
    uint8_t evt[9]={0x7E,0x01,0x02,0x00,0x10,0x20,0x30,0x40,0x7E}; fcs=calc_fcs(0xffff,&evt[1],5); evt[5]=(uint8_t)(fcs>>8); evt[6]=(uint8_t)(fcs&0xFF); int r=mctp_send_event(evt,9); if (require(r==0,"enqueue event failed")) return 1; mock_set_can_write(1); while (mctp_send_frame() != 0) mock_set_can_write(1); const uint8_t* tx=mock_tx_buffer(); if (require(tx[0]==0x7E,"first byte not frame")) return 1; uint8_t out[64]; uint16_t out_len = unescape_tx(tx, mock_tx_len(), out, sizeof(out)); (void)out_len; if (require(out[4]==0x10, "first frame destination mismatch")) return 1; return 0; }


/**
 * @brief Test event queue is empty initially.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_queue_empty_initial(void) { mock_clear_tx(); if (require(mctp_is_event_queue_empty() == 1, "event queue not empty initially")) return 1; return 0; }


/**
 * @brief Test event queue is non-empty after enqueue.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_queue_not_empty_after_enqueue(void) { mock_clear_tx(); uint8_t evt[9]={0x7E,0x01,0x02,0x00,0x10,0x20,0x30,0x40,0x7E}; uint16_t fcs=calc_fcs(0xffff,&evt[1],5); evt[5]=(uint8_t)(fcs>>8); evt[6]=(uint8_t)(fcs&0xFF); int r=mctp_send_event(evt,9); if (require(r==0,"enqueue failed")) return 1; if (require(mctp_is_event_queue_empty() == 0, "queue still empty after enqueue")) return 1; mock_set_can_write(1); while (mctp_send_frame() != 0) mock_set_can_write(1); return 0; }


/**
 * @brief Test event queue empties after transmit.
 *
 * @return int 0 on success, 1 on failure.
 */
int test_event_queue_empty_after_transmit(void) { mock_clear_tx(); uint8_t evt[9]={0x7E,0x01,0x02,0x00,0x10,0x20,0x30,0x40,0x7E}; uint16_t fcs=calc_fcs(0xffff,&evt[1],5); evt[5]=(uint8_t)(fcs>>8); evt[6]=(uint8_t)(fcs&0xFF); int r=mctp_send_event(evt,9); if (require(r==0,"enqueue failed")) return 1; mock_set_can_write(1); while (mctp_send_frame() != 0) mock_set_can_write(1); if (require(mctp_is_event_queue_empty() == 1, "queue not empty after transmit")) return 1; return 0; }
#endif

/* Test registration and runner */
typedef int (*test_fn_t)(void);
struct test_entry { const char* name; test_fn_t fn; };

static struct test_entry tests[] = {
    {"test_calc_fcs_known", test_calc_fcs_known},
    {"test_send_frame_escape_and_resume", test_send_frame_escape_and_resume},
    {"test_send_frame_reentrancy", test_send_frame_reentrancy},
    {"test_validate_rx_valid", test_validate_rx_valid},
    {"test_validate_rx_bad_fcs", test_validate_rx_bad_fcs},
    {"test_init_and_helpers", test_init_and_helpers},
    {"test_control_get_endpoint_id", test_control_get_endpoint_id},
    {"test_control_set_endpoint_id_invalid", test_control_set_endpoint_id_invalid},
    {"test_control_set_endpoint_id_success", test_control_set_endpoint_id_success},
    {"test_control_get_message_type_support", test_control_get_message_type_support},
    {"test_control_get_mctp_version_support", test_control_get_mctp_version_support},
    {"test_control_unsupported_command", test_control_unsupported_command},
    {"test_control_sequence_tag_instance", test_control_sequence_tag_instance},
    {"test_endpoint_eid_acceptance", test_endpoint_eid_acceptance},
    {"test_rx_escape_end_payload", test_rx_escape_end_payload},
    {"test_rx_invalid_escape_sequence", test_rx_invalid_escape_sequence},
    {"test_rx_buffer_boundary_accept", test_rx_buffer_boundary_accept},
    {"test_rx_buffer_boundary_reject", test_rx_buffer_boundary_reject},
    {"test_malformed_too_short", test_malformed_too_short},
    {"test_malformed_bad_length_field", test_malformed_bad_length_field},
    {"test_malformed_missing_trailer", test_malformed_missing_trailer},
    {"test_malformed_truncated_fcs", test_malformed_truncated_fcs},
    {"test_calc_fcs_concat_property", test_calc_fcs_concat_property},
    {"test_control_set_endpoint_id_reset_and_discovery", test_control_set_endpoint_id_reset_and_discovery},
    {"test_control_get_mctp_version_support_ff_and_unsupported", test_control_get_mctp_version_support_ff_and_unsupported},
    
    
#if MCTP_EVENT_TX_ENABLED
    {"test_event_slot_full", test_event_slot_full},
    {"test_event_waits_for_current_frame", test_event_waits_for_current_frame},
    {"test_event_priority_before_primary_when_idle", test_event_priority_before_primary_when_idle},
    {"test_event_queue_empty_initial", test_event_queue_empty_initial},
    {"test_event_queue_not_empty_after_enqueue", test_event_queue_not_empty_after_enqueue},
    {"test_event_queue_empty_after_transmit", test_event_queue_empty_after_transmit},
#endif
};

/**
 * @brief Test runner entry point.
 *
 * Executes all registered tests and emits a JUnit-style XML results file
 * at `tests/results.xml` for CI consumption.
 *
 * @return int 0 when all tests pass, non-zero when failures occurred.
 */

int main(void) {
    const int ntests = (int)(sizeof(tests) / sizeof(tests[0]));
    int failures = 0;
    FILE* xml = fopen("tests/results.xml", "w");
    if (xml) {
        fprintf(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        fprintf(xml, "<testsuite name=\"mctp\" tests=\"%d\">\n", ntests);
    }
    for (int i = 0; i < ntests; ++i) {
        printf("RUNNING %s...\n", tests[i].name);
        last_failure_msg[0] = '\0'; last_failure_file = NULL; last_failure_line = 0;
        int r = tests[i].fn();
        if (r != 0) {
            printf("FAILED %s: %s (%s:%d)\n", tests[i].name, last_failure_msg, last_failure_file ? last_failure_file : "?", last_failure_line);
            if (xml) {
                fprintf(xml, "  <testcase name=\"%s\">\n", tests[i].name);
                fprintf(xml, "    <failure>%s (%s:%d)</failure>\n", last_failure_msg, last_failure_file ? last_failure_file : "?", last_failure_line);
                fprintf(xml, "  </testcase>\n");
            }
            ++failures;
        } else {
            printf("OK %s\n", tests[i].name);
            if (xml) fprintf(xml, "  <testcase name=\"%s\"/>\n", tests[i].name);
        }
    }
    if (xml) {
        fprintf(xml, "</testsuite>\n"); fclose(xml);
    }
    if (failures) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
