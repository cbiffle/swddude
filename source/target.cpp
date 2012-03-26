#include "target.h"

#include "swd_dp.h"
#include "swd.h"
#include "armv6m_v7m.h"

#include "libs/error/error_stack.h"
#include "libs/log/log_default.h"

using Err::Error;
using namespace Log;
using namespace ARM;
using namespace ARMv6M_v7M;

/*******************************************************************************
 * AP registers in the MEM-AP.
 */
namespace MEM_AP
{
    static uint32_t const CSW = 0x00;
    static uint32_t const CSW_RESERVED_mask = 0xFFFFF000;

    static uint32_t const CSW_TRINPROG = 1 << 7;

    static uint32_t const CSW_ADDRINC_OFF    = 0 << 4;
    static uint32_t const CSW_ADDRINC_SINGLE = 1 << 4;
    static uint32_t const CSW_ADDRINC_PACKED = 2 << 4;

    static uint32_t const CSW_SIZE_1 = 0 << 0;
    static uint32_t const CSW_SIZE_2 = 1 << 0;
    static uint32_t const CSW_SIZE_4 = 2 << 0;

    static uint32_t const TAR = 0x04;
    static uint32_t const DRW = 0x0C;
}

/*******************************************************************************
 * Target private methods
 */

Error Target::select_bank_for_address(uint8_t address)
{
    uint8_t bank = address & 0xF0;

    if (_current_ap_bank != bank) {
        Check(_dap.select_ap_bank(_mem_ap_index, bank));
        _current_ap_bank = bank;
    }

    return Err::success;
}

Error Target::write_ap(uint8_t address, uint32_t data)
{
    Check(select_bank_for_address(address));
    return _dap.write_ap_in_bank(address, data);
}

Error Target::start_read_ap(uint8_t address)
{
    Check(select_bank_for_address(address));
    return _dap.start_read_ap_in_bank(address);
}

Error Target::step_read_ap(uint8_t nextAddress, uint32_t * lastData)
{
    Check(select_bank_for_address(nextAddress));
    return _dap.step_read_ap_in_bank(nextAddress, lastData);
}

Error Target::final_read_ap(uint32_t * data)
{
    return _dap.read_rdbuff(data);
}

Error Target::peek32(uint32_t address, uint32_t * data)
{
    Check(write_ap(MEM_AP::TAR, address));
    CheckRetry(start_read_ap(MEM_AP::DRW), 100);
    CheckRetry(final_read_ap(data), 100);

    debug(3, "peek32(%08X) = %08X", address, *data);

    return Err::success;
}

Error Target::poke32(uint32_t address, uint32_t data)
{
    debug(3, "poke32(%08X, %08X)", address, data);

    Check(write_ap(MEM_AP::TAR, address));
    CheckRetry(write_ap(MEM_AP::DRW, data), 100);

    // Block waiting for write to complete.
    uint32_t csw = MEM_AP::CSW_TRINPROG;
    while (csw & MEM_AP::CSW_TRINPROG)
    {
        CheckRetry(start_read_ap(MEM_AP::CSW), 100);
        Check(final_read_ap(&csw));
    }

    return Err::success;
}

/*******************************************************************************
 * Target public methods: construction/initialization
 */

Target::Target(SWDDriver & swd, DebugAccessPort & dap, uint8_t mem_ap_index) :
    _swd(swd),
    _dap(dap),
    _mem_ap_index(mem_ap_index),
    _current_ap_bank(-1) {}

Error Target::initialize()
{
    // We only use one AP.  Go ahead and select it and configure CSW.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    uint32_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask) | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    // Enable debugging.
    uint32_t dhcsr;
    Check(peek32(DCB::DHCSR, &dhcsr));
    if ((dhcsr & (1 << 0)) == 0)
    {
        Check(poke32(DCB::DHCSR, (dhcsr & DCB::DHCSR_update_mask)
                    | DCB::DHCSR_DBGKEY
                    | DCB::DHCSR_C_DEBUGEN));
    }

    return Err::success;
}


/*******************************************************************************
 * Target public methods: memory access
 */

Error Target::read_words(uint32_t target_addr,
                         void * host_buffer,
                         size_t count)
{
    uint32_t * host_buffer_as_words = static_cast<uint32_t *>(host_buffer);

    // Configure MEM-AP for auto-incrementing 32-bit transactions.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    uint32_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask)
        | MEM_AP::CSW_ADDRINC_SINGLE
        | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    // Load Transfer Address Register with first address.
    Check(write_ap(MEM_AP::TAR, target_addr));

    // Transfer using pipelined reads.
    CheckRetry(start_read_ap(MEM_AP::DRW), 100);
    for (size_t i = 0; i < count; ++i)
    {
        CheckRetry(step_read_ap(MEM_AP::DRW, &host_buffer_as_words[i]), 100);
    }

    return Err::success;
}

Error Target::read_word(uint32_t addr, uint32_t * data)
{
    return peek32(addr, data);
}

Error Target::write_words(void const * host_buffer,
                          uint32_t target_addr,
                          size_t count)
{
    uint32_t const * host_buffer_as_words =
        static_cast<uint32_t const *>(host_buffer);

    // Configure MEM-AP for auto-incrementing 32-bit transactions.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    uint32_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask)
        | MEM_AP::CSW_ADDRINC_SINGLE
        | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    // Load Transfer Address Register with first address.
    Check(write_ap(MEM_AP::TAR, target_addr));

    for (size_t i = 0; i < count; ++i)
    {
        Check(write_ap(MEM_AP::DRW, host_buffer_as_words[i]));
    }

    return Err::success;
}

