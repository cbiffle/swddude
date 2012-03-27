#ifndef SWD_DP_H
#define SWD_DP_H

#include "libs/error/error_stack.h"

#include "arm.h"

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
 *
 * However, only a single DebugAccessPort should be used per SWDDriver instance,
 * because DebugAccessPort caches state and assumes that it's the only one
 * mutating it.
 */
class DebugAccessPort
{
    SWDDriver & _swd;
  
    // Caches the current contents of the SELECT DP register.
    ARM::word_t _SELECT;

    // Selects the given AP, and the bank to expose the given address.
    Err::Error select_ap_bank(uint8_t ap, uint8_t address);

public:
    DebugAccessPort(SWDDriver & swd);

    /*
     * Debug Access Port registers defined by ADIv5.  These are used within the
     * DebugAccessPort implementation, and can also be used by other code with
     * a SWDDriver directly.
     */
    enum Register
    {
        kRegABORT = 0x00,  // Write-only
        kRegIDCODE = 0x00,  // Read-only

        kRegCTRLSTAT = 0x01,  // Only available when SELECT.CTRLSEL=0
        kRegWCR = 0x01,  // Only available when SELECT.CTRLSEL=1

        kRegSELECT = 0x02,  // Write-only
        kRegRESEND = 0x02,  // Read-only

        kRegRDBUFF = 0x03,  // Read-only     
    };


    /***************************************************************************
     * Utilities
     */

    /*
     * Resets the Debug Access Port to a known state, erasing leftover effects
     * of previous sessions:
     *  - Resets SELECT to reveal the CTRL/STAT register and select the first
     *    bank of the first AP.
     *  - Clears the sticky error bits in CTRL/STAT to recover from faults.
     *  - Switches on power to the debug systems (required before interacting
     *    with Access Ports).
     */
    Err::Error reset_state();


    /***************************************************************************
     * Direct DP register access.
     */

    /*
     * Reads the contents of the IDCODE register.  This register is
     * architecturally specified to never return WAIT, so only two return codes
     * are possible: Err::success on a successful read, and Err::failure if
     * something is wrong.
     */
    Err::Error read_idcode(ARM::word_t *);

    /*
     * Alters the contents of the ABORT register, used to clear sticky error
     * conditions that cause other reads/writes to FAULT.
     *
     * The ABORT register is architecturally specified to never return WAIT,
     * so only two return codes are possible: Err::success on a successful
     * write, and Err::failure if something is wrong.
     */
    Err::Error write_abort(ARM::word_t);

    /*
     * Reads the contents of the CTRL/STAT register, possibly altering the
     * SELECT register in the process.
     *
     * While CTRL/STAT is architecturally specified to never return WAIT, this
     * method may need to alter SELECT.CTRLSEL to expose CTRL/STAT.  SELECT
     * *can* return WAIT.  Thus, there are three possible return codes:
     *  - Err::success on a successful read.
     *  - Err::failure if something is wrong.
     *  - Err::try_again if the SELECT register is busy.
     *
     * When this method completes with Err::success, SELECT.CTRLSEL is clear,
     * and subsequent accesses to CTRL/STAT won't return Err::try_again until
     * WCR is accessed.
     */
    Err::Error read_ctrlstat(ARM::word_t *);

    /*
     * Alters the contents of the CTRL/STAT register, possibly altering the
     * SELECT register in the process.
     *
     * While CTRL/STAT is architecturally specified to never return WAIT, this
     * method may need to alter SELECT.CTRLSEL to expose CTRL/STAT.  SELECT
     * *can* return WAIT.  Thus, there are three possible return codes:
     *  - Err::success on a successful write.
     *  - Err::failure if something is wrong.
     *  - Err::try_again if the SELECT register is busy.
     *
     * When this method completes with Err::success, SELECT.CTRLSEL is clear,
     * and subsequent accesses to CTRL/STAT won't return Err::try_again until
     * WCR is accessed.
     */
    Err::Error write_ctrlstat(ARM::word_t);

    /*
     * Alters the contents of the SELECT register, which determines both which
     * Access Port bank is visible, and whether the CTRL/STAT or WCR register is
     * visible.
     * 
     * May return Err::try_again if an Access Port transaction is in progress.
     */
    Err::Error write_select(ARM::word_t);

