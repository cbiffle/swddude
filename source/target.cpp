#include "target.h"

#include "swd_dp.h"
#include "swd.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"

using namespace Err;
using namespace Log;

/*******************************************************************************
 * AP registers in the MEM-AP.
 */
namespace MEM_AP
{
    static uint32_t const CSW = 0x00;
    static uint32_t const TAR = 0x04;
    static uint32_t const DRW = 0x0C;
}

/*******************************************************************************
 * The System Control Block.
 */
namespace SCB
{
    static uint32_t const AIRCR = 0xE000ED0C;
    static uint32_t const AIRCR_VECTKEY = 0x05FA << 16;
    static uint32_t const AIRCR_VECTRESET = 1 << 0;

    static uint32_t const DFSR  = 0xE000ED30;
}

/*******************************************************************************
 * The Debug Control Block.
 */
namespace DCB
{
    static uint32_t const DHCSR = 0xE000EDF0;
    static uint32_t const DCRSR = 0xE000EDF4;
    static uint32_t const DCRDR = 0xE000EDF8;
    static uint32_t const DEMCR = 0xE000EDFC;
    static uint32_t const DHCSR_S_HALT = 1 << 17;
}

/*******************************************************************************
 * The BreakPoint Unit (ARMv6-M).  Compatible with ARMv7-M's more complex Flash
 * Patch and Breakpoint (FPB) unit.
 */
namespace BPU
{
  static uint32_t const BP_CTRL = 0xE0002000;
  static uint32_t const BP_COMP0 = 0xE0002008;
}

/*******************************************************************************
 * Target implementation
 */
Target::Target(SWDDriver &swd, DebugAccessPort &dap, uint8_t mem_ap_index)
    : _swd(swd), _dap(dap),
      _mem_ap_index(mem_ap_index),
      _current_ap_bank(-1) {}

Error Target::select_bank_for_address(uint8_t address) {
  uint8_t bank = address & 0xF0;
  if (_current_ap_bank != bank) {
    Check(_dap.select_ap_bank(_mem_ap_index, bank));
    _current_ap_bank = bank;
  }
  return success;
}

Error Target::write_ap(uint8_t address, uint32_t data) {
  Check(select_bank_for_address(address));

  return _dap.write_ap_in_bank(address, data);
}

Error Target::start_read_ap(uint8_t address) {
  Check(select_bank_for_address(address));

  return _dap.start_read_ap_in_bank(address);
}

Error Target::step_read_ap(uint8_t nextAddress, uint32_t *lastData) {
  Check(select_bank_for_address(nextAddress));
  return _dap.step_read_ap_in_bank(nextAddress, lastData);
}

Error Target::final_read_ap(uint32_t *data) {
  Check(_dap.read_rdbuff(data));
  return success;
}

Error Target::peek32(uint32_t address, uint32_t *data) {
  Check(write_ap(MEM_AP::TAR, address));
  CheckRetry(start_read_ap(MEM_AP::DRW), 100);
  CheckRetry(final_read_ap(data), 100);

  debug(3, "peek32(%08X) = %08X", address, *data);
  return success;
}

Error Target::poke32(uint32_t address, uint32_t data) {
  debug(3, "poke32(%08X, %08X)", address, data);
  Check(write_ap(MEM_AP::TAR, address));
  CheckRetry(write_ap(MEM_AP::DRW, data), 100);

  // Block waiting for write to complete.
  uint32_t csw;
  do {
    CheckRetry(start_read_ap(MEM_AP::CSW), 100);
    Check(final_read_ap(&csw));
  } while (csw & (1 << 7));

  return success;
}

Error Target::initialize() {
  // We only use one AP.  Go ahead and select it and configure CSW.
  Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
  uint32_t csw;
  Check(final_read_ap(&csw));
  csw = (csw & 0xFFFFF000) | 2;  // Modify value for 4-byte transactions.
  Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

  // Enable debugging.
  uint32_t dhcsr;
  Check(peek32(DCB::DHCSR, &dhcsr));
  if ((dhcsr & (1 << 0)) == 0) {
    Check(poke32(DCB::DHCSR, (dhcsr & 0xFFFF)
                           | (0xA05F << 16)
                           | (1 << 0)));
  }

  return success;
}

Error Target::read_words(uint32_t target_addr,
                         void *host_buffer,
                         size_t count) {
  uint32_t *host_buffer_as_words = static_cast<uint32_t *>(host_buffer);

  // Configure MEM-AP for auto-incrementing 32-bit transactions.
  Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
  uint32_t csw;
  Check(final_read_ap(&csw));
  csw = (csw & 0xFFFFF000)  // Reserved fields must be preserved
      | (1 << 4)  // Auto-increment.
      | 2;  // 4-byte transactions.
  Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

  // Load Transfer Address Register with first address.
  Check(write_ap(MEM_AP::TAR, target_addr));

  // Transfer using pipelined reads.
  CheckRetry(start_read_ap(MEM_AP::DRW), 100);
  for (size_t i = 0; i < count; ++i) {
    CheckRetry(step_read_ap(MEM_AP::DRW, &host_buffer_as_words[i]), 100);
  }

  return success;
}

