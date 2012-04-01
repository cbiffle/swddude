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

#include "target.h"
#include "swd_dp.h"
#include "swd_mpsse.h"
#include "swd.h"

#include "rptr.h"

#include "armv6m_v7m.h"
#include "arm.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"
#include "libs/command_line/command_line.h"

#include <vector>

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>
#include <termios.h>
#include <signal.h>

using namespace Log;
using Err::Error;

using namespace ARM;
using namespace ARMv6M_v7M;

using std::vector;


/*******************************************************************************
 * Command line flags
 */
namespace CommandLine
{
    static Scalar<int>
    debug("debug", true, 0, "What level of debug logging to use.");

    static Scalar<String>
    programmer("programmer", true, "um232h", "FTDI-based programmer to use");

    static Scalar<int> vid("vid", true, 0, "FTDI VID");
    static Scalar<int> pid("pid", true, 0, "FTDI PID");

    static Scalar<int>
    interface("interface", true, 0, "Interface on FTDI chip");

    static Scalar<bool>
    local_echo("local-echo", true, false, "Whether to echo keystrokes");

    static Argument * arguments[] =
    {
        &debug,
        &programmer,
        &vid,
        &pid,
        &interface,
        &local_echo,
        NULL
    };
}

/*******************************************************************************
 * Implements the semihosting SYS_WRITEC operation.
 */
Error write_char(Target & target, word_t parameter)
{
    debug(2, "SYS_WRITEC %02X", parameter);
    putchar(parameter);
    fflush(stdout);
    return Err::success;
}

/*******************************************************************************
 * Implements the semihosting SYS_WRITE0 operation.
 */
Error write_str(Target & target, word_t parameter)
{
    debug(2, "SYS_WRITE0 %08X", parameter);
    /*
     * This is a byte string, but the target only supports 32-bit accesses.
     * So, we have to jump through some hoops to transfer it.
     */

    rptr_const<word_t> str_word_addr(parameter & ~0x3);
    word_t str_word;
    Check(target.read_word(str_word_addr, &str_word));

    unsigned bytes_left_in_word = 4 - (parameter & 0x3);

    while (true)
    {
        while (bytes_left_in_word)
        {
            char c = str_word & 0xFF;
            str_word >>= 8;
            --bytes_left_in_word;

            if (c) putchar(c);
            else goto end_of_string;
        }

        Check(target.read_word(++str_word_addr, &str_word));
        bytes_left_in_word = 4;
    }

end_of_string:
    fflush(stdout);
    return Err::success;
}

/*******************************************************************************
 * Implements the semihosting SYS_READC operation.
 */
Error read_char(Target & target, word_t parameter)
{
    debug(2, "SYS_READC");
    /*
     * Note: the SYS_READC operation defines no standard way of handling EOF!
     * We just pass it to the target.
     */

    int c = getchar();
    Check(target.write_register(Register::R0, c));

    return Err::success;
}


/*******************************************************************************
 * Inspects the CPU's halt conditions to see whether semihosting has been
 * invoked.
 */
Error handle_halt(Target & target)
{
    word_t dfsr;
    CheckRetry(target.read_word(SCB::DFSR, &dfsr), 100);

    if ((dfsr & SCB::DFSR_reason_mask) == SCB::DFSR_BKPT)
    {
        word_t pc;
        CheckRetry(target.read_register(Register::PC, &pc), 100);

        /*
         * Targets may only support 32-bit accesses, but the PC is 16-bit
         * aligned.  Find the address of the word containing the current
         * instruction and load the whole thing.
         */
        rptr<word_t> instr_word_address(pc & ~0x3);
        word_t instr_word;
        CheckRetry(target.read_word(instr_word_address, &instr_word), 100);

        /*
         * Extract the instruction halfword from the word we've read.
         */
        halfword_t instr = pc & 2 ? instr_word >> 16 : instr_word & 0xFFFF;

        if (instr == 0xBEAB)
        {
            /*
             * The semihosting ABI, summarized, goes something like this:
             *  - Operation code in R0.
             *  - Single 32-bit parameter, or pointer to memory block containing
             *    more parameters, in R1.
             *  - Return value in R0 (either 32-bit value or pointer).
             */
            word_t operation;
            CheckRetry(target.read_register(Register::R0, &operation), 100);

            word_t parameter;
            CheckRetry(target.read_register(Register::R1, &parameter), 100);

            switch (operation)
            {
                case 0x3:
                    Check(write_char(target, parameter));
                    break;

                case 0x4:
                    Check(write_str(target, parameter));
                    break;

                case 0x7:
                    Check(read_char(target, parameter));
                    break;

                default:
                    warning("Unsupported semihosting operation 0x%X",
                            operation);
                    return Err::failure;
            }

            /*
             * Success!  Advance target PC past the breakpoint and resume.
             */
            pc += 2;
            CheckRetry(target.write_register(Register::PC, pc), 100);
            Check(target.resume());

            return Err::success;
        }
        else
        {
            warning("Unexpected non-semihosting breakpoint %04X @%08X",
                    instr,
                    pc);
            return Err::failure;
        }
    }
    else
    {
        warning("Processor halted for unexpected reason 0x%X", dfsr);
        return Err::failure;
    }
}

/*******************************************************************************
 * Semihosting tool entry point.
 */
static termios stored_settings;
static struct sigaction previous_signal;

static void restore_terminal()
{
    tcsetattr(0, TCSANOW, &stored_settings);
}

static void int_handler(int signal)
{
    restore_terminal();
    fflush(stdout);
    exit(1);
}

Error host_main(SWDDriver & swd)
{
    /*
     * Hook SIGINT to ensure that we can restore terminal settings on ^C.
     */
    struct sigaction action;
    action.sa_handler = int_handler;
    action.sa_flags   = SA_RESETHAND;

    CheckP(sigemptyset(&action.sa_mask));
    CheckP(sigaction(SIGINT, &action, &previous_signal));

    /*
     * Make stdin unbuffered and disable echo.
     */
    tcgetattr(0, &stored_settings);

    termios unbuffered = stored_settings;
    unbuffered.c_lflag &= ~ICANON;
    if (!CommandLine::local_echo.get()) unbuffered.c_lflag &= ~ECHO;

    tcsetattr(0, TCSANOW, &unbuffered);

    /*
     * On with the semi-hosting!
     */
    DebugAccessPort dap(swd);
    Target target(swd, dap, 0);

    uint32_t idcode;
    Check(swd.initialize(&idcode));

    Check(swd.enter_reset());
    usleep(10000);
    Check(dap.reset_state());
    Check(target.initialize());
    Check(target.reset_halt_state());

    Check(swd.leave_reset());

    while (true)
    {
        word_t dhcsr;
        CheckRetry(target.read_word(DCB::DHCSR, &dhcsr), 100);

        if (dhcsr & DCB::DHCSR_S_HALT)
        {
            Check(handle_halt(target));
        }
    }
     

    return Err::success;
}


/*******************************************************************************
 * Entry point (sort of -- see main below)
 */

static Error error_main(int argc, char const * * argv)
{
    MPSSEConfig config;
    MPSSE       mpsse;

    Check(lookup_programmer(CommandLine::programmer.get(), &config));

    if (CommandLine::interface.set())
        config.interface = CommandLine::interface.get();

    if (CommandLine::vid.set())
        config.vid = CommandLine::vid.get();

    if (CommandLine::pid.set())
        config.pid = CommandLine::pid.get();

    Check(mpsse.open(config));

    MPSSESWDDriver swd(config, &mpsse);

    Check(host_main(swd));

    return Err::success;
}

/******************************************************************************/
int main(int argc, char const * * argv)
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
