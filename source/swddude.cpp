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
#include "arm.h"

#include "lpc11xx_13xx.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"
#include "libs/command_line/command_line.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>

#define __STDC_FORMAT_MACROS

#include <unistd.h>
#include <stdio.h>
#include <libusb.h>
#include <ftdi.h>
#include <inttypes.h>

using Err::Error;

using namespace Log;
using namespace ARM;
using namespace LPC11xx_13xx;

using std::vector;
using std::ifstream;
using std::ios;

/*******************************************************************************
 * Command-line definitions
 */

namespace CommandLine
{
    static Scalar<int>
    debug ("debug",  true,  0,
           "What level of debug logging to use.");

    static Scalar<String>
    flash("flash", true, "",
          "Binary program to load");

    static Scalar<String>
    programmer("programmer", true, "um232h",
               "FTDI based programmer to use");

    static Scalar<bool>
    fix_lpc_checksum("fix_lpc_checksum", true, false,
                     "When true, the loader will write the LPC-style "
                     "checksum.");

    static Scalar<int>
    vid("vid", true, 0,
        "FTDI VID");

    static Scalar<int>
    pid("pid", true, 0,
        "FTDI PID");

    static Scalar<int>
    interface("interface", true, 0,
              "FTDI interface");

    static Argument     *arguments[] =
    {
        &debug,
        &flash,
        &programmer,
        &fix_lpc_checksum,
        &vid,
        &pid,
        &interface,
        NULL
    };
}


/*******************************************************************************
 * Flash programming implementation
 */

/*
 * Invokes a routine within In-Application Programming ROM of an LPC part.
 */
static Error invoke_iap(Target & target,
                        rptr<word_t> param_table,
                        rptr<word_t> result_table,
                        rptr<word_t> stack)
{
    debug(2, "invoke_iap: param_table=%08X, result_table=%08X, stack=%08X",
          param_table.bits(),
          result_table.bits(),
          stack.bits());

    Check(target.write_register(Register::R0, param_table));
    Check(target.write_register(Register::R1, result_table));
    Check(target.write_register(Register::SP, stack));
    Check(target.write_register(Register::PC, IAP::entry));

    // Tell the CPU to return into RAM, and catch it there with a breakpoint.
    rptr_const<thumb_code_t> trap(param_table.bits() | 1);
    Check(target.write_register(Register::LR, trap));
    Check(target.enable_breakpoint(0, trap));

    Check(target.reset_halt_state());

    Check(target.resume());

    bool halted = false;
    uint32_t attempts = 0;
    do
    {
        Check(target.is_halted(&halted));
        usleep(10000);
    }
    while (++attempts < 100 && !halted);

    if (!halted)
    {
        warning("Target did not halt after IAP execution!");
        Check(target.halt());

        uint32_t pc;
        Check(target.read_register(Register::PC, &pc));
        warning("Target forceably halted at %08X", pc);

        return Err::failure;
    }

    return Err::success;
}

/*
 * Unmaps the bootloader ROM from address 0 in an LPC part, revealing user flash
 * sector 0 beneath.
 *
 * This operation is valid for at least the following micros:
 *  - LPC111x / LPC11Cxx
 *  - LPC13xx
 *
 * The current implementation won't work on the LPC17xx.
 */
static Error unmap_boot_sector(Target & target)
{
    return target.write_word(SYSCON::SYSMEMREMAP,
                             SYSCON::SYSMEMREMAP_MAP_USER_FLASH);
}

static Error unprotect_flash(Target & target,
                             rptr<word_t> work_addr,
                             uint32_t first_sector,
                             uint32_t last_sector)
{
    debug(1, "Unprotecting Flash sectors %"PRIu32"-%"PRIu32"...",
          first_sector,
          last_sector);

    rptr<word_t> const cmd_addr (work_addr);
    rptr<word_t> const resp_addr(cmd_addr);  // Reuse same space.
    rptr<word_t> const stack_top(cmd_addr + IAP::max_command_response_words
                                          + IAP::min_stack_words);

    // Build command table
    Check(target.write_word(cmd_addr + 0, IAP::Command::unprotect_sectors));
    Check(target.write_word(cmd_addr + 1, first_sector));
    Check(target.write_word(cmd_addr + 2, last_sector));

    Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

    uint32_t iap_result;
    Check(target.read_word(resp_addr + 0, &iap_result));
    CheckEQ(iap_result, 0);

    return Err::success;
}

static Error erase_flash(Target & target,
                         rptr<word_t> work_addr,
                         uint32_t first_sector,
                         uint32_t last_sector)
{
    debug(1, "Erasing Flash sectors %"PRIu32"-%"PRIu32"...",
          first_sector,
          last_sector);

    rptr<word_t> const cmd_addr (work_addr);
    rptr<word_t> const resp_addr(cmd_addr);  // Reuse same space.
    rptr<word_t> const stack_top(cmd_addr + IAP::max_command_response_words
                                          + IAP::min_stack_words);

    Check(target.write_word(cmd_addr + 0, IAP::Command::erase_sectors));
    Check(target.write_word(cmd_addr + 1, first_sector));
    Check(target.write_word(cmd_addr + 2, last_sector));
    Check(target.write_word(cmd_addr + 3, 12000));  // TODO hard-coded clock

    Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

    uint32_t iap_result;
    Check(target.read_word(resp_addr + 0, &iap_result));
    CheckEQ(iap_result, 0);

    return Err::success;
}