    /*
     * Reads the contents of the RESEND register.  May return Err::try_again.
     */
    Err::Error read_resend(ARM::word_t *);

    /*
     * Reads the contents of the RDBUFF register.  RDBUFF contains the results
     * of the last successful Access Port read operation.  It is a read-once
     * register: reading it destroys its contents.  May return Err::try_again if
     * the Access Port operation is still in progress.
     */
    Err::Error read_rdbuff(ARM::word_t *);


    /***************************************************************************
     * AP register access.
     */

    /*
     * Starts a read of an AP register, possibly changing AP and bank to do so.
     * This method can be used together with step_read_ap (below) and
     * read_rdbuff (above) to chain together several reads from the same AP for
     * higher throughput.
     *
     * Reads of the AP are asynchronous.  Once the read has completed, its
     * result will be returned by the next call to step_read_ap_in_bank (below)
     * or read_rdbuff (above), whichever comes first.
     *
     * Until the read is complete, other AP accesses may stall (i.e. return a
     * SWD WAIT response, which becomes Err::try_again).  Most AP registers can
     * be read quickly without stalling; the main exception are the registers in
     * the MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status
     * bit for avoiding this.
     *
     * The register is selected by Access Port register address.  Access Port
     * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
     * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
     * the byte address of a 32-bit register.  (Because it's a 32-bit register,
     * the two least significant bits must always be zero.)
     *
     * Return values:
     *  Err::argument_error - least-significant two bits of address not zero.
     *  Err::success - read operation started.
     *  Err::try_again - Access Port transaction in progress, try again.
     *  Err::failure - bad register index or failure communicating with
     *      interface.
     */
    Err::Error start_read_ap(uint8_t ap_index, uint8_t address);

    /*
     * Starts a new read of an AP register, possibly changing banks to do so,
     * and returns the result of the previous read.  This method can be used
     * together with start_read_ap and read_rdbuff (above) to
     * chain together several reads from the same AP for higher throughput.
     *
     * Reads of the AP are asynchronous.  Once the read has completed, its
     * result will be returned by the next call to this method or read_rdbuff
     * (above), whichever comes first.
     *
     * Until the read is complete, other AP accesses may stall (i.e. return a
     * SWD WAIT response, which becomes Err::try_again).  Most AP registers can
     * be read quickly without stalling; the main exception are the registers in
     * the MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status
     * bit for avoiding this.
     *
     * The register is selected by Access Port register address.  Access Port
     * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
     * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
     * the byte address of a 32-bit register.  (Because it's a 32-bit register,
     * the two least significant bits must always be zero.)
     *
     * Return values:
     *  Err::argument_error - least-significant two bits of address not zero or
     *      data pointer is the null pointer.
     *  Err::success - new read operation started, last results returned.
     *  Err::try_again - Access Port transaction in progress, try again.
     *  Err::failure - bad register index or failure communicating with
     *      interface.
     */
    Err::Error step_read_ap(uint8_t ap_index, 
                            uint8_t address,
                            ARM::word_t * data);

    /*
     * Writes a new value into an AP register, possibly changing banks to do
     * so.
     *
     * Writes to the AP are asynchronous (albeit less so than reads).  When this
     * function returns, the write operation has been accepted by the Debug
     * Access Port on your behalf, but the write itself may not have completed.
     * Until it completes, other AP operations may stall  (i.e. return a SWD
     * WAIT response, which becomes Err::try_again).  Most AP registers can be
     * written quickly without stalling; the main exception are the registers in
     * the MEM-AP for accessing memory.  The MEM-AP provides a TrInProg status
     * bit for avoiding this.
     *
     * The register is selected by Access Port register address.  Access Port
     * register addresses are given in the ADIv5 and ARM as 8-bit hexadecimal
     * numbers, e.g. 0xF8.  The top four bits are the bank; the bottom four are
     * the byte address of a 32-bit register.  (Because it's a 32-bit register,
     * the two least significant bits must always be zero.)
     *
     * Return values:
     *  Err::argument_error - least-significant two bits of address not zero.
     *  Err::success - new write operation started.
     *  Err::try_again - Access Port transaction in progress, try again.
     *  Err::failure - bad register index or failure communicating with
     *      interface.
     */
    Err::Error write_ap(uint8_t ap_index, uint8_t address, ARM::word_t data);
};

#endif  // SWD_DP_H
