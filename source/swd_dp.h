#ifndef SWD_DP_H
#define SWD_DP_H

#include "libs/error/error_stack.h"

#include <stdint.h>

class SWDDriver;

/*
 * Wraps a SWDDriver; provides the ADIv5-standard SWD-DP operations.
 *
 * The DebugAccessPort neither takes nor assumes ownership of the provided
 * SWDDriver instance.  It is safe to continue using the SWDDriver instance
 * alongside the DebugAccessPort to deliberately combine their side effects.
 * Since the DebugAccessPort provides a strict subset of SWDDriver's
 * functionality (e.g. it can't reset the communications interface or the
 * system), this is important.
 */
class DebugAccessPort {
  SWDDriver &_swd;

public:
  DebugAccessPort(SWDDriver &swd);

  /*
   * Debug Access Port registers defined by ADIv5.  These are used within the
   * DebugAccessPort implementation, and can also be used by other code with
   * a SWDDriver directly.
   */
  enum Register {
    kRegABORT = 0x00,  // Write-only
    kRegIDCODE = 0x00,  // Read-only

    kRegCTRLSTAT = 0x01,  // Only available when SELECT.CTRLSEL=0
    kRegWCR = 0x01,  // Only available when SELECT.CTRLSEL=1
    
    kRegSELECT = 0x02,  // Write-only
    kRegRESEND = 0x02,  // Read-only
    
    kRegRDBUFF = 0x03,  // Read-only     
  };


  /*****************************************************************************
   * Utilities
   */

  /*
   * Resets the Debug Access Port to a known state, erasing leftover effects of
   * previous sessions:
   *  - Resets SELECT to reveal the CTRL/STAT register and select the first bank
   *    of the first AP.
   *  - Clears the sticky error bits in CTRL/STAT to recover from faults.
   *  - Switches on power to the debug systems (required before interacting with
   *    Access Ports).
   */
  Err::Error reset_state();


  /*****************************************************************************
   * Direct DP register access.
   */

  /*
   * Reads the contents of the IDCODE register.  This register is
   * architecturally specified to never return WAIT, so only two return codes
   * are possible: Err::success on a successful read, and Err::failure if
   * something is wrong.
   */
  Err::Error read_idcode(uint32_t *);

  /*
   * Alters the contents of the ABORT register, used to clear sticky error
   * conditions that cause other reads/writes to FAULT.
   *
   * The ABORT register is architecturally specified to never return WAIT,
   * so only two return codes are possible: Err::success on a successful write,
   * and Err::failure if something is wrong.
   */
  Err::Error write_abort(uint32_t);

  /*
   * Reads the contents of either the CTRL/STAT or WCR register, depending on
   * the value of SELECT.CTRLSEL.
   *
   * The CTRL/STAT register is architecturally specified to never return WAIT,
   * so when SELECT.CTRLSEL=0, only two return codes are possible: Err::success
   * on a successful read, and Err::failure if something is wrong.
   *
   * The WCR register may additionally return Err::try_again.
   */
  Err::Error read_ctrlstat_wcr(uint32_t *);

  /*
   * Alters the contents of either the CTRL/STAT or WCR register, depending on
   * the value of SELECT.CTRLSEL.
   *
   * The CTRL/STAT register is architecturally specified to never return WAIT,
   * so when SELECT.CTRLSEL=0, only two return codes are possible: Err::success
   * on a successful write, and Err::failure if something is wrong.
   *
   * The WCR register may additionally return Err::try_again.
   */
  Err::Error write_ctrlstat_wcr(uint32_t);

  /*
   * Alters the contents of the SELECT register, which determines both which
   * Access Port bank is visible, and whether the CTRL/STAT or WCR register is
   * visible.
   * 
   * May return Err::try_again if an Access Port transaction is in progress.
   */
  Err::Error write_select(uint32_t);

  /*
   * Reads the contents of the RESEND register.  May return Err::try_again.
   */
  Err::Error read_resend(uint32_t *);

  /*
   * Reads the contents of the RDBUFF register.  RDBUFF contains the results of
   * the last successful Access Port read operation.  It is a read-once
   * register: reading it destroys its contents.  May return Err::try_again if
   * the Access Port operation is still in progress.
   */
  Err::Error read_rdbuff(uint32_t *);


  /*****************************************************************************
   * AP register access.
   */

