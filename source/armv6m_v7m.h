#ifndef ARMV6M_V7M_H
#define ARMV6M_V7M_H

/* 
 * Common architectural features of ARMv6-M and ARMv7-M.
 *
 * When a feature (register, peripheral, bit) has a compatible definition in
 * both ARMv6-M and ARMv7-M, we use the ARMv6-M name.  For example, the ARMv7-M
 * Flash Patch and Breakpoint unit is backwards-compatible with the ARMv6-M
 * BreakPoint Unit, so we call it a BPU.
 */

#include "arm.h"
#include "rptr.h"
#include "bitfield.h"

#include <stdint.h>

namespace ARMv6M_v7M
{

/*******************************************************************************
 * The System Control Block.
 */
namespace SCB
{
    static rptr<ARM::word_t> const AIRCR(0xE000ED0C);
    static ARM::word_t const AIRCR_VECTKEY = 0x05FA << 16;
    static ARM::word_t const AIRCR_SYSRESETREQ = 1 << 2;
    static ARM::word_t const AIRCR_VECTRESET   = 1 << 0;  // ARMv7-M only!

    static rptr<ARM::word_t> const DFSR(0xE000ED30);
    static ARM::word_t const DFSR_EXTERNAL = 1 << 4;
    static ARM::word_t const DFSR_VCATCH   = 1 << 3;
    static ARM::word_t const DFSR_DWTTRAP  = 1 << 2;
    static ARM::word_t const DFSR_BKPT     = 1 << 1;
    static ARM::word_t const DFSR_HALTED   = 1 << 0;
    static ARM::word_t const DFSR_reason_mask = 0x1F;
}

/*******************************************************************************
 * The Debug Control Block.
 */
namespace DCB
{
    static rptr<ARM::word_t> const DHCSR(0xE000EDF0);
    static ARM::word_t const DHCSR_update_mask = 0xFFFF;
    static ARM::word_t const DHCSR_DBGKEY    = 0xA05F << 16;
    static ARM::word_t const DHCSR_S_REGRDY  =      1 << 16;
    static ARM::word_t const DHCSR_S_HALT    =      1 << 17;
    static ARM::word_t const DHCSR_C_HALT    =      1 <<  1;
    static ARM::word_t const DHCSR_C_DEBUGEN =      1 <<  0;

    static rptr<ARM::word_t> const DCRSR(0xE000EDF4);
    static ARM::word_t const DCRSR_READ  = 0 << 16;
    static ARM::word_t const DCRSR_WRITE = 1 << 16;

    static rptr<ARM::word_t> const DCRDR(0xE000EDF8);

    static rptr<ARM::word_t> const DEMCR(0xE000EDFC);
    static ARM::word_t const DEMCR_VC_CORERESET = 1 <<  0;
    static ARM::word_t const DEMCR_VC_HARDERR   = 1 << 10;
    static ARM::word_t const DEMCR_DWTENA       = 1 << 24;
}

/*******************************************************************************
 * The BreakPoint Unit (ARMv6-M).  Compatible with ARMv7-M's more complex Flash
 * Patch and Breakpoint (FPB) unit.
 */
namespace BPU
{
    static rptr<ARM::word_t> const BP_CTRL(0xE0002000);
    static ARM::word_t    const BP_CTRL_KEY    = 1 << 1;
    static ARM::word_t    const BP_CTRL_ENABLE = 1 << 0;
    static Bitfield<7, 4> const BP_CTRL_NUM_CODE;

    // ARMv7M extension
    static Bitfield<14, 12> const BP_CTRL_NUM_CODE2;
    static Bitfield<11,  8> const BP_CTRL_NUM_LIT;

    /*
     * ARMv6M can have up to 16 breakpoints.  ARMv7M can have 128.  Either way,
     * this register is the first.
     */
    static rptr<ARM::word_t> const BP_COMP0(0xE0002008);

    static ARM::word_t const BP_COMPx_MATCH_NONE = 0 << 30;
    static ARM::word_t const BP_COMPx_MATCH_LOW  = 1 << 30;
    static ARM::word_t const BP_COMPx_MATCH_HIGH = 2 << 30;
    static ARM::word_t const BP_COMPx_MATCH_BOTH = 3 << 30;

    static ARM::word_t const BP_COMPx_COMP_mask = 0x1FFFFFFC;

    static ARM::word_t const BP_COMPx_ENABLE = 1 << 0;
}

/*******************************************************************************
 * The Data Watchpoint and Trace unit (ARMv6-M).  Compatible with ARMv7-M.
 */
namespace DWT
{
    static rptr<ARM::word_t> const DWT_CTRL(0xE0001000);
    static Bitfield<31, 28> const DWT_CTRL_NUMCOMP;
    static ARM::word_t      const DWT_CTRL_NOTRCPKT  = 1 << 27;
    static ARM::word_t      const DWT_CTRL_NOEXTTRIG = 1 << 26;
    static ARM::word_t      const DWT_CTRL_NOCYCCNT  = 1 << 25;
    static ARM::word_t      const DWT_CTRL_NOPRFCNT  = 1 << 24;
}


}  // namespace ARMv6M_v7M

#endif  // ARMV6M_V7M_H