Error Target::write_words(void const *host_buffer, uint32_t target_addr,
                          size_t count) {
  uint32_t const *host_buffer_as_words =
      static_cast<uint32_t const *>(host_buffer);

  // Configure MEM-AP for auto-incrementing 32-bit transactions.
  Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
  uint32_t csw;
  Check(final_read_ap(&csw));
  csw = (csw & 0xFFFFF000)  // Reserved fields must be preserved
      | (1 << 4)  // Auto-increment.
      | 2;  // 4-byte transactions.
  Check(write_ap(MEM_AP::CSW, csw));  // Write it back.
  
  // Load Transfer Address Register with first address.
  Check(write_ap(MEM_AP::TAR, target_addr));

  for (size_t i = 0; i < count; ++i) {
    Check(write_ap(MEM_AP::DRW, host_buffer_as_words[i]));
  }

  return success;
}

Error Target::read_register(RegisterNumber reg, uint32_t *out) {
  Check(poke32(DCB::DCRSR, (0 << 16) | (reg & 0x1F)));

  uint32_t dhcsr;
  do {
    Check(peek32(DCB::DHCSR, &dhcsr));
  } while ((dhcsr & (1 << 16)) == 0);

  return peek32(DCB::DCRDR, out);
}

Error Target::write_register(RegisterNumber reg, uint32_t data) {
  Check(poke32(DCB::DCRDR, data));
  Check(poke32(DCB::DCRSR, (1 << 16) | (reg & 0x1F)));
  
  uint32_t dhcsr;
  do {
    Check(peek32(DCB::DHCSR, &dhcsr));
  } while ((dhcsr & (1 << 16)) == 0);

  return success;
}

Error Target::reset_and_halt() {
  // Save old DEMCR just in case.
  uint32_t demcr;
  Check(peek32(DCB::DEMCR, &demcr));

  // Write DEMCR back to request Vector Catch.
  Check(poke32(DCB::DEMCR, demcr | (1 << 0)  // VC_CORERESET
                                 | (1 << 10)  // VC_HARDERR
                                 | (1 << 24)));  // TRCENA

  // Request a processor-local reset.
  Check(poke32(SCB::AIRCR, SCB::AIRCR_VECTKEY | SCB::AIRCR_VECTRESET));

  // Wait for the processor to halt.
  CheckRetry(poll_for_halt(1 << 3), 1000);

  // Restore DEMCR.
  Check(poke32(DCB::DEMCR, demcr));

  return success;
}

Error Target::halt() {
  return poke32(DCB::DHCSR, (0xA05F << 16) | (1 << 1) | (1 << 0));
}

Error Target::poll_for_halt(uint32_t const dfsr_mask) {
  uint32_t dhcsr;
  Check(peek32(DCB::DHCSR, &dhcsr));
  uint32_t dfsr;
  Check(peek32(SCB::DFSR, &dfsr));

  debug(3, "poll_for_halt: DHCSR=%08X DFSR=%08X", dhcsr, dfsr);

  if ((dhcsr & DCB::DHCSR_S_HALT) && (dfsr & dfsr_mask)) return success;
  return try_again;
}

Error Target::resume() {
  return poke32(DCB::DHCSR, (0xA05F << 16) | (0 << 1) | (1 << 0));
}

bool Target::is_register_implemented(int n) {
  return n != 19 && n <= kRLast;
}

Error Target::enable_breakpoints() {
  return poke32(BPU::BP_CTRL, (1 << 1) | (1 << 0));
}

Error Target::disable_breakpoints() {
  return poke32(BPU::BP_CTRL, (1 << 1) | (0 << 0));
}

Error Target::are_breakpoints_enabled(bool *result) {
  uint32_t ctrl;
  Check(peek32(BPU::BP_CTRL, &ctrl));
  *result = ctrl & (1 << 0);
  return success;
}

Error Target::get_breakpoint_count(size_t *n) {
  uint32_t ctrl;
  Check(peek32(BPU::BP_CTRL, &ctrl));
  *n = (ctrl >> 4) & 0xF;
  return success;
}

Error Target::enable_breakpoint(size_t n, uint32_t addr) {
  // Note: we ignore bit 0 of the address to permit Thumb-style addresses.

  // Address must point into code region (bottom 512MiB)
  if (addr & 0xE0000000) return argument_error;

  // Break on upper or lower halfword, depending on bit 1 of address.
  uint8_t match_type = (addr & 2) ? 2 : 1;

  return poke32(BPU::BP_COMP0 + (n * sizeof(uint32_t)),
                (match_type << 30) | (addr & 0x1FFFFFFC) | 1);
}

Error Target::disable_breakpoint(size_t n) {
  return poke32(BPU::BP_COMP0 + (n * sizeof(uint32_t)), 0);
}

Error Target::is_halted(bool *flag) {
  uint32_t dhcsr;
  Check(peek32(DCB::DHCSR, &dhcsr));
  *flag = dhcsr & (1 << 17);
  return success;
}

Error Target::read_halt_state(uint32_t *out) {
  uint32_t dfsr;
  Check(peek32(SCB::DFSR, &dfsr));
  *out = dfsr & 0x1F;
  return success;
}

Error Target::reset_halt_state() {
  return poke32(SCB::DFSR, 0x1F);
}

Error Target::read_word(uint32_t addr, uint32_t *data) {
  return peek32(addr, data);
}

Error Target::write_word(uint32_t addr, uint32_t data) {
  return poke32(addr, data);
}
