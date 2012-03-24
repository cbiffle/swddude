#ifndef TARGET_H
#define TARGET_H

#include "libs/error/error_stack.h"

#include <stdint.h>
#include <stddef.h>

class DebugAccessPort;
class SWDDriver;

class Target {
  SWDDriver &_swd;
  DebugAccessPort &_dap;
  uint8_t _mem_ap_index;

  int32_t _current_ap_bank;

  Err::Error select_bank_for_address(uint8_t address);

  Err::Error write_ap(uint8_t address, uint32_t data);

  Err::Error start_read_ap(uint8_t address);
  Err::Error step_read_ap(uint8_t nextAddress, uint32_t *lastData);
  Err::Error final_read_ap(uint32_t *data);

  Err::Error peek32(uint32_t address, uint32_t *data);
  Err::Error poke32(uint32_t address, uint32_t data);

public:
  Target(SWDDriver &, DebugAccessPort &, uint8_t mem_ap_index);

  /*
   * Initializes this object and the debug unit of the remote system.
   *
   * This can be called more than once to re-initialize, but it will destroy
   * debug state.
   */
  Err::Error initialize();

  /*
   * Reads some number of 32-bit words from the target into memory on the host.
   *
   * target_addr gives the target memory address of the first word to read.
   * This is a byte address and must be word-aligned.
   *
   * host_buffergives the destination for the words in host memory.  It should
   * observe any alignment restrictions for 32-bit quantities on the host.
   *
   * count gives the number of words -- not bytes! -- to transfer.
   */
  Err::Error read_words(uint32_t target_addr, void *host_buffer, size_t count);

  Err::Error read_word(uint32_t target_addr, uint32_t *data);

  /*
   * Reads some number of 32-bit words from the target into memory on the host.
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
  Err::Error write_words(void const *host_buffer, uint32_t target_addr,
                          size_t count);

  Err::Error write_word(uint32_t target_addr, uint32_t data);

  enum RegisterNumber {
    // Basic numbered registers
    kR0  =  0, kR1  =  1, kR2  =  2, kR3  =  3,
    kR4  =  4, kR5  =  5, kR6  =  6, kR7  =  7,
    kR8  =  8, kR9  =  9, kR10 = 10, kR11 = 11,
    kR12 = 12, kR13 = 13, kR14 = 14, kR15 = 15,

    // Special-purpose registers
    kPSR = 16,
    kMSP = 17,
    kPSP = 18,
    // 19 unused
    kCONTROL_PRI_MASK = 20,

    // Aliases for registers
    kRStack = kR13,
    kRLink  = kR14,
    kRDebugReturn = kR15,

    kRLast = kCONTROL_PRI_MASK,
  };

  /*
   * Checks whether a register index is valid.
   */
  bool is_register_implemented(int r);


  /*
   * Reads the contents of one of the processor's core or special-purpose
   * registers.  This will only work when the processor is halted.
   */
  Err::Error read_register(RegisterNumber, uint32_t *);

  /*
   * Replaces the contents of one of the processor's core or special-purpose
   * registers.  This will only work when the processor is halted.
   */
  Err::Error write_register(RegisterNumber, uint32_t);

  /*
   * Triggers a processor-local reset (leaving debug state unchanged) and asks
   * the processor to halt after it's complete, before executing any code.  This
   * may take a bit to complete.
   */
  Err::Error reset_and_halt();

  /*
   * Halts the processor.  If the processor is already halted, this has no
   * effect.
   */
  Err::Error halt();

  /*
   * Checks whether the processor is halted with certain bits in DFSR set (given
   * as a mask). If it isn't, returns Err::try_again.  Intended for use with
   * CheckRetry.
   */
  Err::Error poll_for_halt(uint32_t dfsr_mask);

  /*
   * Resumes the halted processor at the address held in the Debug Return
   * register (aka r15).  If the processor is not halted, this has no effect.
   */
  Err::Error resume();

  /*
   * Checks whether the processor is halted.
   */
  Err::Error is_halted(bool *);

  enum HaltReason {
    kHaltPerSe    = 0x1,
    kHaltBkpt     = 0x2,
    kHaltDWT      = 0x4,
    kHaltVCatch   = 0x8,
    kHaltExternal = 0x10,
  };

  /*
   * Finds out why the processor is halted, assuming it's halted.  Bits in the
   * result are set to a combination of HaltReasons, above.
   */
  Err::Error read_halt_state(uint32_t *);

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
  Err::Error enable_breakpoint(size_t bp, uint32_t address);

  /*
   * Disables a hardware breakpoint.
   */
  Err::Error disable_breakpoint(size_t bp);
};

#endif  // TARGET_H
