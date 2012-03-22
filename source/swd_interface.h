#ifndef SWD_INTERFACE_H
#define SWD_INTERFACE_H

#include "libs/error/error_stack.h"

#include <stdint.h>

struct ftdi_context;

/*
 * Wraps an ftdi_context for use as a SWD interface.
 */
class SWDInterface {
  ftdi_context *_ftdi;

public:
  SWDInterface(ftdi_context *);

  /*
   * The functions below must be reimplemented for the specific type of SWD
   * interface.  They will probably become pure-virtual.
   */

  /*
   * Initializes the SWD link to the target.  Currently this involves clocking
   * out some bits on SWDCLK to reset the protocol state machine and inspecting
   * the IDCODE register.
   *
   * When this function returns successfully, SWD communications have been
   * established.  The target can be in any state -- in particular, the contents
   * of SELECT and CTRL/STAT are undefined, and it may be faulting with a
   * sticky error from a previous session.
   */
  Err::Error initialize();

  /*
   * Asserts the target's reset line for a fixed period.
   */
  Err::Error reset_target();

  /*
   * Clocks out a string of bits to attempt to unstick the protocol state
   * machine in the target.
   */
  Err::Error reset_swd();

  /*
   * Reads a 32-bit register from either the Debug Port (debug_port=true) or the
   * current bank of the currently selected Access Port (debug_port=false).
   * Note that some registers can only be read in particular states, and some
   * registers can't be read at all.
   */
  Err::Error read(int address, bool debug_port, uint32_t *data);

  /*
   * Writes a 32-bit word into a register in either the Debug Port
   * (debug_port=true) or the current bank of the currently selected Access
   * Port (debug_port=false).  Note that some registers can only be written in
   * particular states, and some registers can't be written at all.
   */
  Err::Error write(int address, bool debug_port, uint32_t data);
};


class DebugAccessPort {
  SWDInterface &_swd;

public:
  enum DebugRegister {
    /*
     * The register IDs used here are word addresses.  The ARM ADI docs use
     * byte addresses, so to find these addresses in the ADI docs, multiply by
     * four.
     */
    kDPABORT = 0x00,   // Write-only
    kDPIDCODE = 0x00,  // Read-only
    kDPCTRLSTAT = 0x01,  // Only available when SELECT.CTRLSEL=0
    kDPWCR = 0x01,       // Only available when SELECT.CTRLSEL=1
    kDPSELECT = 0x02,  // Write-only
    kDPRESEND = 0x02,  // Read-only
    kDPRDBUFF = 0x03,  // Read-only
  };

  DebugAccessPort(SWDInterface *swd);

  /*
   * Raw access to DP registers.
   */

  Err::Error read_idcode(uint32_t *);
  Err::Error write_abort(uint32_t);
  Err::Error read_ctrlstat_wcr(uint32_t *);
  Err::Error write_ctrlstat_wcr(uint32_t);
  Err::Error write_select(uint32_t);
  Err::Error read_resend(uint32_t *);
  Err::Error read_rdbuff(uint32_t *);

  /*
   * Slightly more pleasant access to DP registers.
   */

  /*
   * Resets the Debug Port to a known state, erasing any leftover effects of
   * previous sessions:
   *  - Resets SELECT to reveal the CTRL/STAT register and select the first bank
   *    of Access Port 0.
   *  - Clears the STKERR bit in CTRL/STAT to recover from faults.
   *  - Switches on power to the debug systems (required to interact with
   *    Access Ports).
   */
  Err::Error reset_state();

  /*
   * Selects a particular Access Port, and bank within that Access Port.
   * Clears the SELECT.CTRLSEL bit to 0, making the Wire Control Register in the
   * Debug Port inaccessible.
   */
  Err::Error select_ap_bank(uint8_t ap, uint8_t bank);

  /*
   * Posts a read of one of the four AP registers within the current bank of the
   * current Access Port, determined by the contents of the Debug Port's SELECT
   * register.
   *
   * To get the result of this read, either call read_rdbuff (before posting
   * another AP read) or call read_ap_in_bank_pipelined to post another read
   * while retrieving the result.
   */
  Err::Error post_read_ap_in_bank(int address);

  /*
   * Posts a read of one of the four AP registers within the current bank of the
   * current Access Port, determined by the contents of the Debug Port's SELECT
   * register.  Simultaneously returns the result of the previously posted AP
   * read.
   * 
   * To get the result of this read, either call read_rdbuff (before posting
   * another AP read) or call read_ap_in_bank_pipelined to post another read
   * while retrieving the result.
   */
  Err::Error read_ap_in_bank_pipelined(int address, uint32_t *lastData);

  /*
   * Alters one of the four AP registers within the current bank of the current
   * Access Port, determined by the contents of the Debug Port's SELECT
   * register.
   */
  Err::Error write_ap_in_bank(int address, uint32_t data);
};

class AccessPort {
  DebugAccessPort &_dap;
  uint8_t _ap;

public:
  AccessPort(DebugAccessPort *, uint8_t);

  uint8_t index() const;
  DebugAccessPort &dap();

  Err::Error post_read(uint8_t addr);
  Err::Error read_pipelined(uint8_t addr, uint32_t *lastData);
  Err::Error read_last_result(uint32_t *lastData);

  Err::Error read_blocking(uint8_t addr, uint32_t *lastData, uint32_t tries);

  Err::Error write(uint8_t addr, uint32_t data);
};

#endif  // SWD_INTERFACE_H
