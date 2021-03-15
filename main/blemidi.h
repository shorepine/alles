/*
 * BLE MIDI Driver
 *
 * See README.md for usage hints
 *
 * =============================================================================
 *
 * MIT License
 *
 * Copyright (c) 2019 Thorsten Klose (tk@midibox.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * =============================================================================
 */

#ifndef _BLEMIDI_H
#define _BLEMIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>


#define BLEMIDI_DEVICE_NAME "Alles MIDI"
#define BLEMIDI_TAG "BLEMIDI"
#define BLEMIDI_NUM_PORTS 2
#define BLEMIDI_OUTBUFFER_FLUSH_MS 30


int32_t blemidi_receive_packet(uint8_t blemidi_port, uint8_t *stream,
                                      size_t len,
                                      void *_callback_midi_message_received);


/**
 * @brief Initializes the BLEMIDI Server
 *
 * @param  callback_midi_message_received References the callback function which
 * is called whenever a new MIDI message has been received. API see
 * blemidi_receive_packet_callback_for_debugging Specify NULL if no callback
 * required in your application.
 */
extern int32_t blemidi_init(void *callback_midi_message_received);
extern int32_t blemidi_deinit();

/**
 * @brief Sends a BLE MIDI message
 *
 * @param  blemidi_port currently always 0 expected (we might support multiple
 * ports in future)
 * @param  stream       output stream
 * @param  len          output stream length
 *
 * @return < 0 on errors
 *
 */
extern int32_t blemidi_send_message(uint8_t blemidi_port, uint8_t *stream,
                                    size_t len);

/**
 * @brief A dummy callback which demonstrates the usage.
 *        It will just print out incoming MIDI messages on the terminal.
 *        You might want to implement your own for doing something more useful!

 * @param  blemidi_port currently always 0 expected (we might support multiple
 ports in future)
 * @param  timestamp    the timestamp
 * @param  midi_status  the MIDI status byte (first byte of a MIDI message)
 * @param  remaining_message the remaining bytes
 * @param  len          size of the remaining message
 * @param  continued_sysex_pos in case the next part of a SysEx stream has been
 received, this variable is >0 and passes the position of the continued sysex
 stream
 *
 * @return < 0 on errors
 */
extern void blemidi_receive_packet_callback_for_debugging(
    uint8_t blemidi_port, uint16_t timestamp, uint8_t midi_status,
    uint8_t *remaining_message, size_t len, size_t continued_sysex_pos);

/**
 * @brief This function should be called each mS to update the timestamp and
 * flush the output buffer
 *
 * @return < 0 on errors
 */
extern void blemidi_tick();


#ifdef __cplusplus
}
#endif

#endif /* _BLEMIDI_H */
