#ifndef SWD_MPSSE_H
#define SWD_MPSSE_H

/*
 * A SWDDriver implementation that uses the FTDI FT232H's MPSSE for
 * communication.
 */

#include "swd.h"

#include "libs/error/error_stack.h"

#include <stdint.h>

// Forward declaration to avoid importing <ftdi.h>, which pollutes.
struct ftdi_context;

struct MPSSEPinConfig
{
    uint8_t	low_state;
    uint8_t	low_direction;
    uint8_t	high_state;
    uint8_t	high_direction;
};

struct MPSSEConfig
{
    uint16_t		default_vid;
    uint16_t		default_pid;
    int			default_interface;
    MPSSEPinConfig	idle_read;
    MPSSEPinConfig	idle_write;
    MPSSEPinConfig	reset_target;
    MPSSEPinConfig	reset_swd;
};

MPSSEConfig const	um232h_config =
{
    0x0403, 0x6014, 0,
    {0x09, 0x09, 0b00000000, 0b00000000}, //idle read
    {0x09, 0x0b, 0b00000000, 0b00000000}, //idle write
    {0x01, 0x0b, 0b00000000, 0b00000000}, //reset target
    {0x0b, 0x0b, 0b00000000, 0b00000000}, //reset swd
};

MPSSEConfig const	bus_blaster_config =
{
    0x0403, 0x6010, 0,
    {0x09, 0x29, 0b10110111, 0b01011000}, //idle read
    {0x09, 0x2b, 0b10100111, 0b01011000}, //idle write
    {0x01, 0x2b, 0b10100101, 0b01011010}, //reset target
    {0x0b, 0x2b, 0b10100111, 0b01011000}, //reset swd
};

class MPSSESWDDriver : public SWDDriver
{
    MPSSEConfig	const &	_config;
    ftdi_context *	_ftdi;

public:
    MPSSESWDDriver(MPSSEConfig const & config,
		   ftdi_context * ftdi);

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
