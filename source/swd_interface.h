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
   * Reads a 32-bit register from the Debug Port.  Note that some registers can
   * only be read in particular states of the SELECT.CTRLSEL bit, and some
   * registers can't be read at all.
   */
  Err::Error read_dp(DebugRegister, uint32_t *data);

  /*
   * Writes a 32-bit word into a register in the Debug Port.  Note that some
   * registers can only be written in particular states of the SELECT.CTRLSEL
   * bit, and some registers can't be written at all.
   */
  Err::Error write_dp(DebugRegister, uint32_t data);

  /*
   * Posts a read of one of the four AP registers within the current bank of the
   * current Access Port, determined by the contents of the Debug Port's SELECT
   * register.
   *
   * Returns the result of the previously posted read, if ready, into data.  To
   * get the result of this read, either call read_ap_in_bank again, or read the
   * Debug Port's RDBUFF register.
   */
  Err::Error read_ap_in_bank(int address, uint32_t *data);

  /*
   * Alters one of hte four AP registers within the current bank of the current
   * Access Port, determined by the contents of the Debug Port's SELECT
   * register.
   */
  Err::Error write_ap_in_bank(int address, uint32_t data);

  /*
   * The functions below are simply handy shorthand for accessing Debug Port
   * registers.  They're defined in terms of the functions above.
   *
   * Using these functions consistently can help avoid accidentally writing to
   * a read-only register, or vice versa.
   */
  Err::Error read_dp_idcode(uint32_t *);
  Err::Error write_dp_abort(uint32_t);
  Err::Error read_dp_ctrlstat_wcr(uint32_t *);
  Err::Error write_dp_ctrlstat_wcr(uint32_t);
  Err::Error write_dp_select(uint32_t);
  Err::Error read_dp_resend(uint32_t *);
  Err::Error read_dp_rdbuff(uint32_t *);

};

#endif  // SWD_INTERFACE_H