static Error copy_ram_to_flash(Target & target,
                               rptr<word_t> work_addr,
                               rptr<word_t> src_addr,
                               rptr<word_t> dest_addr,
                               size_t num_bytes)
{
    rptr<word_t> const cmd_addr (work_addr);
    rptr<word_t> const resp_addr(cmd_addr);  // Reuse same space.
    rptr<word_t> const stack_top(cmd_addr + IAP::max_command_response_words
                                          + IAP::min_stack_words);

    debug(1, "Writing Flash: %zu bytes at %"PRIx32,
          num_bytes,
          dest_addr.bits());

    Check(target.write_word(cmd_addr + 0, IAP::Command::copy_ram_to_flash));
    Check(target.write_word(cmd_addr + 1, dest_addr.bits()));
    Check(target.write_word(cmd_addr + 2, src_addr.bits()));
    Check(target.write_word(cmd_addr + 3, num_bytes));
    Check(target.write_word(cmd_addr + 4, 12000));  // TODO hard-coded clock

    Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

    uint32_t iap_result;
    Check(target.read_word(resp_addr + 0, &iap_result));
    CheckEQ(iap_result, 0);

    return Err::success;
}


/*
 * Rewrites the target's flash memory.
 */
static Error program_flash(Target & target,
                           word_t const * program,
                           size_t word_count)
{
    size_t const bytes_per_block = 256;
    size_t const words_per_block = bytes_per_block / sizeof(word_t);

    size_t const bytes_per_sector = 4096;
    size_t const words_per_sector = bytes_per_sector / sizeof(word_t);

    rptr<word_t> const ram_buffer(0x10000000);
    rptr<word_t> const work_area(ram_buffer + words_per_block);

    size_t const last_sector = word_count / words_per_sector;
    size_t const block_count =
        (word_count + words_per_block - 1) / words_per_block;

    // Ensure that the boot Flash isn't visible (will mess us up).
    Check(unmap_boot_sector(target));

    // Erase the current contents of Flash.  (TODO: make optional?)
    Check(unprotect_flash(target, work_area, 0, last_sector));
    Check(erase_flash(target, work_area, 0, last_sector));

    // Copy program to RAM, then to Flash, in 256 byte chunks.
    for (unsigned block = 0; block < block_count; ++block)
    {
        size_t block_offset = block * words_per_block;
        rptr<word_t> block_address(block_offset * sizeof(word_t));

        size_t current_block_words =
            std::max(word_count - block_offset, words_per_block);

        // Copy a block to RAM...
        debug(1, "Copying %zu words starting with #%u to %08X",
              current_block_words,
              block,
              ram_buffer.bits());

        Check(target.write_words(&program[block_offset],
                                 ram_buffer,
                                 current_block_words));

        // ...and write it to Flash.
        unsigned sector = block_address.bits() / bytes_per_sector;
        Check(unprotect_flash(target, work_area, sector, sector));

        Check(copy_ram_to_flash(target,
                                work_area,
                                ram_buffer,
                                block_address,
                                bytes_per_block));
    }

    return Err::success;
}

/*
 * Dumps the first 256 bytes of the target's flash to the console.
 */
static Error dump_flash(Target & target)
{
    word_t buffer[256 / sizeof(word_t)];
    size_t buffer_size = sizeof(buffer) / sizeof(buffer[0]);

    Check(target.read_words(rptr<word_t>(0), buffer, buffer_size));

    notice("Contents of Flash:");
    for (unsigned i = 0; i < buffer_size; ++i)
    {
        notice(" [%08zX] %08"PRIX32, i * sizeof(word_t), buffer[i]);
    }

    return Err::success;
}


/*******************************************************************************
 * swddude main implementation
 */

static void fix_lpc_checksum(char * program, size_t program_length)
{
    size_t const checked_vectors = 7;

    if (program_length < checked_vectors * sizeof(word_t)) {
        warning("Program too short to write LPC checksum.");
        return;
    }

    word_t * program_words = (word_t *) program;

    word_t sum = 0;
    for (size_t i = 0; i < checked_vectors; ++i)
    {
        sum += program_words[i];
    }
    sum = 0 - sum;

    debug(1, "Repairing LPC checksum: %"PRIX32, sum);

    program_words[checked_vectors] = sum;
}

static Error flash_from_file(Target & target, char const * path)
{
    Error check_error = Err::success;
    char * program = 0;
    ifstream input;

    input.open(path);

    // Do the "get length of file" dance.
    input.seekg(0, ios::end);
    size_t input_length = input.tellg();
    input.seekg(0, ios::beg);

    CheckCleanupEQ(input_length,
                   (input_length / sizeof(word_t)) * sizeof(word_t),
                   wrong_size);

    program = new char[input_length];

    input.read(program, input_length);

    debug(1, "Read program of %zu bytes", input_length);

    if (CommandLine::fix_lpc_checksum.get())
    {
        fix_lpc_checksum(program, input_length);
    }

    CheckCleanup(program_flash(target,
                               (word_t *) program,
                               input_length / sizeof(word_t)),
                 comms_failure);

    CheckCleanup(dump_flash(target), comms_failure);

comms_failure:
wrong_size:
    input.close();
    if (program) delete[] program;
    return check_error;
}

