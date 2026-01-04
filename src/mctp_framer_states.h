/**
 * @file mctp_framer_states.h
 * @brief Internal MCTP framer state definitions (single source of truth).
 *
 * @internal This header is internal to the `src/` implementation and is
 * included by production code and test-only hooks to avoid duplicate
 * definitions. Do not expose these symbols as part of the public API.
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

#ifndef MCTP_FRAMER_STATES_H
#define MCTP_FRAMER_STATES_H

#define MCTPSER_WAITING_FOR_SYNC 0
#define MCTPSER_HEADER1 1
#define MCTPSER_HEADER2 2
#define MCTPSER_BODY 3
#define MCTPSER_FCS1 4
#define MCTPSER_FCS2 5
#define MCTPSER_END 6
#define MCTPSER_ESCAPE 7
#define MCTPSER_AWAITING_RESPONSE 8
#define SENDING_RESPONSE 9

#endif /* MCTP_FRAMER_STATES_H */
