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

#include <unistd.h>
#include <stdio.h>
#include <ftdi.h>

using namespace Err;
using namespace Log;
using std::vector;

/******************************************************************************/
namespace CommandLine
{
    static Scalar<int>          debug ("debug",  true,  0,
				       "What level of debug logging to use.");

    static Argument     *arguments[] = { &debug, null };
}
/******************************************************************************/
Error read_cpuid(AccessPort &ap, uint32_t *cpuid) {
  Check(ap.write(0x04, 0xE000ED00));  // CPUID register on ARMv6M
  return ap.read_blocking(0x0C, cpuid, 100);
}
/******************************************************************************/
Error crawl_unknown_peripheral(AccessPort &ap, uint32_t regfile);
/******************************************************************************/
Error crawl_rom_table(AccessPort &ap, uint32_t base_addr,
                                      uint32_t regfile,
                                      uint32_t size) {
  debug(1, "Found ROM Table.");

  CheckEQ(size, 4096);

  uint32_t memtype;
  Check(ap.write(0x04, regfile + 0xFCC));  // MEMTYPE address into TAR.
  Check(ap.read_blocking(0x0C, &memtype, 100));

  if (memtype & 1) {
    debug(1, "ROM Table is on a common bus with system memory.");
  } else {
    debug(1, "ROM Table is on a dedicated bus.");
  }

  vector<uint32_t> next_layer;

  Check(ap.write(0x04, base_addr));
  for (uint32_t i = 0; i < (0xF00 / 4); ++i) {
    uint32_t entry;
    Check(ap.read_blocking(0x0C, &entry, 100));

    if ((entry & (1 << 1)) == 0) {
      // 8-bit entry; construct it from the three consecutive words.
      entry <<= 24;
      for (int i = 0; i < 3; ++i) {
        uint32_t part;
        Check(ap.read_blocking(0x0C, &part, 100));
        entry = (entry >> 8) | (part << 24);
      }
    }

    if (entry == 0) {
      // End of ROM table!
      break;
    }

    if ((entry & (1 << 0)) == 0) {
      // Entry not present; skip it.
      debug(1, "[%d]: not present", i);
      continue;
    }

    int32_t offset = entry & ~0xFFF;
    uint32_t sub_regfile = base_addr + offset;
    next_layer.push_back(sub_regfile);
    debug(1, "[%d]: base + %08X = %08X", i, offset, sub_regfile);
  }

  for (vector<uint32_t>::iterator it = next_layer.begin();
       it != next_layer.end();
       ++it) {
    crawl_unknown_peripheral(ap, *it);
  }

  return success;
}
/******************************************************************************/
Error crawl_armv6m_dwt(AccessPort &ap, uint32_t base_addr, 
                                       uint32_t regfile,
                                       uint32_t size) {
  debug(1, "Found ARMv6M DWT");
  return success;
}
/******************************************************************************/
Error crawl_armv6m_bpu(AccessPort &ap, uint32_t base_addr, 
                                       uint32_t regfile,
                                       uint32_t size) {
  debug(1, "Found ARMv6M BPU");
  return success;
}
/******************************************************************************/
Error crawl_armv6m_scs(AccessPort &ap, uint32_t base_addr, 
                                       uint32_t regfile,
                                       uint32_t size) {
  debug(1, "Requesting halt...");
  ap.write(0x04, 0xE000EDF0);
  ap.write(0x0C, (0xA05F << 16)
               | (1 << 1)  // Request a halt
               | (1 << 0)  // Enable debugging
               );
  
  for (int i = 0; i < 10; ++i) {
    ap.write(0x04, 0xE000EDF0);
    uint32_t dhcsr;
    ap.read_blocking(0x0C, &dhcsr, 100);
    if (dhcsr & (1 << 17)) {
      debug(1, "Halted!");
      break;
    } else {
      debug(1, "Waiting for halt...");
      usleep(1000000);
    }
  }

  return success;
}
/******************************************************************************/
Error crawl_unknown_peripheral(AccessPort &ap, uint32_t regfile) {
  debug(1, "--- Peripheral at %08X in AP %02X", regfile, ap.index());

  // Load Component ID registers!
  uint32_t component_id[4];
  Check(ap.write(0x04, regfile + 0xFF0));  // First address into TAR
  Check(ap.read_blocking(0x0C, &component_id[0], 100));
  Check(ap.read_blocking(0x0C, &component_id[1], 100));
  Check(ap.read_blocking(0x0C, &component_id[2], 100));
  Check(ap.read_blocking(0x0C, &component_id[3], 100));

  for (int i = 0; i < 4; ++i) {
    debug(1, "Component ID %d = %08X", i, component_id[i]);
  }

  // ADIv5 places certain constraints on component IDs.  Sanity check them.
  CheckEQ(component_id[0], 0x0D);
  CheckEQ(component_id[2], 0x05);
  CheckEQ(component_id[3], 0xB1);

  // Load Peripheral ID4, which tells us how large this component truly is.
  Check(ap.write(0x04, regfile + 0xFD0));
  uint32_t peripheral_id4;
  Check(ap.read_blocking(0x0C, &peripheral_id4, 100));

  uint32_t log2_size_in_blocks = (peripheral_id4 >> 4) & 0xF;
  uint32_t size = (1 << log2_size_in_blocks) * 4 * 1024;
  debug(1, " Size = 2^%u blocks = %u bytes", log2_size_in_blocks, size);

  // regfile is the address of the *last* block -- compute the *first* block.
  uint32_t base_addr = (regfile + 4 * 1024) - size;

  // Now, dispatch on the type of this component.
  uint8_t component_class = (component_id[1] >> 4) & 0xF;
  switch (component_class) {
    case 1:
      return crawl_rom_table(ap, base_addr, regfile, size);
  }

  // Hm, Cortex-M0 seems to return bogus component classes.
  // Attempt to recognize processor heuristically.
  uint32_t cpuid;
  Check(read_cpuid(ap, &cpuid));
  debug(1, "CPUID = %08X", cpuid);
  if (((cpuid >> 16) & 0xF) == 0xC) {  // ARMv6M
    switch (regfile) {
      case 0xE000E000:
        return crawl_armv6m_scs(ap, base_addr, regfile, size);

      case 0xE0001000:
        return crawl_armv6m_dwt(ap, base_addr, regfile, size);

      case 0xE0002000:
        return crawl_armv6m_bpu(ap, base_addr, regfile, size);
    }
  }

  debug(1, "Unknown component class %X.", component_class);
  return success;
}
/******************************************************************************/
Error crawl_memory_ap(AccessPort &ap) {
  uint32_t base;
  Check(ap.read_blocking(0xF8, &base, 10));

  if ((base & 3) != 3) {
    // This is a "legacy" device not compliant with ARM ADIv5.
    debug(1, "Encountered non-ADIv5 legacy device");
    return success;
  }

  // The MEM-AP may be in an arbitrary state.  Fix that.
  Check(ap.write(0x00, (1 << 4)     // Auto increment.
                     | (2 << 0)));  // Transfer size 2^2 = 4 bytes.

  // Extract the Debug Register File address from the BASE register.
  // This points to the last 4KiB block of this debug device.
  uint32_t regfile = base & ~0xFFF;
  debug(1, "register file at %08X", regfile);

  return crawl_unknown_peripheral(ap, regfile);
}
/******************************************************************************/
static uint8_t const program[] = {
0xf0, 0x1f, 0x00, 0x10, 0x41, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x12, 0xde, 
0xff, 0xef, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 
0x59, 0x00, 0x00, 0x00, 0x08, 0x4c, 0x80, 0x25, 0x00, 0x26, 0x25, 0x60, 0x07, 0x4c, 0x25, 0x60, 0x00, 0xf0, 0x05, 0xf8, 0x26, 0x60, 0x00, 0xf0, 0x02, 0xf8, 0xf8, 0xe7, 0xfe, 0xe7, 
0x04, 0x48, 0x01, 0x38, 0xfd, 0xd1, 0x70, 0x47, 0x00, 0x00, 0x00, 0x80, 0x00, 0x50, 0xfc, 0x3f, 0x00, 0x50, 0x00, 0x00, 0x10, 0x00, 

};
/******************************************************************************/
Error invoke_iap(Target &target, uint32_t param_table, uint32_t result_table) {
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
Error unmap_boot_sector(Target &target) {
  return target.write_word(0x40048000, 2);
}
/******************************************************************************/
Error program_flash(Target &target, void const *program, size_t word_count) {
  // Only support single-sector writes for now.
  if (word_count > (256 / sizeof(uint32_t))) return argument_error;

  uint32_t iap_table = 0x10000000;
  uint32_t ram_buffer = 0x10000100;

  // Prepare a copy of the program in RAM.
  Check(target.write_words(program, ram_buffer, word_count));
 
  // Ensure that the boot Flash isn't visible (will mess us up).
  Check(unmap_boot_sector(target));

  // Unprotect the bottom sector of Flash.
  debug(1, "Unprotecting Flash...");
  Check(target.write_word(iap_table + 0, 50));
  Check(target.write_word(iap_table + 4, 0));
  Check(target.write_word(iap_table + 8, 0));
  Check(invoke_iap(target, iap_table, iap_table));
  uint32_t iap_result;
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Erase the bottom sector of Flash.
  debug(1, "Erasing...");
  Check(target.write_word(iap_table +  0, 52));
  Check(target.write_word(iap_table +  4, 0));
  Check(target.write_word(iap_table +  8, 0));
  Check(target.write_word(iap_table + 12, 12000));
  Check(invoke_iap(target, iap_table, iap_table));
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  // Unprotect it, again.
  debug(1, "Unprotecting Flash - again...");
  Check(target.write_word(iap_table + 0, 50));
  Check(target.write_word(iap_table + 4, 0));
  Check(target.write_word(iap_table + 8, 0));
  Check(invoke_iap(target, iap_table, iap_table));
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  for (uint32_t i = ram_buffer; i < ram_buffer + (word_count * 4); i += 4) {
    uint32_t x;
    Check(target.read_word(i, &x));
    debug(2, "[%08X] = %08X", i, x);
  }
   // Copy program into Flash.
  debug(1, "Copying program into Flash...");
  Check(target.write_word(iap_table +  0, 51));
  Check(target.write_word(iap_table +  4, 0));
  Check(target.write_word(iap_table +  8, ram_buffer));
  Check(target.write_word(iap_table + 12, 256));
  Check(target.write_word(iap_table + 16, 12000));
  Check(invoke_iap(target, iap_table, iap_table));
  Check(target.read_word(iap_table + 0, &iap_result));
  CheckEQ(iap_result, 0);

  return success;
}
/******************************************************************************/
Error dump_flash(Target &target) {
  uint32_t buffer[256 / sizeof(uint32_t)];
  Check(target.read_words(0, buffer, sizeof(buffer) / sizeof(buffer[0])));

  notice("Contents of Flash:");
  for (unsigned i = 0; i < (256 / sizeof(uint32_t)); ++i) {
    notice(" [%08X] %08X", i * 4, buffer[i]);
  }

  return success;
}
/******************************************************************************/
Error run_experiment(ftdi_context &ftdi) {
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

  Check(program_flash(target, program, sizeof(program) / sizeof(uint32_t)));
  Check(dump_flash(target));

  Check(swd.reset_target());

  return success;
}
/******************************************************************************/
Error error_main(int argc, char const ** argv)
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
