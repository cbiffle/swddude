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
 * Configuration
 */

/*
 * The ARM ADIv5 docs on the algorithm for writing to memory are slightly
 * ambiguous.  Do we need to poll the CSW.TrInProg bit or not?  Not doing so
 * gives a large performance boost, and appears to work!
 *
 * However, on more complex targets like the dual-processor NXP43xx series,
 * we might have to return to a literal interpretation of the standard.  If
 * memory accesses are faulting on a new target, try setting this back to
 * true.
 *
 * (cbiffle 2012-03-31)
 */
static bool const use_careful_memory_writes = false;


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

Error Target::write_ap(uint8_t address, word_t data)
{
    return _dap.write_ap(_mem_ap_index, address, data);
}

Error Target::start_read_ap(uint8_t address)
{
    return _dap.start_read_ap(_mem_ap_index, address);
}

Error Target::step_read_ap(uint8_t next_address, word_t * last_data)
{
    return _dap.step_read_ap(_mem_ap_index, next_address, last_data);
}

Error Target::final_read_ap(word_t * data)
{
    return _dap.read_rdbuff(data);
}

/*******************************************************************************
 * Target public methods: construction/initialization
 */

Target::Target(SWDDriver & swd, DebugAccessPort & dap, uint8_t mem_ap_index) :
    _swd(swd),
    _dap(dap),
    _mem_ap_index(mem_ap_index) {}

Error Target::initialize(bool enable_debugging)
{
    debug(3, "Target::initialize(%d)", enable_debugging);

    // We only use one AP.  Go ahead and select it and configure CSW.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    word_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask) | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    if (enable_debugging)
    {
        word_t dhcsr;
        Check(read_word(DCB::DHCSR, &dhcsr));
        if ((dhcsr & (1 << 0)) == 0)
        {
            Check(write_word(DCB::DHCSR, (dhcsr & DCB::DHCSR_update_mask)
                                                | DCB::DHCSR_DBGKEY
                                                | DCB::DHCSR_C_DEBUGEN));
        }
    }

    return Err::success;
}


/*******************************************************************************
 * Target public methods: memory access
 */

Error Target::read_words(rptr_const<word_t> target_addr,
                         word_t * host_buffer,
                         size_t count)
{
    debug(3, "Target::read_words(%08X, %p, %zu)",
          target_addr.bits(),
          host_buffer,
          count);

    // Configure MEM-AP for auto-incrementing 32-bit transactions.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    word_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask)
        | MEM_AP::CSW_ADDRINC_SINGLE
        | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    // Load Transfer Address Register with first address.
    Check(write_ap(MEM_AP::TAR, target_addr.bits()));

    // Transfer using pipelined reads.
    CheckRetry(start_read_ap(MEM_AP::DRW), 100);
    for (size_t i = 0; i < count; ++i)
    {
        CheckRetry(step_read_ap(MEM_AP::DRW, &host_buffer[i]), 100);
    }

    return Err::success;
}

Error Target::read_word(rptr_const<word_t> address, word_t * data)
{
    debug(3, "Target::read_word(%08X, %p)", address.bits(), data);
    Check(write_ap(MEM_AP::TAR, address.bits()));
    CheckRetry(start_read_ap(MEM_AP::DRW), 100);
    CheckRetry(final_read_ap(data), 100);

    return Err::success;
}

Error Target::write_words(word_t const * host_buffer,
                          rptr<word_t> target_addr,
                          size_t count)
{
    debug(3, "Target::write_words(%p, %08X, %zu)",
          host_buffer,
          target_addr.bits(),
          count);

    // Configure MEM-AP for auto-incrementing 32-bit transactions.
    Check(start_read_ap(MEM_AP::CSW));  // Load previous value.
    word_t csw;
    Check(final_read_ap(&csw));
    csw = (csw & MEM_AP::CSW_RESERVED_mask)
        | MEM_AP::CSW_ADDRINC_SINGLE
        | MEM_AP::CSW_SIZE_4;
    Check(write_ap(MEM_AP::CSW, csw));  // Write it back.

    // Load Transfer Address Register with first address.
    Check(write_ap(MEM_AP::TAR, target_addr.bits()));

    for (size_t i = 0; i < count; ++i)
    {
        Check(write_ap(MEM_AP::DRW, host_buffer[i]));
    }

    return Err::success;
}

Error Target::write_word(rptr<word_t> address, word_t data)
{
    debug(3, "Target::write_word(%08X, %08X)", address.bits(), data);
    Check(write_ap(MEM_AP::TAR, address.bits()));
    CheckRetry(write_ap(MEM_AP::DRW, data), 100);

    if (use_careful_memory_writes)
    {
      // Kick off a pipelined read.
      CheckRetry(start_read_ap(MEM_AP::CSW), 100);

      // Block waiting for write to complete.
      word_t csw = MEM_AP::CSW_TRINPROG;
      while (csw & MEM_AP::CSW_TRINPROG)
      {
          CheckRetry(step_read_ap(MEM_AP::CSW, &csw), 100);
      }
    }

    return Err::success;
}


/*******************************************************************************
 * Target public methods: register access
 */

