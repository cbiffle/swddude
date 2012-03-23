#include "target.h"

#include "swd_dp.h"
#include "swd.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"

using namespace Err;
using namespace Log;

enum MEMAPRegister {
  kMEMAP_CSW = 0x00,
  kMEMAP_TAR = 0x04,
  kMEMAP_DRW = 0x0C,
};

enum SCSRegister {
  kSCS_DFSR  = 0xE000ED30,
  kSCS_DHCSR = 0xE000EDF0,
  kSCS_DCRSR = 0xE000EDF4,
  kSCS_DCRDR = 0xE000EDF8,
  kSCS_DEMCR = 0xE000EDFC,
};

enum BPURegister {
  kBPU_CTRL = 0xE0002000,
  kBPU_COMP_base = 0xE0002008,
};

Target::Target(SWDDriver *swd, DebugAccessPort *dap, uint8_t mem_ap_index)
    : _swd(*swd), _dap(*dap),
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
  return _dap.read_rdbuff(data);
}

Error Target::peek32(uint32_t address, uint32_t *data) {
  Check(write_ap(kMEMAP_TAR, address));
  Check(start_read_ap(kMEMAP_DRW));
  return final_read_ap(data);
}

Error Target::poke32(uint32_t address, uint32_t data) {
  Check(write_ap(kMEMAP_TAR, address));
  Check(write_ap(kMEMAP_DRW, data));

  uint32_t csw;
  do {
    Check(start_read_ap(kMEMAP_CSW));
    Check(final_read_ap(&csw));
  } while (csw & (1 << 7));

  return success;
}

Error Target::initialize() {
  // We only use one AP.  Go ahead and select it and configure CSW.
  Check(write_ap(kMEMAP_CSW, 2));  // Configure for 4-byte transactions.

  // Enable debugging.
  uint32_t dhcsr;
  Check(peek32(kSCS_DHCSR, &dhcsr));
  if ((dhcsr & (1 << 0)) == 0) {
    Check(poke32(kSCS_DHCSR, (dhcsr & 0xFFFF)
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
  Check(write_ap(kMEMAP_CSW, (1 << 4) | 2));
  
  // Load Transfer Address Register with first address.
  Check(write_ap(kMEMAP_TAR, target_addr));

  // Transfer using pipelined reads.
  Check(start_read_ap(kMEMAP_DRW));
  for (size_t i = 0; i < count; ++i) {
    Check(step_read_ap(kMEMAP_DRW, &host_buffer_as_words[i]));
  }

  return success;
}

Error Target::write_words(void const *host_buffer, uint32_t target_addr,
                          size_t count) {
  uint32_t const *host_buffer_as_words =
      static_cast<uint32_t const *>(host_buffer);

  // Configure MEM-AP for auto-incrementing 32-bit transactions.
  Check(write_ap(kMEMAP_CSW, (1 << 4) | 2));
  
  // Load Transfer Address Register with first address.
  Check(write_ap(kMEMAP_TAR, target_addr));

  for (size_t i = 0; i < count; ++i) {
    Check(write_ap(kMEMAP_DRW, host_buffer_as_words[i]));
  }

  return success;
}

Error Target::read_register(RegisterNumber reg, uint32_t *out) {
  Check(poke32(kSCS_DCRSR, (0 << 16) | (reg & 0x1F)));

  uint32_t dhcsr;
  do {
    Check(peek32(kSCS_DHCSR, &dhcsr));
  } while ((dhcsr & (1 << 16)) == 0);

  return peek32(kSCS_DCRDR, out);
}

Error Target::write_register(RegisterNumber reg, uint32_t data) {
  Check(poke32(kSCS_DCRDR, data));
  Check(poke32(kSCS_DCRSR, (1 << 16) | (reg & 0x1F)));
  
  uint32_t dhcsr;
  do {
    Check(peek32(kSCS_DHCSR, &dhcsr));
  } while ((dhcsr & (1 << 16)) == 0);

  return success;
}

Error Target::halt() {
  return poke32(kSCS_DHCSR, (0xA05F << 16) | (1 << 1) | (1 << 0));
}

Error Target::resume() {
  return poke32(kSCS_DHCSR, (0xA05F << 16) | (0 << 1) | (1 << 0));
}

bool Target::is_register_implemented(int n) {
  return n != 19 && n <= kRLast;
}

Error Target::enable_breakpoints() {
  return poke32(kBPU_CTRL, (1 << 1) | (1 << 0));
}

Error Target::disable_breakpoints() {
  return poke32(kBPU_CTRL, (1 << 1) | (0 << 0));
}

Error Target::are_breakpoints_enabled(bool *result) {
  uint32_t ctrl;
  Check(peek32(kBPU_CTRL, &ctrl));
  *result = ctrl & (1 << 0);
  return success;
}

Error Target::get_breakpoint_count(size_t *n) {
  uint32_t ctrl;
  Check(peek32(kBPU_CTRL, &ctrl));
  *n = (ctrl >> 4) & 0xF;
  return success;
}

Error Target::enable_breakpoint(size_t n, uint32_t addr) {
  // Note: we ignore bit 0 of the address to permit Thumb-style addresses.

  // Address must point into code region (bottom 512MiB)
  if (addr & 0xE0000000) return argument_error;

  // Break on upper or lower halfword, depending on bit 1 of address.
  uint8_t match_type = (addr & 2) ? 2 : 1;

  return poke32(kBPU_COMP_base + (n * sizeof(uint32_t)),
                (match_type << 30) | (addr & 0x1FFFFFFC) | 1);
}

Error Target::disable_breakpoint(size_t n) {
  return poke32(kBPU_COMP_base + (n * sizeof(uint32_t)), 0);
}

Error Target::is_halted(bool *flag) {
  uint32_t dhcsr;
  Check(peek32(kSCS_DHCSR, &dhcsr));
  *flag = dhcsr & (1 << 17);
  return success;
}

Error Target::read_halt_state(uint32_t *out) {
  uint32_t dfsr;
  Check(peek32(kSCS_DFSR, &dfsr));
  *out = dfsr & 0x1F;
  return success;
}

Error Target::reset_halt_state() {
  return poke32(kSCS_DFSR, 0x1F);
}

Error Target::read_word(uint32_t addr, uint32_t *data) {
  return peek32(addr, data);
}

Error Target::write_word(uint32_t addr, uint32_t data) {
  return poke32(addr, data);
}
