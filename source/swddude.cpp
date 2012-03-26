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

#define __STDC_FORMAT_MACROS

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>
#include <inttypes.h>

using namespace Err;
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

  Check(target.write_register(Register::R0, param_table));
  Check(target.write_register(Register::R1, result_table));
  Check(target.write_register(Register::SP, stack));
  Check(target.write_register(Register::PC, IAP::entry));

  // Tell the CPU to return into RAM, and catch it there with a breakpoint.
  Check(target.write_register(Register::LR, param_table | 1));
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
    Check(target.read_register(Register::PC, &pc));
    warning("Target forceably halted at %08X", pc);
    return failure;
  }

  return success;
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
  return target.write_word(SYSCON::SYSMEMREMAP,
                           SYSCON::SYSMEMREMAP_MAP_USER_FLASH);
}

static Error unprotect_flash(Target &target, uint32_t work_addr,
                                             uint32_t first_sector,
                                             uint32_t last_sector) {
  debug(1, "Unprotecting Flash sectors %"PRIu32"-%"PRIu32"...",
      first_sector, last_sector);

  uint32_t const cmd_addr = work_addr;
  uint32_t const resp_addr = cmd_addr;  // Reuse same space.
  uint32_t const stack_top = cmd_addr
                           + IAP::max_command_response_words * sizeof(word_t)
                           + IAP::min_stack_bytes;

  // Build command table
  Check(target.write_word(cmd_addr + 0, IAP::Command::unprotect_sectors));
  Check(target.write_word(cmd_addr + 4, first_sector));
  Check(target.write_word(cmd_addr + 8, last_sector));

  Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

  uint32_t iap_result;
  Check(target.read_word(resp_addr + 0, &iap_result));
  CheckEQ(iap_result, 0);
  return success;
}

static Error erase_flash(Target &target, uint32_t work_addr,
                                         uint32_t first_sector,
                                         uint32_t last_sector) {
  debug(1, "Erasing Flash sectors %"PRIu32"-%"PRIu32"...",
      first_sector, last_sector);

  uint32_t const cmd_addr = work_addr;
  uint32_t const resp_addr = cmd_addr;  // Reuse same space.
  uint32_t const stack_top = cmd_addr
                           + IAP::max_command_response_words * sizeof(word_t)
                           + IAP::min_stack_bytes;

  Check(target.write_word(cmd_addr +  0, IAP::Command::erase_sectors));
  Check(target.write_word(cmd_addr +  4, first_sector));
  Check(target.write_word(cmd_addr +  8, last_sector));
  Check(target.write_word(cmd_addr + 12, 12000));  // TODO hard-coded clock

  Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

  uint32_t iap_result;
  Check(target.read_word(resp_addr + 0, &iap_result));
  CheckEQ(iap_result, 0);
  return success;
}

static Error copy_ram_to_flash(Target &target, uint32_t work_addr,
                                               uint32_t src_addr,
                                               uint32_t dest_addr,
                                               size_t num_bytes) {
  uint32_t const cmd_addr = work_addr;
  uint32_t const resp_addr = cmd_addr;  // Reuse same space.
  uint32_t const stack_top = cmd_addr
                           + IAP::max_command_response_words * sizeof(word_t)
                           + IAP::min_stack_bytes;

  debug(1, "Writing Flash: %zu bytes at %"PRIx32, num_bytes, dest_addr);

  Check(target.write_word(cmd_addr +  0, IAP::Command::copy_ram_to_flash));
  Check(target.write_word(cmd_addr +  4, dest_addr));
  Check(target.write_word(cmd_addr +  8, src_addr));
  Check(target.write_word(cmd_addr + 12, num_bytes));
  Check(target.write_word(cmd_addr + 16, 12000));  // TODO hard-coded clock

  Check(invoke_iap(target, cmd_addr, resp_addr, stack_top));

  uint32_t iap_result;
  Check(target.read_word(resp_addr + 0, &iap_result));
  CheckEQ(iap_result, 0);
  return success;
}


/*
 * Rewrites the target's flash memory.
 */
static Error program_flash(Target &target, void const *program,
                                           size_t word_count) {
  uint32_t const bytes_per_block = 256;
  uint32_t const ram_buffer = 0x10000000;
  uint32_t const work_area = ram_buffer + bytes_per_block;

  // The LPC11xx and LPC13xx parts use a uniform 4KiB protection/erase sector.
  size_t const bytes_per_sector = 4096;
  size_t const last_sector =
      (word_count * sizeof(uint32_t)) / bytes_per_sector;  // Inclusive.

  // Ensure that the boot Flash isn't visible (will mess us up).
  Check(unmap_boot_sector(target));

  Check(unprotect_flash(target, work_area, 0, last_sector));
  Check(erase_flash(target, work_area, 0, last_sector));

  // Copy program to RAM, then to Flash, in 256 byte chunks.
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

    unsigned sector = block_offset * sizeof(uint32_t) / bytes_per_sector;
    Check(unprotect_flash(target, work_area, sector, sector));

    // Copy block to Flash
    Check(copy_ram_to_flash(target, work_area,
                                    ram_buffer,
                                    block_offset * sizeof(uint32_t),
                                    bytes_per_block));
  }

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

static void fix_lpc_checksum(uint8_t program[], size_t program_length) {
  const size_t kCheckedVectors = 7;

  if (program_length < kCheckedVectors * sizeof(uint32_t)) {
    warning("Program too short to write LPC checksum.");
    return;
  }

  uint32_t *program_words = (uint32_t *) program;

  uint32_t sum = 0;
  for (size_t i = 0; i < kCheckedVectors; ++i) {
    sum += program_words[i];
  }
  sum = 0 - sum;

  debug(1, "Repairing LPC checksum: %08X", sum);

  program_words[kCheckedVectors] = sum;
}

static Error flash_from_file(Target &target, char const *path) {
  Error check_error = success;
  uint8_t *program = 0;
  ifstream input;

  input.open(path);

  // Do the "get length of file" dance.
  input.seekg(0, ios::end);
  size_t input_length = input.tellg();
  input.seekg(0, ios::beg);

  CheckCleanupEQ(input_length, (input_length / 4) * 4, wrong_size);

  program = new uint8_t[input_length];

  input.read((char *) program, input_length);

  debug(1, "Read program of %zu bytes", input_length);

  if (CommandLine::fix_lpc_checksum.get()) {
    fix_lpc_checksum(program, input_length);
  }

  CheckCleanup(program_flash(target, program, input_length / sizeof(uint32_t)),
               comms_failure);
  CheckCleanup(dump_flash(target), comms_failure);

comms_failure:
wrong_size:
  input.close();
  if (program) delete[] program;
  return check_error;
}

static Error run_experiment(SWDDriver &swd) {
  Error check_error = success;

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

  if (breakpoint_count == 0) {
    warning("Can't continue!");
    return success;
  }

  // Flash if requested.
  if (CommandLine::flash.set()) {
    CheckCleanup(flash_from_file(target, CommandLine::flash.get()),
                 comms_failure);
  }

comms_failure:
  Check(swd.enter_reset());
  usleep(100000);
  Check(swd.leave_reset());

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

  {
    MPSSESWDDriver swd(&ftdi);
    CheckCleanup(run_experiment(swd), experiment_failed);
  }

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
