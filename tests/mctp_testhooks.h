/**
 * @file mctp_testhooks.h
 * @brief Test-only hooks for exercising `mctp` internals from tests.
 *
 * This header intentionally exposes selected internal symbols for the test
 * runner. It is not part of the public API and lives under `tests/`.
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

#ifndef MCTP_TESTHOOKS_H
#define MCTP_TESTHOOKS_H

#include <stdint.h>

/* Reuse canonical framer-state definitions from src/ to avoid duplication */
#include "../src/mctp_framer_states.h"
/* Internal buffer and state (available to tests) */
extern uint8_t mctp_buffer[];
extern uint8_t buffer_idx;
extern uint8_t rxState;

#endif /* MCTP_TESTHOOKS_H */
