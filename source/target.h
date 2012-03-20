#ifndef TARGET_H
#define TARGET_H

#include "libs/error/error_stack.h"

#include <stdint.h>
#include <stddef.h>

class DebugAccessPort;

class Target {
  DebugAccessPort &_dap;
  uint8_t _mem_ap_index;

  int32_t _current_ap_bank;

  Err::Error select_bank_for_address(uint8_t address);

  Err::Error write_ap(uint8_t address, uint32_t data);

  Err::Error post_read_ap(uint8_t address);
  Err::Error read_ap_pipelined(uint8_t nextAddress, uint32_t *lastData);
  Err::Error final_read_ap();

public:
  Target(DebugAccessPort *, uint8_t mem_ap_index);

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
  };

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
   * Enables halting debug.  If halting debug is already enabled, this has no
   * effect.
   */
  Err::Error enable_halting_debug();

  /*
   * Disables halting debug.  If the processor is currently halted, it will
   * resume as though resume() had been called.  If halting debug is not
   * enabled, this has no effect.
   */
  Err::Error disable_halting_debug();

  /*
   * Halts the processor.  If the processor is already halted, this has no
   * effect.
   */
  Err::Error halt();

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
   * Issues a local (processor) reset without losing state in the debug unit.
   */
  Err::Error reset_processor();

  /*
   * Issues a complete (system) reset that may lose state in the debug unit.
   */
  Err::Error reset_system();

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
