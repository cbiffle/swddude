/*
 * Copyright (c) 2012, Anton Staaf, Cliff L. Biffle.
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

#include "source/mpsse_config.h"

#include "libs/error/error.h"

using namespace Err;

MPSSEConfig const       um232h_config =
{
    0x0403, 0x6014, 0,
    {0x09, 0x09, 0x00, 0x00}, //idle read
    {0x09, 0x0b, 0x00, 0x00}, //idle write
    {0x01, 0x0b, 0x00, 0x00}, //reset target
    {0x0b, 0x0b, 0x00, 0x00}, //reset swd
};

MPSSEConfig const       bus_blaster_config =
{
    0x0403, 0x6010, 0,
    {0x09, 0x29, 0xb7, 0x58}, //idle read
    {0x09, 0x2b, 0xa7, 0x58}, //idle write
    {0x01, 0x2b, 0xa5, 0x5A}, //reset target
    {0x0b, 0x2b, 0xa7, 0x58}, //reset swd
};

/******************************************************************************/
Error lookup_programmer(String name, MPSSEConfig * config)
{
    if      (name.equal("um232h"))      *config = um232h_config;
    else if (name.equal("bus_blaster")) *config = bus_blaster_config;
    else return Err::failure;

    return Err::success;
}
/******************************************************************************/