Error Target::write_word(uint32_t addr, uint32_t data)
{
    return poke32(addr, data);
}


/*******************************************************************************
 * Target public methods: register access
 */

Error Target::read_register(Register::Number reg, uint32_t * out)
{
    Check(poke32(DCB::DCRSR, DCB::DCRSR_READ | (reg & 0x1F)));

    uint32_t dhcsr;
    do
    {
        Check(peek32(DCB::DHCSR, &dhcsr));
    }
    while ((dhcsr & DCB::DHCSR_S_REGRDY) == 0);

    return peek32(DCB::DCRDR, out);
}

Error Target::write_register(Register::Number reg, uint32_t data)
{
    Check(poke32(DCB::DCRDR, data));
    Check(poke32(DCB::DCRSR, DCB::DCRSR_WRITE | (reg & 0x1F)));

    uint32_t dhcsr;
    do
    {
        Check(peek32(DCB::DHCSR, &dhcsr));
    }
    while ((dhcsr & DCB::DHCSR_S_REGRDY) == 0);

    return Err::success;
}


/*******************************************************************************
 * Target public methods: reset and halt management
 */

Error Target::reset_and_halt()
{
    // Save old DEMCR just in case.
    uint32_t demcr;
    Check(peek32(DCB::DEMCR, &demcr));

    // Write DEMCR back to request Vector Catch.
    Check(poke32(DCB::DEMCR, demcr | DCB::DEMCR_VC_CORERESET
                                   | DCB::DEMCR_VC_HARDERR
                                   | DCB::DEMCR_DWTENA));

    // Request a processor-local reset.
    Check(poke32(SCB::AIRCR, SCB::AIRCR_VECTKEY | SCB::AIRCR_VECTRESET));

    // Wait for the processor to halt.
    CheckRetry(poll_for_halt(SCB::DFSR_VCATCH), 1000);

    // Restore DEMCR.
    Check(poke32(DCB::DEMCR, demcr));

    return Err::success;
}

Error Target::halt()
{
    return poke32(DCB::DHCSR, DCB::DHCSR_DBGKEY
                            | DCB::DHCSR_C_HALT
                            | DCB::DHCSR_C_DEBUGEN);
}

Error Target::poll_for_halt(uint32_t dfsr_mask)
{
    uint32_t dhcsr;
    Check(peek32(DCB::DHCSR, &dhcsr));
    uint32_t dfsr;
    Check(peek32(SCB::DFSR, &dfsr));

    debug(3, "poll_for_halt: DHCSR=%08X DFSR=%08X", dhcsr, dfsr);

    if ((dhcsr & DCB::DHCSR_S_HALT) && (dfsr & dfsr_mask)) return Err::success;

    return Err::try_again;
}

Error Target::resume()
{
    return poke32(DCB::DHCSR, DCB::DHCSR_DBGKEY
                            | 0  // Do not set C_HALT
                            | DCB::DHCSR_C_DEBUGEN);
}

Error Target::is_halted(bool * flag)
{
    uint32_t dhcsr;
    Check(peek32(DCB::DHCSR, &dhcsr));

    *flag = dhcsr & DCB::DHCSR_S_HALT;

    return Err::success;
}

Error Target::read_halt_state(uint32_t * out)
{
    uint32_t dfsr;
    Check(peek32(SCB::DFSR, &dfsr));

    *out = dfsr & SCB::DFSR_reason_mask;

    return Err::success;
}

Error Target::reset_halt_state()
{
    return poke32(SCB::DFSR, SCB::DFSR_reason_mask);
}


/*******************************************************************************
 * Breakpoints
 */

Error Target::enable_breakpoints()
{
    return poke32(BPU::BP_CTRL, BPU::BP_CTRL_KEY | BPU::BP_CTRL_ENABLE);
}

Error Target::disable_breakpoints()
{
    return poke32(BPU::BP_CTRL, BPU::BP_CTRL_KEY);
}

Error Target::are_breakpoints_enabled(bool * result)
{
    uint32_t ctrl;
    Check(peek32(BPU::BP_CTRL, &ctrl));

    *result = ctrl & BPU::BP_CTRL_KEY;

    return Err::success;
}

Error Target::get_breakpoint_count(size_t * n)
{
    uint32_t ctrl;
    Check(peek32(BPU::BP_CTRL, &ctrl));

    *n = (ctrl & BPU::BP_CTRL_NUM_CODE_mask) >> BPU::BP_CTRL_NUM_CODE_pos;

    return Err::success;
}

Error Target::enable_breakpoint(size_t n, uint32_t addr)
{
    // Note: we ignore bit 0 of the address to permit Thumb-style addresses.

    // Address must point into code region (bottom 512MiB)
    if (addr & 0xE0000000) return Err::argument_error;

    // Break on upper or lower halfword, depending on bit 1 of address.
    uint32_t reg = BPU::BP_COMP0 + (n * sizeof(uint32_t));
    return poke32(reg, ((addr & 2) ? BPU::BP_COMPx_MATCH_HIGH
                                   : BPU::BP_COMPx_MATCH_LOW)
                     | (addr & BPU::BP_COMPx_COMP_mask)
                     | BPU::BP_COMPx_ENABLE);
}

Error Target::disable_breakpoint(size_t n)
{
    return poke32(BPU::BP_COMP0 + (n * sizeof(uint32_t)), 0);
}
