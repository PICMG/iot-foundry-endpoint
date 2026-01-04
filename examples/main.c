/**
 * @file main.c
 * @brief Example usage for IoTFoundry endpoint code.
 *
 * Initializes platform and MCTP subsystems, then runs the main polling
 * loop which processes incoming MCTP packets.
 *
 * @author Douglas Sandy
 *
 * MIT License
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

#include "mctp.h"
#include "platform.h"

#ifdef PLDM_SUPPORT
#include "pldm_version.h"
#endif

/**
 * @brief Program entry point.
 *
 * This function initializes the MCTP subsystem and platform hardware,
 * then enters the main loop which repeatedly updates the MCTP framer
 * and processes any available packets. Control and PLDM packets are
 * dispatched to their respective handlers; other packets are ignored.
 *
 * @return int Returns 0 on normal termination (never reached in typical
 *             embedded runtime where main runs indefinitely).
 */
int main(void) {
    /* initialize the mctp subsystem */
    mctp_init();

    while (1) {
        /* update the mctp framer state */
        mctp_update();

        /* process_packet */
        if (mctp_is_packet_available()) {
            if (mctp_is_control_packet()) {
                mctp_process_control_message();
            }
#ifdef PLDM_SUPPORT
            else if (mctp_is_pldm_packet()) {
                pldm_process_packet();
            }
#endif
            else {
                // non-control packet - drop packet
                mctp_ignore_packet();
            }
        }

        /* other application tasks can be added here */
    }
    return 0;
}
