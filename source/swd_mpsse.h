#ifndef SWD_MPSSE_H
#define SWD_MPSSE_H

/*
 * A SWDDriver implementation that uses the FTDI FT232H's MPSSE for
 * communication.
 */

#include "source/swd.h"
#include "source/mpsse_config.h"
#include "source/mpsse.h"

#include "libs/error/error_stack.h"

#include <stdint.h>

class MPSSESWDDriver : public SWDDriver
{
    MPSSEConfig const & _config;
    MPSSE *             _mpsse;

public:
    MPSSESWDDriver(MPSSEConfig const & config, MPSSE * mpsse);

    /*
     * See SWDDriver for documentation of these functions.
     */
    virtual Err::Error initialize(uint32_t *);
    virtual Err::Error enter_reset();
    virtual Err::Error leave_reset();
    virtual Err::Error read(unsigned address, bool debug_port, uint32_t *data);
    virtual Err::Error write(unsigned address, bool debug_port, uint32_t data);
};

#endif  // SWD_MPSSE_H
