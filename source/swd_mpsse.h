#ifndef SWD_MPSSE_H
#define SWD_MPSSE_H

/*
 * A SWDDriver implementation that uses the FTDI FT232H's MPSSE for
 * communication.
 */

#include "libs/error/error_stack.h"

#include "swd.h"

#include <stdint.h>

// Forward declaration to avoid importing <ftdi.h>, which pollutes.
struct ftdi_context;

class MPSSESWDDriver : public SWDDriver {
  ftdi_context *_ftdi;

public:
  MPSSESWDDriver(ftdi_context *);

  /*
   * See SWDDriver for documentation of these functions.
   */

  virtual Err::Error initialize();
  virtual Err::Error enter_reset();
  virtual Err::Error leave_reset();
  virtual Err::Error read(unsigned address, bool debug_port, uint32_t *data);
  virtual Err::Error write(unsigned address, bool debug_port, uint32_t data);
};

#endif  // SWD_MPSSE_H
