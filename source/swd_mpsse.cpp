/*
 * Copyright (c) 2012, Anton Staaf, Cliff L. Biffle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the project nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "swd_mpsse.h"
#include "swd_dp.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"

#include <ftdi.h>
#include <unistd.h>

using namespace Log;

using Err::Error;

/*
 * Many of the MPSSE commands expect either an 8- or 16-bit count.  To get the
 * most out of those bits, it encodes a count N as N-1.  These macros produce
 * the individual bytes of the adjusted count.
 */
#define FTH(n) ((((n) - 1) >> 8) & 0xff) // High 8 bits
#define FTL(n) ((((n) - 1) >> 0) & 0xff) // Low 8 bits

uint8_t const swd_header_start  = 1 << 0;

uint8_t const swd_header_ap     = 1 << 1;
uint8_t const swd_header_dp     = 0 << 1;

uint8_t const swd_header_read   = 1 << 2;
uint8_t const swd_header_write  = 0 << 2;

uint8_t const swd_header_parity = 1 << 5;

uint8_t const swd_header_park   = 1 << 7;

/******************************************************************************/
uint8 swd_request(int address, bool debug_port, bool write)
{
    bool        parity  = debug_port ^ write;
    uint8_t     request = (swd_header_start |
                           (debug_port ? swd_header_dp : swd_header_ap) |
                           (write ? swd_header_write : swd_header_read) |
                           ((address & 0x03) << 3) |
                           swd_header_park);

    switch (address & 0x03)
    {
        case 0:
        case 3:
            break;

        case 1:
        case 2:
            parity ^= 1;
            break;
    }

    if (parity)
        request |= swd_header_parity;

    return request;
}
/******************************************************************************/
bool swd_parity(uint32_t data)
{
    uint32_t    step = data ^ (data >> 16);

    step = step ^ (step >> 8);
    step = step ^ (step >> 4);
    step = step ^ (step >> 2);
    step = step ^ (step >> 1);

    return (step & 1);
}
/******************************************************************************/
Error mpsse_setup_buffers(ftdi_context * ftdi)
{
    unsigned    read;
    unsigned    write;

    CheckP(ftdi_usb_purge_buffers(ftdi));

    CheckP(ftdi_read_data_set_chunksize(ftdi, 65536));
    CheckP(ftdi_write_data_set_chunksize(ftdi, 65536));

    CheckP(ftdi_read_data_get_chunksize(ftdi, &read));
    CheckP(ftdi_write_data_get_chunksize(ftdi, &write));

    debug(4, "Chunksize (r/w): %d/%d", read, write);

    return Err::success;
}
/******************************************************************************/
Error mpsse_write(ftdi_context * ftdi, uint8_t * buffer, size_t count)
{
    CheckEQ(ftdi_write_data(ftdi, buffer, count), (int) count);

    return Err::success;
}
/******************************************************************************/
Error mpsse_read(ftdi_context * ftdi,
                 uint8_t * buffer,
                 size_t count,
                 int timeout)
{
    size_t      received = 0;

    /*
     * This is a crude timeout mechanism.  The time that we wait will never
     * be less than the requested number of milliseconds.  But it can certainly
     * be more.
     */
    for (int i = 1; i < timeout + 1; ++i)
    {
        received += CheckP(ftdi_read_data(ftdi,
                                          buffer + received,
                                          count - received));

        if (received >= count)
        {
            debug(5, "MPSSE read took %d attempt%s.", i, i == 1 ? "" : "s");
            return Err::success;
        }

        /*
         * The latency timer is set to 1ms, so we wait that long before trying
         * again.
         */
        usleep(1000);
    }

    debug(5, "MPSSE read failed after %d attempt%s.",
          timeout, timeout == 1 ? "" : "s");

    return Err::timeout;
}
/******************************************************************************/
Error mpsse_synchronize(ftdi_context * ftdi)
{
    uint8_t     commands[] = {0xaa};
    uint8_t     response[2];

    Check(mpsse_write(ftdi, commands, sizeof(commands)));
    Check(mpsse_read (ftdi, response, sizeof(response), 1000));

    CheckEQ(response[0], 0xfa);
    CheckEQ(response[1], 0xaa);

    return Err::success;
}
/******************************************************************************/
Error mpsse_setup(MPSSEConfig const & config,
                  ftdi_context * ftdi,
                  int clock_frequency_hz)
{
    int         divisor    = 30000000 / clock_frequency_hz;
    uint8_t     commands[] =
    {
        DIS_DIV_5,
        DIS_ADAPTIVE,
        DIS_3_PHASE,
        EN_3_PHASE,
        TCK_DIVISOR,   FTL(divisor), FTH(divisor),
        SET_BITS_LOW,
        config.idle_write.low_state,
        config.idle_write.low_direction,
        SET_BITS_HIGH,
        config.idle_write.high_state,
        config.idle_write.high_direction,
    };

    Check(mpsse_setup_buffers(ftdi));

    CheckP(ftdi_set_latency_timer(ftdi, 1));

    CheckP(ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET));
    CheckP(ftdi_set_bitmode(ftdi, 0x00, BITMODE_MPSSE));

    Check(mpsse_synchronize(ftdi));
    Check(mpsse_write(ftdi, commands, sizeof(commands)));

    return Err::success;
}
/******************************************************************************/
Error swd_reset(MPSSEConfig const & config, ftdi_context * ftdi)
{
    uint8_t     commands[] =
    {
        /*
         * Pull SWDIO high
         */
        SET_BITS_LOW,
        config.reset_swd.low_state,
        config.reset_swd.low_direction,
        SET_BITS_HIGH,
        config.reset_swd.high_state,
        config.reset_swd.high_direction,

        /*
         * Generate 50 clocks (6 bytes + 2 bits)
         */
        CLK_BYTES, FTL(6), FTH(6),
        CLK_BITS, FTL(2),

        /*
         * Release SWDIO
         */
        SET_BITS_LOW,
        config.idle_write.low_state,
        config.idle_write.low_direction,
        SET_BITS_HIGH,
        config.idle_write.high_state,
        config.idle_write.high_direction,

        CLK_BITS, FTL(1)
    };

    Check(mpsse_write(ftdi, commands, sizeof(commands)));

    return Err::success;
}
/******************************************************************************/
Error swd_response_to_error(uint8_t response)
{
    switch (response)
    {
        case 1: return Err::success;
        case 2: return Err::try_again;
        case 4: return Err::failure;

        default:
            warning("Received unexpected SWD response %u", response);
            return Err::failure;
    }
}
/******************************************************************************/
MPSSESWDDriver::MPSSESWDDriver(MPSSEConfig const & config,
                               ftdi_context * ftdi) :
    _config(config),
    _ftdi(ftdi)
{
}
/******************************************************************************/
Error MPSSESWDDriver::initialize()
{
    debug(3, "MPSSESWDDriver::initialize");

    Check(mpsse_setup(_config, _ftdi, 1000000));
    Check(swd_reset(_config, _ftdi));

    return Err::success;
}
/******************************************************************************/
Error MPSSESWDDriver::enter_reset()
{
    uint8_t     commands[] =
    {
        SET_BITS_LOW,
        _config.reset_target.low_state,
        _config.reset_target.low_direction,
        SET_BITS_HIGH,
        _config.reset_target.high_state,
        _config.reset_target.high_direction,
    };

    debug(3, "MPSSESWDDriver::enter_reset");

    Check(mpsse_write(_ftdi, commands, sizeof(commands)));

    return Err::success;
}
/******************************************************************************/
Error MPSSESWDDriver::leave_reset()
{
    uint8_t     commands[] =
    {
        SET_BITS_LOW,
        _config.idle_write.low_state,
        _config.idle_write.low_direction,
        SET_BITS_HIGH,
        _config.idle_write.high_state,
        _config.idle_write.high_direction,
    };

    debug(3, "MPSSESWDDriver::leave_reset");

    Check(mpsse_write(_ftdi, commands, sizeof(commands)));

    return Err::success;
}
/******************************************************************************/
Error MPSSESWDDriver::read(unsigned address, bool debug_port, uint32_t * data)
{
    debug(3, "MPSSESWDDriver::read(%08X, %d)", address, debug_port);

    uint8_t     request_commands[] =
    {
        // Write SWD header
        MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(8),
        swd_request(address, debug_port, false),
        // Turn the bidirectional data line around
        SET_BITS_LOW,
        _config.idle_read.low_state,
        _config.idle_read.low_direction,
        SET_BITS_HIGH,
        _config.idle_read.high_state,
        _config.idle_read.high_direction,
        // And clock out one bit
        CLK_BITS, FTL(1),
        // Now read in the target response
        MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(3),
    };

    uint8_t     data_commands[] =
    {
        // Then read in the target data
        MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB, FTL(4), FTH(4),
        // And finally read in the target parity and turn around
        MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(2),
    };

    uint8_t     cleanup_commands[] =
    {
        // Turn the bidirectional data line back to an output
        SET_BITS_LOW,
        _config.idle_write.low_state,
        _config.idle_write.low_direction,
        SET_BITS_HIGH,
        _config.idle_write.high_state,
        _config.idle_write.high_direction,
        // And clock out one bit
        CLK_BITS, FTL(1),
    };

    uint8_t     response[6] = {0};

    // response[0]: the three-bit response, MSB-justified.
    Check(mpsse_write(_ftdi, request_commands, sizeof(request_commands)));
    Check(mpsse_read(_ftdi, response, 1, 1000));

    uint8_t     ack = response[0] >> 5;

    debug(4, "SWD read got response %u", ack);

    if (ack == 0x01)
    {
        uint32_t        temp;

        // SWD OK
        // Read the data phase.
        // response[4:1]: the 32-bit response word.
        // response[5]: the parity bit in bit 6, turnaround (ignored) in bit 7.
        Check(mpsse_write(_ftdi, data_commands, sizeof(data_commands)));
        Check(mpsse_read(_ftdi, response + 1, sizeof(response) - 1, 1000));

        temp = (response[1] <<  0 |
                response[2] <<  8 |
                response[3] << 16 |
                response[4] << 24);

        // Check for parity error.
        CheckEQ((response[5] >> 6) & 1, swd_parity(temp));

        if (data)
            *data = temp;

        debug(4, "SWD read (%X, %d) = %08X complete with status %d",
              address, debug_port, temp, ack);
    }

    Check(mpsse_write(_ftdi, cleanup_commands, sizeof(cleanup_commands)));

    return swd_response_to_error(ack);
}
/******************************************************************************/
Error MPSSESWDDriver::write(unsigned address, bool debug_port, uint32_t data)
{
    bool        parity             = swd_parity(data);
    uint8_t     request_commands[] =
    {
        // Write SWD header
        MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(8),
        swd_request(address, debug_port, true),
        // Turn the bidirectional data line around
        SET_BITS_LOW,
        _config.idle_read.low_state,
        _config.idle_read.low_direction,
        SET_BITS_HIGH,
        _config.idle_read.high_state,
        _config.idle_read.high_direction,
        // And clock out one bit
        CLK_BITS, FTL(1),
        // Now read in the target response
        MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(3),
        // Turn the bidirectional data line back to an output
        SET_BITS_LOW,
        _config.idle_write.low_state,
        _config.idle_write.low_direction,
        SET_BITS_HIGH,
        _config.idle_write.high_state,
        _config.idle_write.high_direction,
        // And clock out one bit
        CLK_BITS, FTL(1),
    };

    uint8_t     data_commands[] =
    {
        // Write the data
        MPSSE_DO_WRITE | MPSSE_LSB, FTL(4), FTH(4),
        (data >>  0) & 0xff,
        (data >>  8) & 0xff,
        (data >> 16) & 0xff,
        (data >> 24) & 0xff,
        // And finally write the parity bit
        MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(1),
        parity ? 0xff : 0x00,
    };

    debug(3, "MPSSESWDDriver::write(%08X, %d, %08X)",
          address, debug_port, data);

    uint8_t     response[1] = {0};

    Check(mpsse_write(_ftdi, request_commands, sizeof(request_commands)));
    Check(mpsse_read (_ftdi, response, sizeof(response), 1000));

    uint8_t     ack = response[0] >> 5;

    debug(4, "SWD write got response %u", ack);

    if (ack == 0x01)
        Check(mpsse_write(_ftdi, data_commands, sizeof(data_commands)));

    return swd_response_to_error(ack);
}
/******************************************************************************/
