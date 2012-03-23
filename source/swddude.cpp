/*
 * Copyright (c) 2012, Cliff L. Biffle.
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

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"
#include "libs/command_line/command_line.h"

#include <vector>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>
#include <inttypes.h>

using namespace Err;
using namespace Log;
using std::vector;
using std::ifstream;
using std::ios;

/*******************************************************************************
 * Command-line definitions
 */

namespace CommandLine
{
    static Scalar<int>          debug ("debug",  true,  0,
				       "What level of debug logging to use.");

    static Scalar<String> flash("flash", true, "", "Binary program to load");

    static Scalar<bool> fix_lpc_checksum("fix_lpc_checksum", true, false,
        "When true, the loader will write the LPC-style checksum.");

    static Argument     *arguments[] = {
      &debug,
      &flash,
      &fix_lpc_checksum,
      null };
}


/*******************************************************************************
 * Flash programming implementation
 */

/*
 * Invokes a routine within In-Application Programming ROM of an LPC part.
 */
static Error invoke_iap(Target &target, uint32_t param_table,
                                        uint32_t result_table,
                                        uint32_t stack) {
  debug(2, "invoke_iap: param_table=%08X, result_table=%08X, stack=%08X",
      param_table, result_table, stack);

  Check(target.write_register(Target::kR0, param_table));
  Check(target.write_register(Target::kR1, result_table));
  Check(target.write_register(Target::kRStack, stack));
  Check(target.write_register(Target::kRDebugReturn, 0x1FFF1FF0));

  // Tell the CPU to return into RAM, and catch it there with a breakpoint.
  Check(target.write_register(Target::kRLink, param_table | 1));
  Check(target.enable_breakpoint(0, param_table));

  Check(target.reset_halt_state());

  Check(target.resume());
  bool halted = false;
  uint32_t attempts = 0;
  do {
    Check(target.is_halted(&halted));
    usleep(10000);
  } while (++attempts < 100 && !halted);

  if (!halted) {
    warning("Target did not halt after IAP execution!");
    Check(target.halt());
    uint32_t pc;
    Check(target.read_register(Target::kR15, &pc));
    warning("Target forceably halted at %08X", pc);
  }

  uint32_t reason;
  Check(target.read_halt_state(&reason));
  if (reason & Target::kHaltBkpt) {
    return success;
  }

  uint32_t icsr;
  Check(target.read_word(0xE000ED04, &icsr));
  warning("ICSR = %08X", icsr);

  uint32_t cfsr;
  Check(target.read_word(0xE000ED28, &cfsr));
  warning("CFSR = %08X", cfsr);

  uint32_t bfar;
  Check(target.read_word(0xE000ED38, &bfar));
  warning("BFAR = %08X", bfar);

  for (int i = 0; i < 16; ++i) {
    uint32_t r;
    Check(target.read_register(Target::RegisterNumber(i), &r));
    warning("r%d = %08X", i, r);
  }
  return failure;
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
static Error unmap_boot_sector(Target &target) {
  return target.write_word(0x40048000, 2);
}

/*
 * Rewrites the target's flash memory.
 */
static Error program_flash(Target &target, void const *program,
                                           size_t word_count) {
  uint32_t iap_table = 0x10000000;  // Used for param and response: 20B
  uint32_t ram_buffer = 0x10000020;  // 256B
  uint32_t stack = 0x10000220;

  // Ensure that the boot Flash isn't visible (will mess us up).
  Check(unmap_boot_sector(target));

  // Erase affected sectors.  Assumes uniform 4KiB sectors for now.
  size_t const lastSector = (word_count * sizeof(uint32_t)) / 4096;
  // Unprotect affected sectors.
  debug(1, "Unprotecting Flash sectors 0-%lu...", lastSector);
  Check(target.write_word(iap_table + 0, 50));
  Check(target.write_word(iap_table + 4, 0));
  Check(target.write_word(iap_table + 8, lastSector));
  Check(invoke_iap(target, iap_table, iap_table, stack));
  uint32_t iap_result;
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Erase affected sectors.
  debug(1, "Erasing...");
  Check(target.write_word(iap_table +  0, 52));
  Check(target.write_word(iap_table +  4, 0));
  Check(target.write_word(iap_table +  8, lastSector));
  Check(target.write_word(iap_table + 12, 12000));
  Check(invoke_iap(target, iap_table, iap_table, stack));
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Copy program to RAM, then to Flash, in 256 byte chunks.
  uint32_t const bytes_per_block = 256;
  uint32_t const words_per_block = bytes_per_block / sizeof(uint32_t);
  uint32_t const block_count =
      (word_count + words_per_block - 1) / words_per_block;
  uint32_t const *program_words = (uint32_t const *) program;

  for (uint32_t block = 0; block < block_count; ++block) {
    uint32_t const block_offset = block * words_per_block;

    uint32_t block_size = word_count - block_offset;
    if (block_size > words_per_block) block_size = words_per_block;

    debug(1, "Copying %"PRIu32" words starting with #%"PRIu32" to %08X",
        block_size, block, ram_buffer);
    Check(target.write_words(&program_words[block_offset],
                             ram_buffer,
                             block_size));

    // Unprotect the sector.
    unsigned sector = block_offset * sizeof(uint32_t) / 4096;
    debug(1, "Unprotecting Flash sector %u", sector);
    Check(target.write_word(iap_table + 0, 50));
    Check(target.write_word(iap_table + 4, sector));
    Check(target.write_word(iap_table + 8, sector));
    Check(invoke_iap(target, iap_table, iap_table, stack));
    Check(target.read_word(iap_table + 0, &iap_result));
    CheckEQ(iap_result, 0);

    // Copy block to Flash
    debug(1, "Writing Flash...");
    Check(target.write_word(iap_table +  0, 51));
    Check(target.write_word(iap_table +  4, block_offset * sizeof(uint32_t)));
    Check(target.write_word(iap_table +  8, ram_buffer));
    Check(target.write_word(iap_table + 12, 256));
    Check(target.write_word(iap_table + 16, 12000));
    Check(invoke_iap(target, iap_table, iap_table, stack));
    Check(target.read_word(iap_table + 0, &iap_result));
    CheckEQ(iap_result, 0);
  }

  return success;
}

/*
 * Enables ARMv7-M faults, so that missteps don't become Hard Fault.  This
 * is tolerated by the ARMv6-M Cortex-M0, but technically illegal.
 */
static Error enable_faults(Target &target) {
  Check(target.write_word(0xE000ED24, (1 << 18)  // Usage fault
                                    | (1 << 17)  // Bus fault
                                    | (1 << 16)  // Mem Manage fault
                                    ));
  return success;
}

/*
 * Dumps the first 256 bytes of the target's flash to the console.
 */
static Error dump_flash(Target &target) {
  uint32_t buffer[256 / sizeof(uint32_t)];
  Check(target.read_words(0, buffer, sizeof(buffer) / sizeof(buffer[0])));

  notice("Contents of Flash:");
  for (unsigned i = 0; i < (256 / sizeof(uint32_t)); ++i) {
    notice(" [%08X] %08X", i * 4, buffer[i]);
  }

  return success;
}


/*******************************************************************************
 * swddude main implementation
 */

static Error run_experiment(ftdi_context &ftdi) {
  Error check_error = success;

  MPSSESWDDriver swd(&ftdi);
  Check(swd.initialize());
  Check(swd.reset_target(100000));

  DebugAccessPort dap(swd);
  Check(dap.reset_state());

  Target target(&swd, &dap, 0);
  Check(target.initialize());
  Check(target.halt());

  Check(enable_faults(target));

  uint32_t pc;
  Check(target.read_register(Target::kR15, &pc));
  debug(1, "Interrupted target at PC = %08X", pc);

  Check(target.enable_breakpoints());
  size_t breakpoint_count;
  Check(target.get_breakpoint_count(&breakpoint_count));
  notice("Target supports %zu hardware breakpoints.", breakpoint_count);

  if (breakpoint_count == 0) {
    warning("Can't continue!");
    return success;
  }

  if (CommandLine::flash.set()) {
    ifstream input;
    input.open(CommandLine::flash.get());

    input.seekg(0, ios::end);
    size_t input_length = input.tellg();
    input.seekg(0, ios::beg);

    uint8_t *program = 0;
    CheckCleanupEQ(input_length, (input_length / 4) * 4, wrong_size);

    program = new uint8_t[input_length];

    input.read((char *) program, input_length);

    debug(1, "Read program of %u bytes", (unsigned int) input_length);

    if (CommandLine::fix_lpc_checksum.get()) {

      const size_t kCheckedVectors = 7;
      uint32_t sum = 0;
      uint32_t *program_words = (uint32_t *) program;
      for (size_t i = 0; i < kCheckedVectors; ++i) {
        sum += program_words[i];
      }
      sum = 0 - sum;
      debug(1, "Repairing LPC checksum: %08X", sum);
      program_words[kCheckedVectors] = sum;
    }

    CheckCleanup(program_flash(target, program,
                               input_length / sizeof(uint32_t)),
                 report_failure_reason);
    CheckCleanup(dump_flash(target),
                 report_failure_reason);

report_failure_reason:
    uint32_t ctrlstat;
    Check(dap.read_ctrlstat_wcr(&ctrlstat));
    notice("Final CTRL/STAT = %08X", ctrlstat);

    Check(swd.reset_target(100000));

wrong_size:
    input.close();
    if (program) delete[] program;
  }

  return check_error;
}


/*******************************************************************************
 * Entry point (sort of -- see main below)
 */

static uint16_t kFt232hVid = 0x0403;
static uint16_t kFt232hPid = 0x6014;

static Error error_main(int argc, char const *argv[]) {
  Error check_error = success;

  ftdi_context ftdi;
  CheckCleanupP(ftdi_init(&ftdi), init_failed);

  CheckCleanupStringP(ftdi_usb_open(&ftdi, kFt232hVid, kFt232hPid),
                      open_failed,
                      "Unable to open FTDI device: %s",
                      ftdi_get_error_string(&ftdi));

  CheckCleanupP(ftdi_usb_reset(&ftdi), reset_failed);
  CheckCleanupP(ftdi_set_interface(&ftdi, INTERFACE_A), interface_failed);

  unsigned chipid;
  CheckCleanupP(ftdi_read_chipid(&ftdi, &chipid), read_failed);

  debug(3, "FTDI chipid: %X", chipid);

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


/*******************************************************************************
 * System entry point.
 */
int main(int argc, char const *argv[]) {
  Error check_error = success;

  CheckCleanup(CommandLine::parse(argc, argv, CommandLine::arguments),
               failure);

  log().set_level(CommandLine::debug.get());

  CheckCleanup(error_main(argc, argv), failure);
  return 0;

failure:
  error_stack_print();
  return 1;
}
