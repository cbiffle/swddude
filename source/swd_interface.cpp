#include "swd_interface.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"

#include <ftdi.h>

#include <unistd.h>

using namespace Err;
using namespace Log;

/*
 * Many of the MPSSE commands expect either an 8- or 16-bit count.  To get the
 * most out of those bits, it encodes a count N as N-1.  These macros produce
 * the individual bytes of the adjusted count.
 */
#define FTH(n) ((((n) - 1) >> 8) & 0xFF)  // High 8 bits
#define FTL(n) (((n) - 1) & 0xFF)         // Low 8 bits


namespace {

/*
 * Maps FT232H I/O pins to SWD signals and protocol states.  The mapping
 * is fixed for now.
 */
enum PinStates {
  //                           RST  SWDI  SWDO  SWDCLK
  kStateIdle        = 0x9,  //  1    0     0      1
  kStateResetTarget = 0x1,  //  0    0     0      1
  kStateResetSWD    = 0xB,  //  1    0     1      1
};

/*
 * Pin directions for read and write -- used with PinStates above.
 */
enum PinDirs {
  //                  RST  SWDI  SWDO  SWDCLK
  kDirWrite = 0xB,  // 1    0     1      1
  kDirRead  = 0x9,  // 1    0     0      1
};


uint8_t const kSWDHeaderStart = 1 << 0;

uint8_t const kSWDHeaderAP = 1 << 1;
uint8_t const kSWDHeaderDP = 0 << 1;

uint8_t const kSWDHeaderRead  = 1 << 2;
uint8_t const kSWDHeaderWrite = 0 << 2;

uint8_t const kSWDHeaderParity = 1 << 5;
uint8_t const kSWDHeaderPark = 1 << 7;

uint8_t swd_request(int address, bool debug_port, bool write) {
  bool parity = debug_port ^ write;

  uint8_t request = kSWDHeaderStart
                  | (debug_port ? kSWDHeaderDP : kSWDHeaderAP)
                  | (write ? kSWDHeaderWrite : kSWDHeaderRead)
                  | ((address & 0x3) << 3)
                  | kSWDHeaderPark;

  // Incorporate address into parity
  switch (address & 0x3) {
    case 0:
    case 3:
      // Even number of ones, no change required.
      break;

    case 1:
    case 2:
      parity ^= 1;
      break;
  }

  if (parity) request |= kSWDHeaderParity;

  return request;
}

bool swd_parity(uint32_t data) {
  uint32_t t = data;
  t ^= t >> 16;
  t ^= t >> 8;
  t ^= t >> 4;
  t ^= t >> 2;
  t ^= t >> 1;

  return t & 1;
}

Error mpsse_transaction(ftdi_context *ftdi,
                        uint8_t *command, size_t command_count,
                        uint8_t *response, size_t response_count,
                        int timeout) {
  size_t count = 0;

  CheckEQ(ftdi_write_data(ftdi, command, command_count), (int) command_count);

  for (int i = 0; i < timeout; ++i) {
    count += CheckP(ftdi_read_data(ftdi, response + count,
                                         response_count - count));

    if (count >= response_count) {
      debug(3, "Response took %d attempts.", i);
      return success;
    }

    usleep(1000);
  }

  return Err::timeout;
}


}  // un-named namespace for implementation factors


SWDInterface::SWDInterface(ftdi_context *ftdi) : _ftdi(ftdi) {}


Error SWDInterface::initialize() {
  Check(reset_swd());

  uint32_t idcode;
  Check(read(DebugAccessPort::kDPIDCODE, true, &idcode));

  uint32_t version = idcode >> 28;
  uint32_t partno = (idcode >> 12) & 0xFFFF;
  uint32_t designer = (idcode >> 1) & 0x7FF;

  debug(1, "Debug Port IDCODE = %08X", idcode);
  debug(1, "  Version:  %X", version);
  debug(1, "  Part:     %X", partno);
  debug(1, "  Designer: %X", designer);

  return success;
}


Error SWDInterface::reset_target() {
  uint8_t commands[] = { SET_BITS_LOW, 0 /* set below */, kDirWrite };

  commands[1] = kStateResetTarget;
  CheckEQ(ftdi_write_data(_ftdi, commands, sizeof(commands)),
          sizeof(commands));

  usleep(20000);

  commands[1] = kStateIdle;
  CheckEQ(ftdi_write_data(_ftdi, commands, sizeof(commands)),
          sizeof(commands));

  return success;
}


Error SWDInterface::reset_swd() {
  uint8_t commands[] = {
    SET_BITS_LOW, kStateResetSWD, kDirWrite,
    CLK_BYTES, FTL(6), FTH(6),
    CLK_BITS, FTL(2),
    SET_BITS_LOW, kStateIdle, kDirWrite,
    CLK_BITS, FTL(1),
  };

  CheckEQ(ftdi_write_data(_ftdi, commands, sizeof(commands)),
          sizeof(commands));

  return success;
}

