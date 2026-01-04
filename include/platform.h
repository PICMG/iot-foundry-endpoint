/**
 * @file platform.h
 * @brief Minimal platform API expected by production code and tests.
 *
 * This header declares the minimal serial API used by the MCTP
 * implementation. Tests provide a mock implementation under `tests/`.
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

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/**
 * @brief Initialize platform hardware.
 *
 * This function is called once by mctp_init to initialize
 * platform-specific hardware (serial interfaces, timers, etc.).
 */
void platform_init(void);

/**
 * @brief Query whether data is available to read from the serial interface.
 *
 * @return uint8_t Returns non-zero when data is available to read.
 */
uint8_t platform_serial_has_data(void);

/**
 * @brief Read a byte from the serial interface. May block if no data is available.
 *
 * @return uint8_t The byte read from the serial interface.
 */
uint8_t platform_serial_read_byte(void);

/**
 * @brief Write a byte to the serial interface. May block if the interface is not ready.
 *
 * @param b The byte to write.
 */
void platform_serial_write_byte(uint8_t b);

/**
 * @brief Query whether the serial interface can accept writes.
 *
 * @return uint8_t Returns non-zero when writes are currently allowed.
 */
uint8_t platform_serial_can_write(void);

#endif /* PLATFORM_H */
