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

enum PinData
{
    //                            7    6    5    4    3    2    1    0
    //                            led1 led0           rst  in   out  clk
    //
    state_idle         = 0xc9, // 1    1    0    0    1    0    0    1
    state_leds         = 0x09, // 0    0    0    0    1    0    0    1
    state_reset_target = 0xc1, // 1    1    0    0    0    0    0    1
    state_reset_swd    = 0xcb, // 1    1    0    0    1    0    1    1

    direction_write    = 0xfb, // 1    1    1    1    1    0    1    1
    direction_read     = 0xf9, // 1    1    1    1    1    0    0    1
};

/******************************************************************************/
namespace CommandLine
{
    static Scalar<int>          debug ("debug",  true,  0,
				       "What level of debug logging to use.");

    static Argument     *arguments[] = { &debug, null };
}
/******************************************************************************/
Error mpsse_setup_buffers(ftdi_context & ftdi)
{
    uint	read;
    uint	write;

    CheckP(ftdi_usb_purge_buffers(&ftdi));

    CheckP(ftdi_read_data_set_chunksize(&ftdi, 65536));
    CheckP(ftdi_write_data_set_chunksize(&ftdi, 65536));

    CheckP(ftdi_read_data_get_chunksize(&ftdi, &read));
    CheckP(ftdi_write_data_get_chunksize(&ftdi, &write));

    debug(1, "Chunksize (r/w): %d/%d", read, write);

    return success;
}
/******************************************************************************/
Error mpsse_write(ftdi_context & ftdi, uint8 * buffer, int count)
{
    CheckEQ(ftdi_write_data(&ftdi, buffer, count), count);

    return success;
}
/******************************************************************************/
Error mpsse_read(ftdi_context & ftdi, uint8 * buffer, int count, int timeout)
{
    int	received = 0;

    /*
     * This is a crude timeout mechanism.  The time that we wait will never
     * be less than the requested number of milliseconds.  But it can certainly
     * be more.
     */
    for (int i = 0; i < timeout; ++i)
    {
	received += CheckP(ftdi_read_data(&ftdi,
					  buffer + received,
					  count - received));

	if (received >= count)
	{
	    debug(2, "Response took about %d milliseconds.", i);
	    return success;
	}

	/*
	 * The latency timer is set to 1ms, so we wait that long before trying
	 * again.
	 */
	usleep(1000);
    }

    return Err::timeout;
}
/******************************************************************************/
Error mpsse_synchronize(ftdi_context & ftdi)
{
    uint8	commands[] = {0xaa};
    uint8	response[2];

    Check(mpsse_write(ftdi, commands, sizeof(commands)));
    Check(mpsse_read (ftdi, response, sizeof(response), 1000));

    CheckEQ(response[0], 0xfa);
    CheckEQ(response[1], 0xaa);

    return success;
}
/******************************************************************************/
Error mpsse_setup(ftdi_context & ftdi)
{
    /*
     * 1MHz = 60Mhz / ((29 + 1) * 2)
     */
    uint8	commands[] =
    {
	DIS_DIV_5,
	DIS_ADAPTIVE,
	DIS_3_PHASE,
	EN_3_PHASE,
	TCK_DIVISOR,   29, 0,
	SET_BITS_LOW,  state_idle, direction_write,
	SET_BITS_HIGH, 0x00, 0x00
    };

    Check(mpsse_setup_buffers(ftdi));

    CheckP(ftdi_set_latency_timer(&ftdi, 1));

    CheckP(ftdi_set_bitmode(&ftdi, 0x00, BITMODE_RESET));
    CheckP(ftdi_set_bitmode(&ftdi, 0x00, BITMODE_MPSSE));

    Check(mpsse_synchronize(ftdi));

    CheckEQ(ftdi_write_data(&ftdi, commands, sizeof(commands)),
	    sizeof(commands));

    return success;
}
/******************************************************************************/
Error flash_leds(ftdi_context & ftdi)
{
    for (int i = 0; i < 2; ++i)
    {
	uint8	commands[] = {SET_BITS_LOW, 0x00, direction_write};

	commands[1] = (i & 1) ? state_leds : state_idle;

	CheckEQ(ftdi_write_data(&ftdi, commands, sizeof(commands)),
		sizeof(commands));

	usleep(200000);
    }

    return success;
}
/******************************************************************************/
#define SWD_HEADER_START	0x01
#define SWD_HEADER_AP		0x02
#define SWD_HEADER_DP		0x00
#define SWD_HEADER_READ		0x04
#define SWD_HEADER_WRITE	0x00
#define SWD_HEADER_A0		0x00
#define SWD_HEADER_A4		0x08
#define SWD_HEADER_A8		0x10
#define SWD_HEADER_AC		0x18
#define SWD_HEADER_PARITY	0x20
#define SWD_HEADER_PARK		0x80

uint8 swd_request(int address, bool debug_port, bool write)
{
    bool	parity  = debug_port ^ write;
    uint8	request = (SWD_HEADER_START |
			   (debug_port ? SWD_HEADER_DP : SWD_HEADER_AP) |
			   (write ? SWD_HEADER_WRITE : SWD_HEADER_READ) |
			   ((address & 0x03) << 3) |
			   SWD_HEADER_PARK);

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
	request |= SWD_HEADER_PARITY;

    return request;
}
/******************************************************************************/
bool swd_parity(uint32 data)
{
    uint32	step = data ^ (data >> 16);

    step = step ^ (step >> 8);
    step = step ^ (step >> 4);
    step = step ^ (step >> 2);
    step = step ^ (step >> 1);

    return (step & 1);
}
/******************************************************************************/
Error swd_reset(ftdi_context & ftdi)
{
    uint8	commands[] =
    {
	SET_BITS_LOW, state_reset_swd, direction_write,
	CLK_BYTES, 5, 0, CLK_BITS, 1,
	SET_BITS_LOW, state_idle, direction_write,
	CLK_BITS, 0
    };

    Check(mpsse_write(ftdi, commands, sizeof(commands)));

    return success;
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
Error enumerate_access_ports(ftdi_context &ftdi) {
  SWDInterface swd(&ftdi);
  Check(swd.initialize());

  DebugAccessPort dap(&swd);
  Check(dap.reset_state());

  uint32_t buffer[32];
  Target target(&dap, 0);
  Check(target.initialize());
  Check(target.read_words(0, buffer, 32));
  notice("First 32 words of target memory:");
  for (int i = 0; i < 32; ++i) {
    notice(" %08X: %08X", i * 4, buffer[i]);
  }

  uint32_t special_value = 0xDEADBEEF;
  Check(target.write_words(&special_value, 0x10000000, 1));
  special_value = 0;
  Check(target.read_words(0x10000000, &special_value, 1));
  notice("Wrote word %08X", special_value);

  for (uint32_t i = 0; i < 256; ++i) {
    AccessPort ap(&dap, i);
    
    Check(ap.post_read(0xFC));
    uint32_t idr;
    Check(ap.read_last_result(&idr));
    
    if (idr == 0) continue;  // AP not implemented

    debug(1, "AP %02X IDR = %08X", i, idr);

    if (idr & (1 << 16)) {  // Memory Access Port
      Check(crawl_memory_ap(ap));
    } else {
      debug(1, "Unknown AP type.");
    }
  }

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

    CheckCleanup(mpsse_setup(ftdi), mpsse_failed);
    CheckCleanup(flash_leds(ftdi), leds_failed);

    enumerate_access_ports(ftdi);

  leds_failed:
    CheckP(ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_RESET));

  mpsse_failed:
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
