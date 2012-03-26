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

#include <stdint.h>

namespace ARMv6M_v7M
{

/*******************************************************************************
 * The System Control Block.
 */
namespace SCB
{
    static uint32_t const AIRCR = 0xE000ED0C;
    static uint32_t const AIRCR_VECTKEY = 0x05FA << 16;
    static uint32_t const AIRCR_VECTRESET = 1 << 0;

    static uint32_t const DFSR  = 0xE000ED30;
    static uint32_t const DFSR_EXTERNAL = 1 << 4;
    static uint32_t const DFSR_VCATCH   = 1 << 3;
    static uint32_t const DFSR_DWTTRAP  = 1 << 2;
    static uint32_t const DFSR_BKPT     = 1 << 1;
    static uint32_t const DFSR_HALTED   = 1 << 0;
    static uint32_t const DFSR_reason_mask = 0x1F;
}

/*******************************************************************************
 * The Debug Control Block.
 */
namespace DCB
{
    static uint32_t const DHCSR = 0xE000EDF0;
    static uint32_t const DHCSR_update_mask = 0xFFFF;
    static uint32_t const DHCSR_DBGKEY    = 0xA05F << 16;
    static uint32_t const DHCSR_S_REGRDY  =      1 << 16;
    static uint32_t const DHCSR_S_HALT    =      1 << 17;
    static uint32_t const DHCSR_C_HALT    =      1 <<  1;
    static uint32_t const DHCSR_C_DEBUGEN =      1 <<  0;

    static uint32_t const DCRSR = 0xE000EDF4;
    static uint32_t const DCRSR_READ  = 0 << 16;
    static uint32_t const DCRSR_WRITE = 1 << 16;

    static uint32_t const DCRDR = 0xE000EDF8;

    static uint32_t const DEMCR = 0xE000EDFC;
    static uint32_t const DEMCR_VC_CORERESET = 1 <<  0;
    static uint32_t const DEMCR_VC_HARDERR   = 1 << 10;
    static uint32_t const DEMCR_DWTENA       = 1 << 24;
}

/*******************************************************************************
 * The BreakPoint Unit (ARMv6-M).  Compatible with ARMv7-M's more complex Flash
 * Patch and Breakpoint (FPB) unit.
 */
namespace BPU
{
    static uint32_t const BP_CTRL = 0xE0002000;
    static uint32_t const BP_CTRL_KEY    = 1 << 1;
    static uint32_t const BP_CTRL_ENABLE = 1 << 0;
    static uint32_t const BP_CTRL_NUM_CODE_pos = 4;
    static uint32_t const BP_CTRL_NUM_CODE_mask = 0x7 << BP_CTRL_NUM_CODE_pos;

    // Architeturally, there can be up to 8 breakpoints.  This is the first.
    static uint32_t const BP_COMP0 = 0xE0002008;

    static uint32_t const BP_COMPx_MATCH_NONE = 0 << 30;
    static uint32_t const BP_COMPx_MATCH_LOW  = 1 << 30;
    static uint32_t const BP_COMPx_MATCH_HIGH = 2 << 30;
    static uint32_t const BP_COMPx_MATCH_BOTH = 3 << 30;

    static uint32_t const BP_COMPx_COMP_mask = 0x1FFFFFFC;

    static uint32_t const BP_COMPx_ENABLE = 1 << 0;
}

}  // namespace ARMv6M_v7M

#endif  // ARMV6M_V7M_H
