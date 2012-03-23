#ifndef SWD_H
#define SWD_H

/*
 * Abstract base class for SWD interface drivers.
 */

#include "libs/error/error_stack.h"

#include <stdint.h>

/*
 * SWDDriver provides a low-level interface to SWD interface devices.
 * Each function maps directly to a SWD protocol concept.  The ARM ADIv5
 * specification explains the SWD protocol in more detail; it's available
 * behind a clickwrap license on ARM's site.
 *
 * Client software should rarely interact with SWDDriver directly.  Instead,
 * wrap it in another object that provides a higher-level more pleasant
 * interface with support for things like retries and named registers.
 */
class SWDDriver {
public:
  /*
   * Creates an instance of the driver.  Because the constructor can't return
   * any meaningful error condition, implementations should only do things that
   * can't fail -- such as copying in constructor arguments and initializing
   * memory.
   */
  SWDDriver() {}

  /*
   * Destroys this driver instance.  Because the destructor can't return any
   * meaningful error condition, any resource cleanup here is last-resort.
   * Clients should call the close() function to clean up resources in a way
   * that can report errors.  Destructor implementations may optionally log
   * attempts to destroy a driver instance without calling close(), as this
   * indicates a programming error.
   */
  virtual ~SWDDriver() {}

  /*
   * Initialize the SWD link to the target, per the "Connection and line reset
   * sequence" defined by the ARM ADI v5.
   *
   * If this function returns successfully, it indicates that the interface is
   * functioning, and that an attached microprocessor has responded to us.  The
   * state of the target is unknown -- in particular, the contents of the Debug
   * Access Port's SELECT and CTRL/STAT registers are undefined.
   *
   * Callers may optionally provide a pointer to a 32-bit variable to receive
   * the target Debug Access Port's IDCODE value.  This does not identify the
   * target itself -- only the implementation of the DAP.  If the idcode pointer
   * is zero, the IDCODE value won't be saved anywhere, and can be retrieved
   * later using a DAP read.
   *
   * Return values:
   *  Err::success - initialization complete, target responded, IDCODE valid.
   *  Err::failure - initialization failed or target failed to respond.
   */
  virtual Err::Error initialize(uint32_t *idcode = 0) = 0;

  /*
   * Asserts the target's reset line for the specified time.  This is a
   * system-level reset that destroys state in the Debug Access Port, and so
   * it should be followed by a call to initialize().  Note that many CPUs also
   * offer a "local reset" that leaves debug state unchanged; this can be
   * accessed in a CPU-specific manner through the Debug Access Port.
   *
   * Return values:
   *  Err::success - reset asserted.
   *  Err::failure - communications with interface failed.
   */
  virtual Err::Error reset_target(uint32_t microseconds) = 0;

  /*
   * Reads a 32-bit register from either the Debug Access Port (when debug_port
   * is true) or the Access Port bank named in the DAP's SELECT register (when
   * debug_port is false).
   *
   * Access Port reads are delayed: each read returns the result of the
   * previous operation.  To kick off a read without retrieving the results of
   * the last one, pass a data pointer of zero; the result won't be written.  To
   * retrieve the results of the last Access Port read without starting a new
   * one, read the Debug Access Port's RDBUFF register instead.  
   *
   * Note that some registers can only be accessed in particular states of the
   * CTRL/STAT register, and some registers can't be read at all.
   *
   * Refer to the ARM ADIv5 spec for more information.
   *
   * Return values:
   *  Err::success - read completed, data valid.
   *  Err::try_again - target returned SWD WAIT response, read not completed.
   *  Err::failure - read failed, either in the interface or due to SWD FAULT.
   */
  virtual Err::Error read(unsigned address, bool debug_port, uint32_t *data)
      = 0;

  /*
   * Writes a 32-bit value into a register in either the Debug Access Port (when
   * debug_port is true) or the Access Port bank named in the DAP's SELECT
   * register (when debug_port is false).
   *
   * Access Port writes may take time to complete.  For a MEM-AP, monitor the
   * status of the TrInProg (transaction in progress) bit of the Access Port's
   * CSW register to detect when another write may be issued.
   *
   * Note that some registers can only be accessed in particular states of the
   * CTRL/STAT register, and some registers can't be written at all.
   *
   * Refer to the ARM ADIv5 spec for more information.
   *
   * Return values:
   *  Err::success - write completed.
   *  Err::try_again - target returned SWD WAIT response, write not completed.
   *  Err::failure - write failed, either in the interface or due to SWD FAULT.
   */
  virtual Err::Error write(unsigned address, bool debug_port, uint32_t data)
      = 0;
};

#endif  // SWD_H
