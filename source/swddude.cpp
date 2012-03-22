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
#include "swd_interface.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"
#include "libs/command_line/command_line.h"

#include <vector>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>

using namespace Err;
using namespace Log;
using std::vector;
using std::ifstream;
using std::ios;

/******************************************************************************/
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
/******************************************************************************/
static Error invoke_iap(Target &target, uint32_t param_table, uint32_t result_table) {
  Check(target.write_register(Target::kR0, param_table));
  Check(target.write_register(Target::kR1, result_table));
  Check(target.write_register(Target::kRDebugReturn, 0x1FFF1FF0));

  // Tell the CPU to return into RAM, and catch it there with a breakpoint.
  Check(target.write_register(Target::kRLink, param_table | 1));
  Check(target.enable_breakpoint(0, param_table));

  Check(target.reset_halt_state());

  debug(2, "Invoking IAP function...");
  Check(target.resume());
  bool halted = false;
  uint32_t attempts = 0;
  do {
    Check(target.is_halted(&halted));
    usleep(1000);
  } while (++attempts < 100 && !halted);

  uint32_t reason;
  Check(target.read_halt_state(&reason));
  if (reason & Target::kHaltBkpt) {
    return success;
  }

  if (!reason) {
    warning("Target did not halt (or resume)");
    Check(target.halt());
    uint32_t pc;
    Check(target.read_register(Target::kR15, &pc));
    warning("Target forceably halted at %08X", pc);

    uint32_t icsr;
    Check(target.read_word(0xE000ED04, &icsr));
    warning("ICSR = %08X", icsr);
  } else {
    uint32_t pc;
    Check(target.read_register(Target::kR15, &pc));
    warning("Target halted for unexpected reason at %08X", pc);
  }
  return success;
}
/******************************************************************************/
static Error unmap_boot_sector(Target &target) {
  return target.write_word(0x40048000, 2);
}
/******************************************************************************/
static Error program_flash(Target &target, void const *program, size_t word_count) {
  // Only support single-sector writes for now.
  uint32_t iap_table = 0x10000000;
  uint32_t ram_buffer = 0x10000100;

  // Ensure that the boot Flash isn't visible (will mess us up).
  Check(unmap_boot_sector(target));

  // Erase affected sectors.  Assumes uniform 4KiB sectors for now.
  size_t const lastSector = (word_count * sizeof(uint32_t) + 4095) / 4096;
  // Unprotect affected sectors.
  debug(1, "Unprotecting Flash sectors 0-%lu...", lastSector);
  Check(target.write_word(iap_table + 0, 50));
  Check(target.write_word(iap_table + 4, 0));
  Check(target.write_word(iap_table + 8, lastSector));
  Check(invoke_iap(target, iap_table, iap_table));
  uint32_t iap_result;
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Erase affected sectors.
  debug(1, "Erasing...");
  Check(target.write_word(iap_table +  0, 52));
  Check(target.write_word(iap_table +  4, 0));
  Check(target.write_word(iap_table +  8, lastSector));
  Check(target.write_word(iap_table + 12, 12000));
  Check(invoke_iap(target, iap_table, iap_table));
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Copy program to RAM, then to Flash, in 256 byte chunks.
  for (unsigned block = 0;
       block < word_count;
       block += (256 / sizeof(uint32_t))) {
    size_t block_size = word_count - block;
    if (block_size > 256) block_size = 256;

    debug(1, "Copying %lu words starting with #%u", block_size, block);
    Check(target.write_words(&program[block], ram_buffer, block_size));
    // Unprotect the sector.
    unsigned sector = block / 4096;
    debug(1, "Unprotecting Flash sector %u", sector);
    Check(target.write_word(iap_table + 0, 50));
    Check(target.write_word(iap_table + 4, sector));
    Check(target.write_word(iap_table + 8, sector));
    Check(invoke_iap(target, iap_table, iap_table));
    Check(target.read_word(iap_table + 0, &iap_result));
    CheckEQ(iap_result, 0);

    // Copy block to Flash
    debug(1, "Writing Flash...");
    Check(target.write_word(iap_table +  0, 51));
    Check(target.write_word(iap_table +  4, block * sizeof(uint32_t)));
    Check(target.write_word(iap_table +  8, ram_buffer));
    Check(target.write_word(iap_table + 12, 256));
    Check(target.write_word(iap_table + 16, 12000));
    Check(invoke_iap(target, iap_table, iap_table));
    Check(target.read_word(iap_table + 0, &iap_result));
    CheckEQ(iap_result, 0);
  }

  return success;
}
/******************************************************************************/
static Error dump_flash(Target &target) {
  uint32_t buffer[256 / sizeof(uint32_t)];
  Check(target.read_words(0, buffer, sizeof(buffer) / sizeof(buffer[0])));

  notice("Contents of Flash:");
  for (unsigned i = 0; i < (256 / sizeof(uint32_t)); ++i) {
    notice(" [%08X] %08X", i * 4, buffer[i]);
  }

  return success;
}
/******************************************************************************/
static Error run_experiment(ftdi_context &ftdi) {
  SWDInterface swd(&ftdi);
  Check(swd.initialize());
  Check(swd.reset_target());

  DebugAccessPort dap(&swd);
  Check(dap.reset_state());

  Target target(&swd, &dap, 0);
  Check(target.initialize());
  Check(target.halt());

  uint32_t pc;
  Check(target.read_register(Target::kR15, &pc));
  debug(1, "Interrupted target at PC = %08X", pc);

  Check(target.enable_breakpoints());
  size_t breakpoint_count;
  Check(target.get_breakpoint_count(&breakpoint_count));
  notice("Target supports %u hardware breakpoints.", (uint32_t) breakpoint_count);

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
    Error check_error = success;
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

    Check(program_flash(target, program, input_length / sizeof(uint32_t)));
    Check(dump_flash(target));

    Check(swd.reset_target());

wrong_size:
    input.close();
    if (program) delete[] program;
  }

  return success;
}
/******************************************************************************/
static Error error_main(int argc, char const ** argv)
{
    Error		check_error = success;
    ftdi_context	ftdi;
    uint		chipid;

    CheckCleanupP(ftdi_init(&ftdi), init_failed);

    CheckCleanupStringP(ftdi_usb_open(&ftdi, 0x0403, 0x6014), open_failed,
			"Unable to open FTDI device: %s",
			ftdi_get_error_string(&ftdi));

    CheckCleanupP(ftdi_usb_reset(&ftdi), reset_failed);
    CheckCleanupP(ftdi_set_interface(&ftdi, INTERFACE_A), interface_failed);
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
    Error	check_error = success;

    CheckCleanup(CommandLine::parse(argc, argv, CommandLine::arguments),
                 failure);

    log().set_level(CommandLine::debug.get());

    CheckCleanup(error_main(argc, argv), failure);
    return 0;

  failure:
    error_stack_print();
    return 1;
}
/******************************************************************************/