static Error run_experiment(SWDDriver & swd)
{
    Error check_error = Err::success;

    DebugAccessPort dap(swd);
    Target target(swd, dap, 0);

    Check(swd.initialize());

    // Set up the initial DAP configuration while the target is in reset.
    // The STM32 wants us to do this, and the others don't seem to mind.
    Check(swd.enter_reset());
    usleep(10000);
    Check(dap.reset_state());
    Check(target.initialize());
    Check(target.reset_halt_state());
    Check(swd.leave_reset());
    usleep(100000);

    Check(target.halt());
    Check(target.reset_and_halt());

    // Scope out the breakpoint unit.
    Check(target.enable_breakpoints());
    size_t breakpoint_count;
    Check(target.get_breakpoint_count(&breakpoint_count));
    notice("Target supports %zu hardware breakpoints.", breakpoint_count);

    if (breakpoint_count == 0)
    {
        warning("Can't continue!");
        return Err::success;  // Prevent stack trace
    }

    // Flash if requested.
    if (CommandLine::flash.set())
    {
        CheckCleanup(flash_from_file(target, CommandLine::flash.get()),
                     comms_failure);
    }

comms_failure:
    Check(swd.enter_reset());
    usleep(100000);
    Check(swd.leave_reset());

    return check_error;
}

static Error lookup_programmer(String name, MPSSEConfig const * * config)
{
    if      (name.equal("um232h"))      *config = &um232h_config;
    else if (name.equal("bus_blaster")) *config = &bus_blaster_config;
    else return Err::failure;

    return Err::success;
}

/*******************************************************************************
 * Entry point (sort of -- see main below)
 */

static Error error_main(int argc, char const ** argv)
{
    Error                       check_error = Err::success;
    libusb_context *            libusb;
    libusb_device_handle *      handle;
    libusb_device *             device;
    ftdi_context                ftdi;
    MPSSEConfig const *         config;

    Check(lookup_programmer(CommandLine::programmer.get(), &config));

    ftdi_interface interface = ftdi_interface(INTERFACE_A +
                                              config->default_interface);
    uint16_t       vid       = config->default_vid;
    uint16_t       pid       = config->default_pid;

    if (CommandLine::interface.set())
        interface = ftdi_interface(INTERFACE_A + CommandLine::interface.get());

    if (CommandLine::vid.set())
        vid = CommandLine::vid.get();

    if (CommandLine::pid.set())
        pid = CommandLine::pid.get();

    CheckCleanupP(libusb_init(&libusb), libusb_init_failed);
    CheckCleanupP(ftdi_init(&ftdi), ftdi_init_failed);

    /*
     * Locate FTDI chip using it's VID:PID pair.  This doesn't uniquely identify
     * the programmer so this will need to be improved.
     */
    handle = libusb_open_device_with_vid_pid(libusb, vid, pid);

    CheckCleanupStringB(handle, libusb_open_failed,
                        "No device found with VID:PID = 0x%04x:0x%04x\n",
                        vid, pid);

    CheckCleanupB(device = libusb_get_device(handle), get_failed);

    /*
     * The interface must be selected before the ftdi device can be opened.
     */
    CheckCleanupStringP(ftdi_set_interface(&ftdi, interface),
                        interface_failed,
                        "Unable to set FTDI device interface: %s",
                        ftdi_get_error_string(&ftdi));

    CheckCleanupStringP(ftdi_usb_open_dev(&ftdi, device),
                        open_failed,
                        "Unable to open FTDI device: %s",
                        ftdi_get_error_string(&ftdi));

    CheckCleanupStringP(ftdi_usb_reset(&ftdi),
                        reset_failed,
                        "FTDI device reset failed: %s",
                        ftdi_get_error_string(&ftdi));

    {
        unsigned        chipid;

        CheckCleanupP(ftdi_read_chipid(&ftdi, &chipid), read_failed);

        debug(3, "FTDI chipid: %X", chipid);
    }

    {
        MPSSESWDDriver swd(*config, &ftdi);
        CheckCleanup(run_experiment(swd), experiment_failed);
    }

  experiment_failed:
    CheckP(ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_RESET));

  read_failed:
  reset_failed:
    CheckStringP(ftdi_usb_close(&ftdi),
                 "Unable to close FTDI device: %s",
                 ftdi_get_error_string(&ftdi));

  open_failed:
  interface_failed:
  get_failed:
    libusb_close(handle);

  libusb_open_failed:
    ftdi_deinit(&ftdi);

  ftdi_init_failed:
    libusb_exit(libusb);

  libusb_init_failed:
    return check_error;
}


/*******************************************************************************
 * System entry point.
 */
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