Error Target::read_register(Register::Number reg, word_t * out)
{
    debug(3, "Target::read_register(%u, %p)", reg, out);

    Check(write_word(DCB::DCRSR, DCB::DCRSR_READ | (reg & 0x1F)));

    word_t dhcsr;
    do
    {
        Check(read_word(DCB::DHCSR, &dhcsr));
    }
    while ((dhcsr & DCB::DHCSR_S_REGRDY) == 0);

    return read_word(DCB::DCRDR, out);
}

Error Target::write_register(Register::Number reg, word_t data)
{
    debug(3, "Target::write_register(%u, %08X)", reg, data);

    Check(write_word(DCB::DCRDR, data));
    Check(write_word(DCB::DCRSR, DCB::DCRSR_WRITE | (reg & 0x1F)));

    word_t dhcsr;
    do
    {
        Check(read_word(DCB::DHCSR, &dhcsr));
    }
    while ((dhcsr & DCB::DHCSR_S_REGRDY) == 0);

    return Err::success;
}


/*******************************************************************************
 * Target public methods: reset and halt management
 */

Error Target::reset_and_halt()
{
    debug(3, "Target::reset_and_halt()");

    // Save old DEMCR just in case.
    word_t demcr;
    Check(read_word(DCB::DEMCR, &demcr));

    // Write DEMCR back to request Vector Catch.
    Check(write_word(DCB::DEMCR, demcr | DCB::DEMCR_VC_CORERESET
                                       | DCB::DEMCR_VC_HARDERR
                                       | DCB::DEMCR_DWTENA));

    // Request a processor-local reset.
    Check(write_word(SCB::AIRCR, SCB::AIRCR_VECTKEY | SCB::AIRCR_SYSRESETREQ));

    // Wait for the processor to halt.
    CheckRetry(poll_for_halt(SCB::DFSR_VCATCH), 1000);

    // Restore DEMCR.
    Check(write_word(DCB::DEMCR, demcr));

    return Err::success;
}

Error Target::halt()
{
    debug(3, "Target::halt()");

    return write_word(DCB::DHCSR, DCB::DHCSR_DBGKEY
                            | DCB::DHCSR_C_HALT
                            | DCB::DHCSR_C_DEBUGEN);
}

Error Target::poll_for_halt(unsigned dfsr_mask)
{
    word_t dhcsr;
    Check(read_word(DCB::DHCSR, &dhcsr));
    word_t dfsr;
    Check(read_word(SCB::DFSR, &dfsr));

    debug(3, "Target::poll_for_halt(%u): DHCSR=%08X DFSR=%08X",
          dfsr_mask, dhcsr, dfsr);

    if ((dhcsr & DCB::DHCSR_S_HALT) && (dfsr & dfsr_mask)) return Err::success;

    return Err::try_again;
}

Error Target::resume()
{
    debug(3, "Target::resume()");
    return write_word(DCB::DHCSR, DCB::DHCSR_DBGKEY
                                | 0  // Do not set C_HALT
                                | DCB::DHCSR_C_DEBUGEN);
}

Error Target::is_halted(bool * flag)
{
    debug(3, "Target::is_halted");
    word_t dhcsr;
    Check(read_word(DCB::DHCSR, &dhcsr));

    *flag = dhcsr & DCB::DHCSR_S_HALT;

    return Err::success;
}

Error Target::read_halt_state(word_t * out)
{
    debug(3, "Target::read_halt_state(%p)", out);

    word_t dfsr;
    Check(read_word(SCB::DFSR, &dfsr));

    *out = dfsr & SCB::DFSR_reason_mask;

    return Err::success;
}

Error Target::reset_halt_state()
{
    debug(3, "Target::reset_halt_state()");
    return write_word(SCB::DFSR, SCB::DFSR_reason_mask);
}


/*******************************************************************************
 * Breakpoints
 */

Error Target::enable_breakpoints()
{
    return write_word(BPU::BP_CTRL, BPU::BP_CTRL_KEY | BPU::BP_CTRL_ENABLE);
}

Error Target::disable_breakpoints()
{
    return write_word(BPU::BP_CTRL, BPU::BP_CTRL_KEY);
}

Error Target::are_breakpoints_enabled(bool * result)
{
    word_t ctrl;
    Check(read_word(BPU::BP_CTRL, &ctrl));

    *result = ctrl & BPU::BP_CTRL_KEY;

    return Err::success;
}

Error Target::get_breakpoint_count(size_t * n)
{
    word_t ctrl;
    Check(read_word(BPU::BP_CTRL, &ctrl));

    *n = BPU::BP_CTRL_NUM_CODE.extract(ctrl);

    return Err::success;
}

Error Target::enable_breakpoint(size_t n, rptr_const<thumb_code_t> addr)
{
    // Note: we ignore bit 0 of the address to permit Thumb-style addresses.

    // Address must point into code region (bottom 512MiB)
    if (addr >= rptr<thumb_code_t>(512 * 1024 * 1024))
    {
        return Err::argument_error;
    }

    // Break on upper or lower halfword, depending on bit 1 of address.
    rptr<word_t> reg = BPU::BP_COMP0 + n;
    return write_word(reg, ((addr.bit<1>()) ? BPU::BP_COMPx_MATCH_HIGH
                                   : BPU::BP_COMPx_MATCH_LOW)
                         | (addr.bits() & BPU::BP_COMPx_COMP_mask)
                         | BPU::BP_COMPx_ENABLE);
}

Error Target::disable_breakpoint(size_t n)
{
    return write_word(BPU::BP_COMP0 + n, 0);
}