  /*
   * Selects a particular Access Port and bank.  As a side effect, this clears
   * the SELECT.CTRLSEL bit, making the Wire Control Register inaccessible and
   * revealing the CTRL/STAT register in its place.
   *
   * The bank is selected by Access Port register address.  Access Port register
   * addresses are given in the ADIv5 and ARM as 8-bit hexadecimal numbers, e.g.
   * 0xF8.  The top four bits of this address are the bank number that will be
   * written to SELECT.  Thus, only the four most-significant bits matter for
   * this function.
   *
   * Return values:
   *  Err::success - Access Port and bank changed.
   *  Err::try_again - Access Port transaction in progress, try again.
   *  Err::failure - communications with interface failed.
   */
  Err::Error select_ap_bank(uint8_t ap, uint8_t bank_address);

  /*
   * Starts a read of one of the four AP registers visible in the current bank.
   * (The AP and bank are determined by the most recent write to the SELECT
   * register, i.e. through select_ap_bank above.)  This function can be used
   * together with step_read_ap_in_bank (below) and read_rdbuff (above) to
   * chain together several reads from the same AP/bank for higher throughput.
   *
   * Reads of the AP are asynchronous.  Once the read has completed, its result
   * will be returned by the next call to step_read_ap_in_bank (below) or
   * read_rdbuff (above), whichever comes first.
   *
   * Until the read is complete, other AP accesses may stall (i.e. return a SWD
   * WAIT response, which becomes Err::try_again).  Most AP registers can be
   * read quickly without stalling; the main exception are the registers in the
   * MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status bit for
   * avoiding this.
   *
   * The register is selected by Access Port register address.  Access Port
   * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
   * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
   * the byte address of a 32-bit register.  (Because it's a 32-bit register,
   * the two least significant bits must always be zero.)
   *
   * Note that the top four bits of the address are ignored by this function!
   * The DebugAccessPort assumes that you know what you're doing.
   *
   * Return values:
   *  Err::argument_error - least-significant two bits of address not zero.
   *  Err::success - read operation started.
   *  Err::try_again - Access Port transaction in progress, try again.
   *  Err::failure - bad register index or communications with interface failed.
   */
  Err::Error start_read_ap_in_bank(uint8_t bank_address);

  /*
   * Starts a new read of one of the four AP registers visible in the current
   * bank, and returns the result of the previous read.  (The AP and bank are
   * determined by the most recent write to the SELECT register, i.e. through
   * select_ap_bank above.)  This function can be used
   * together with step_read_ap_in_bank (below) and read_rdbuff (above) to
   * chain together several reads from the same AP/bank for higher throughput.
   *
   * Reads of the AP are asynchronous.  Once the read
   * has completed, its result will be returned by the next call to this
   * function or read_rdbuff (above), whichever comes first.
   *
   * Until the read is complete, other AP accesses may stall (i.e. return a SWD
   * WAIT response, which becomes Err::try_again).  Most AP registers can be
   * read quickly without stalling; the main exception are the registers in the
   * MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status bit for
   * avoiding this.
   *
   * The register is selected by Access Port register address.  Access Port
   * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
   * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
   * the byte address of a 32-bit register.  (Because it's a 32-bit register,
   * the two least significant bits must always be zero.)
   *
   * Note that the top four bits of the address are ignored by this function!
   * The DebugAccessPort assumes that you know what you're doing.
   *
   * Return values:
   *  Err::argument_error - least-significant two bits of address not zero or
   *      data pointer is the null pointer.
   *  Err::success - new read operation started, last results returned.
   *  Err::try_again - Access Port transaction in progress, try again.
   *  Err::failure - bad register index or communications with interface failed.
   */
  Err::Error step_read_ap_in_bank(uint8_t bank_address, uint32_t *data);

  /*
   * Writes a new value into one of the four AP registers visible in the current
   * bank.  (The AP and bank are determined by the most recent write to the
   * SELECT register, i.e. through select_ap_bank above.)
   *
   * Writes to the AP are asynchronous (albeit less so than reads).  When this
   * function returns, the write operation has been accepted by the Debug Access
   * Port on your behalf, but the write itself may not have completed.  Until
   * it completes, other AP operations may stall  (i.e. return a SWD WAIT
   * response, which becomes Err::try_again).  Most AP registers can be
   * written quickly without stalling; the main exception are the registers in
   * the MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status bit
   * for avoiding this.
   *
   * The register is selected by Access Port register address.  Access Port
   * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
   * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
   * the byte address of a 32-bit register.  (Because it's a 32-bit register,
   * the two least significant bits must always be zero.)
   *
   * Note that the top four bits of the address are ignored by this function!
   * The DebugAccessPort assumes that you know what you're doing.
   *
   * Return values:
   *  Err::argument_error - least-significant two bits of address not zero.
   *  Err::success - new write operation started.
   *  Err::try_again - Access Port transaction in progress, try again.
   *  Err::failure - bad register index or communications with interface failed.
   */
  Err::Error write_ap_in_bank(uint8_t bank_address, uint32_t data);
};

#endif  // SWD_DP_H