Error SWDInterface::read(int addr, bool debug_port, uint32_t *data) {
  uint8_t commands[] = {
    // Send SWD request byte
    MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(8),
        swd_request(addr, debug_port, false),

    // Release the bus and clock out a turnaround bit.
    SET_BITS_LOW, kStateIdle, kDirRead,
    CLK_BITS, FTL(1),

    // Read in the response, data, and parity bitfields.
    MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(3),
    MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB, FTL(4), FTH(4),
    MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(2),

    // Take the bus back and clock out a turnaround bit.
    SET_BITS_LOW, kStateIdle, kDirWrite,
    CLK_BITS, FTL(1),
  };

  uint8_t response[6];
  // response[0]: the three-bit response, MSB-justified.
  // response[4:1]: the 32-bit response word.
  // response[5]: the parity bit in bit 6, turnaround (ignored) in bit 7.
  Check(mpsse_transaction(_ftdi, commands, sizeof(commands),
                                 response, sizeof(response),
                                 1000));

  uint8_t ack = response[0] >> 5;
  CheckEQ(ack, 0x1);  // Require an OK response from the target.

  // Check for parity error.
  uint32_t data_temp = response[1]
                     | response[2] << 8
                     | response[3] << 16
                     | response[4] << 24;
  uint8_t parity = (response[5] >> 6) & 1;
  CheckEQ(parity, swd_parity(data_temp));
  
  // All is well!
  if (data) *data = data_temp;
  return success;
}

Error SWDInterface::write(int addr, bool debug_port, uint32_t data) {
  uint8_t commands[] = {
    // Write request byte.
    MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(8),
        swd_request(addr, debug_port, true),
    // Release the bus and clock out a turnaround bit.
    SET_BITS_LOW, kStateIdle, kDirRead,
    CLK_BITS, FTL(1),

    // Read response.
    MPSSE_DO_READ | MPSSE_READ_NEG | MPSSE_LSB | MPSSE_BITMODE, FTL(3),

    // Take the bus back and clock out a turnaround bit.
    SET_BITS_LOW, kStateIdle, kDirWrite,
    CLK_BITS, FTL(1),

    // Send the data word.
    MPSSE_DO_WRITE | MPSSE_LSB, FTL(4), FTH(4),
    (data >>  0) & 0xFF,
    (data >>  8) & 0xFF,
    (data >> 16) & 0xFF,
    (data >> 24) & 0xFF,
    // Send the parity bit.
    MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE, FTL(1),
        swd_parity(data) ? 0xFF : 0x00,
  };

  uint8_t response[1];
  Check(mpsse_transaction(_ftdi, commands, sizeof(commands),
                                 response, sizeof(response),
                                 1000));
  
  uint8_t ack = response[0] >> 5;
  CheckEQ(ack, 0x1);  // Require OK response.

  return success;
}


/*
 * Implementation of DebugAccessPort.
 */

DebugAccessPort::DebugAccessPort(SWDInterface *swd) : _swd(*swd) {}

Error DebugAccessPort::read_idcode(uint32_t *data) {
  return _swd.read(kDPIDCODE, true, data);
}

Error DebugAccessPort::write_abort(uint32_t data) {
  return _swd.write(kDPABORT, true, data);
}

Error DebugAccessPort::read_ctrlstat_wcr(uint32_t *data) {
  return _swd.read(kDPCTRLSTAT, true, data);
}

Error DebugAccessPort::write_ctrlstat_wcr(uint32_t data) {
  return _swd.write(kDPCTRLSTAT, true, data);
}

Error DebugAccessPort::write_select(uint32_t data) {
  return _swd.write(kDPSELECT, true, data);
}

Error DebugAccessPort::read_resend(uint32_t *data) {
  return _swd.read(kDPRESEND, true, data);
}

Error DebugAccessPort::read_rdbuff(uint32_t *data) {
  return _swd.read(kDPRDBUFF, true, data);
}

Error DebugAccessPort::post_read_ap_in_bank(int addr) {
  return _swd.read(addr, false, 0);
}

Error DebugAccessPort::read_ap_in_bank_pipelined(int addr, uint32_t *last) {
  return _swd.read(addr, false, last);
}

Error DebugAccessPort::write_ap_in_bank(int addr, uint32_t data) {
  return _swd.write(addr, false, data);
}

Error DebugAccessPort::select_ap_bank(uint8_t ap, uint8_t bank) {
  return write_select((ap << 24) | ((bank & 0xF) << 4));
}

Error DebugAccessPort::reset_state() {
  Check(write_select(0));  // Reset SELECT.
  Check(write_abort(1 << 2));  // Clear STKERR.
  Check(write_ctrlstat_wcr((1 << 30)     // CSYSPWRUPREQ
                         | (1 << 28)));  // CDBGPWRUPREQ
  return success;
}


/*
 * Implementation of AccessPort
 */

AccessPort::AccessPort(DebugAccessPort *dap, uint8_t ap)
    : _dap(*dap), _ap(ap) {}

uint8_t AccessPort::index() const {
  return _ap;
}

Error AccessPort::post_read(uint8_t address) {
  Check(_dap.select_ap_bank(_ap, address >> 4));
  return _dap.post_read_ap_in_bank((address & 0xF) >> 2);
}

Error AccessPort::read_pipelined(uint8_t address, uint32_t *lastData) {
  Check(_dap.select_ap_bank(_ap, address >> 4));
  return _dap.read_ap_in_bank_pipelined((address & 0xF) >> 2, lastData);
}

Error AccessPort::read_last_result(uint32_t *lastData) {
  return _dap.read_rdbuff(lastData);
}

Error AccessPort::read_blocking(uint8_t address, uint32_t *data,
    uint32_t tries) {
  Check(post_read(address));
  return read_last_result(data);
}

Error AccessPort::write(uint8_t address, uint32_t data) {
  Check(_dap.select_ap_bank(_ap, address >> 4));
  return _dap.write_ap_in_bank((address & 0xF) >> 2, data);
}
