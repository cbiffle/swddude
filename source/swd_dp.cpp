#include "swd_dp.h"

#include "swd.h"

using namespace Err;

DebugAccessPort::DebugAccessPort(SWDDriver &swd) : _swd(swd) {}

Error DebugAccessPort::read_idcode(uint32_t *data) {
  return _swd.read(kRegIDCODE, true, data);
}

Error DebugAccessPort::write_abort(uint32_t data) {
  return _swd.write(kRegABORT, true, data);
}

Error DebugAccessPort::read_ctrlstat_wcr(uint32_t *data) {
  return _swd.read(kRegCTRLSTAT, true, data);
}

Error DebugAccessPort::write_ctrlstat_wcr(uint32_t data) {
  return _swd.write(kRegCTRLSTAT, true, data);
}

Error DebugAccessPort::write_select(uint32_t data) {
  return _swd.write(kRegSELECT, true, data);
}

Error DebugAccessPort::read_resend(uint32_t *data) {
  return _swd.read(kRegRESEND, true, data);
}

Error DebugAccessPort::read_rdbuff(uint32_t *data) {
  return _swd.read(kRegRDBUFF, true, data);
}

Error DebugAccessPort::select_ap_bank(uint8_t ap, uint8_t bank_address) {
  return write_select((ap << 24) | (bank_address >> 4));
}

Error DebugAccessPort::start_read_ap_in_bank(uint8_t bank_address) {
  if (bank_address & 3) return argument_error;

  return _swd.read((bank_address >> 2) & 3, false, 0);
}

Error DebugAccessPort::step_read_ap_in_bank(uint8_t bank_address,
    uint32_t *last) {
  if (bank_address & 3) return argument_error;

  return _swd.read((bank_address >> 2) & 3, false, last);
}

Error DebugAccessPort::write_ap_in_bank(uint8_t bank_address, uint32_t data) {
  if (bank_address & 3) return argument_error;

  return _swd.write((bank_address >> 2) & 3, false, data);
}

Error DebugAccessPort::reset_state() {
  Check(write_select(0));  // Reset SELECT.
  Check(write_abort((1 << 1)  // Clear STKCMP
                  | (1 << 2)  // Clear STKERR
                  | (1 << 3)  // Clear WDERR
                  | (1 << 4)  // Clear ORUNERR
                  ));
  Check(write_ctrlstat_wcr((1 << 30)     // CSYSPWRUPREQ
                         | (1 << 28)));  // CDBGPWRUPREQ
  return success;
}
