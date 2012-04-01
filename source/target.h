#ifndef TARGET_H
#define TARGET_H

/*
 * A high-level interface for manipulating a remote ("target") processor.
 * Target provides a facade over the specifics of SWD and ARM ADIv5, translating
 * them into concepts familiar from debuggers like GDB.
 */

#include "arm.h"
#include "rptr.h"

#include "libs/error/error_stack.h"

#include <stdint.h>
#include <stddef.h>

// Forward decls of our two collaborators
class DebugAccessPort;
class SWDDriver;


class Target
{
    // State set during construction:
    SWDDriver &_swd;        // Underlying SWD driver.
    DebugAccessPort &_dap;  // DAP to wrap.
    uint8_t _mem_ap_index;  // Index of the sole AP used (a MEM-AP); often 0.

    // State updated during use:
    rptr<ARM::word_t> _tar;  // Contents of Transfer Address Register.

    // Writes data to a register in AP #0.
    Err::Error write_ap(uint8_t address, ARM::word_t data);

    /*
     * start_read_ap, step_read_ap, and final_read_ap map to the similarly-named
     * functions in DebugAccessPort -- but they implicitly pass the ID of our
     * single AP, to save typing.
     */
    Err::Error start_read_ap(uint8_t address);
    Err::Error step_read_ap(uint8_t next_address, ARM::word_t * last_data);
    Err::Error final_read_ap(ARM::word_t * data);

    // Updates TAR to point to the desired location, if necessary.
    Err::Error write_tar(rptr_const<ARM::word_t>);

public:
    Target(SWDDriver &, DebugAccessPort &, uint8_t mem_ap_index);

    /*
     * Initializes this object and the debug unit of the remote system.
     *
     * This can be called more than once to re-initialize; it will reset debug
     * state on the target.
     *
     * TODO: currently it incompletely resets debug state.
     */
    Err::Error initialize(bool enable_debugging = true);

    /*
     * Reads some number of 32-bit words from the target into memory on the
     * host.
     *
     * target_addr gives the start address in the target's memory.  This is a
     * byte address and must be word-aligned.
     *
     * host_buffer gives the destination for the words in host memory.  It
     * should observe any alignment restrictions for 32-bit quantities on the
     * host.
     *
     * count gives the number of words -- not bytes! -- to transfer.
     */
    Err::Error read_words(rptr_const<ARM::word_t> target_addr,
                          ARM::word_t * host_buffer,
                          size_t count);

    /*
     * Single-word equivalent of read_words.  Slightly cheaper for moving
     * small numbers of non-contiguous words around.
     */
    Err::Error read_word(rptr_const<ARM::word_t> target_addr,
                         ARM::word_t * host_buffer);

    /*
     * Reads some number of 32-bit words from the target into memory on the
     * host.
     *
     * host_buffer gives the start of host memory containing words to write into
     * target memory.  It should observe any alignment restrictions for 32-bit
     * quantities on the host.
     *
     * target_addr gives the target memory address where words will be placed.
     * This is a byte address and must be word-aligned.
     *
     * count gives the number of words -- not bytes! -- to transfer.
     */
    Err::Error write_words(ARM::word_t const * host_buffer,
                           rptr<ARM::word_t> target_addr,
                           size_t count);

    /*
     * Single-word equivalent of write_words.  Slightly cheaper for moving
     * small numbers of non-contiguous words around.
     */
    Err::Error write_word(rptr<ARM::word_t> target_addr, ARM::word_t data);

    /*
     * Reads the contents of one of the processor's core or special-purpose
     * registers.  This will only work when the processor is halted.
     */
    Err::Error read_register(ARM::Register::Number, ARM::word_t *);

    /*
     * Replaces the contents of one of the processor's core or special-purpose
     * registers.  This will only work when the processor is halted.
     */
    Err::Error write_register(ARM::Register::Number, ARM::word_t);

    /*
     * Overload of write_register that allows rptrs to be used directly.
     */
    template <typename type>
    inline Err::Error write_register(ARM::Register::Number r, rptr<type> p)
    {
        return write_register(r, p.bits());
    }

    /*
     * Overload of write_register that allows rptr_consts to be used directly.
     */
    template <typename type>
    inline Err::Error write_register(ARM::Register::Number r,
                                     rptr_const<type> p)
    {
        return write_register(r, p.bits());
    }

    /*
     * Triggers a processor-local reset (leaving debug state unchanged) and asks
     * the processor to halt after it's complete, before executing any code.
     */
    Err::Error reset_and_halt();

    /*
     * Halts the processor.  If the processor is already halted, this has no
     * effect.
     */
    Err::Error halt();

    /*
     * Checks whether the processor is halted with certain bits in DFSR set
     * (given as a mask). If it isn't, returns Err::try_again.  Intended for use
     * with CheckRetry.
     *
     * Note: a mask of all ones will match any halt condition.
     */
    Err::Error poll_for_halt(unsigned dfsr_mask);

    /*
     * Resumes the halted processor at the address held in the Debug Return
     * register (aka r15).  If the processor is not halted, this has no effect.
     */
    Err::Error resume();

    /*
     * Checks whether the processor is halted.
     */
    Err::Error is_halted(bool *);

    /*
     * Finds out why the processor is halted, assuming it's halted.  Bits in the
     * result are set to a combination of the DFSR values defined in
     * ARMv6M_v7M::SCB.
     */
    Err::Error read_halt_state(ARM::word_t *);

    /*
     * Clears the sticky halt state flags.
     */
    Err::Error reset_halt_state();

    /*
     * Enables hardware breakpoint support.
     */
    Err::Error enable_breakpoints();

    /*
     * Disables hardware breakpoint support.
     */
    Err::Error disable_breakpoints();

    /*
     * Checks whether breakpoints are enabled.
     */
    Err::Error are_breakpoints_enabled(bool *);

    /*
     * Determines how many hardware breakpoints the target supports.
     */
    Err::Error get_breakpoint_count(size_t *);

    /*
     * Enables a hardware breakpoint and sets it to a particular address.
     *
     * The address must be halfword-aligned.
     */
    Err::Error enable_breakpoint(size_t bp,
                                 rptr_const<ARM::thumb_code_t> address);

    /*
     * Disables a hardware breakpoint.
     */
    Err::Error disable_breakpoint(size_t bp);
};

#endif  // TARGET_H
