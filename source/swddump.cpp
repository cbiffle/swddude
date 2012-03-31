/*
 * Copyright (c) 2012, Anton Staaf
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

#include "target.h"
#include "swd_dp.h"
#include "swd_mpsse.h"
#include "swd.h"
#include "arm.h"
#include "lpc11xx_13xx.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"
#include "libs/command_line/command_line.h"

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>

using Err::Error;

using namespace Log;
using namespace ARM;
using namespace LPC11xx_13xx;

/******************************************************************************/
namespace CommandLine
{
    static Scalar<int> debug ("debug",  true,  0,
                              "What level of debug logging to use.");

    static Scalar<int> count("count", true, 32, "Words to dump");

    static Argument * arguments[] =
    {
        &debug,
        &count,
        null
    };
}
/******************************************************************************/
static Error unmap_boot_sector(Target & target)
{ 
    return target.write_word(SYSCON::SYSMEMREMAP,
                             SYSCON::SYSMEMREMAP_MAP_USER_FLASH);
}
/******************************************************************************/
static Error dump_flash(Target & target, unsigned n)
{
    word_t buffer;

    notice("First %u words of Flash:", n);

    rptr_const<word_t> const top(n * sizeof(word_t));

    for (rptr_const<word_t> i(0); i < top; ++i)
    {
        Check(target.read_word(i, &buffer));
        notice(" [%08X] %08X", i.bits(), buffer);
    }

    return Err::success;
}
/******************************************************************************/
static Error run_experiment(ftdi_context & ftdi)
{
    MPSSESWDDriver swd(um232h_config, &ftdi);
    Check(swd.initialize(NULL));
    Check(swd.enter_reset());
    usleep(100000);
    Check(swd.leave_reset());

    DebugAccessPort dap(swd);
    Check(dap.reset_state());

    Target target(swd, dap, 0);
    Check(target.initialize());
    Check(target.halt());

    Check(unmap_boot_sector(target));
    Check(dump_flash(target, CommandLine::count.get()));

    return Err::success;
}
/******************************************************************************/
static Error error_main(int argc, char const ** argv)
{
    Error check_error = Err::success;
    ftdi_context ftdi;

    CheckCleanupP(ftdi_init(&ftdi), init_failed);

    CheckCleanupStringP(ftdi_usb_open(&ftdi, 0x0403, 0x6014), open_failed,
                        "Unable to open FTDI device: %s",
                        ftdi_get_error_string(&ftdi));

    CheckCleanupP(ftdi_usb_reset(&ftdi), reset_failed);
    CheckCleanupP(ftdi_set_interface(&ftdi, INTERFACE_A), interface_failed);

    unsigned chipid;
    CheckCleanupP(ftdi_read_chipid(&ftdi, &chipid), read_failed);
    debug(1, "FTDI chipid: %X", chipid);

    CheckCleanup(run_experiment(ftdi), experiment_failed);

  experiment_failed:
    CheckP(ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_RESET));

  read_failed:
  interface_failed:
  reset_failed:
    CheckStringP(ftdi_usb_close(&ftdi),
                 "Unable to close FTDI device: %s",
                 ftdi_get_error_string(&ftdi));

  open_failed:
    ftdi_deinit(&ftdi);

  init_failed:
    return check_error;
}
/******************************************************************************/
int main(int argc, char const ** argv)
{
    Error check_error = Err::success;

    CheckCleanup(CommandLine::parse(argc, argv, CommandLine::arguments),
                 failure);

    log().set_level(CommandLine::debug.get());

    CheckCleanup(error_main(argc, argv), failure);
    return 0;

  failure:
    Err::stack()->print();
    return 1;
}
/******************************************************************************/
