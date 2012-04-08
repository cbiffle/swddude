/*
 * Copyright (c) 2012, Anton Staaf, Cliff L. Biffle.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the project nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "source/mpsse.h"

#include "libs/error/error.h"
#include "libs/log/log_default.h"

using namespace Err;
using namespace Log;

/******************************************************************************/
MPSSE::MPSSE() :
    _libusb(NULL),
    _handle(NULL)
{
}
/******************************************************************************/
MPSSE::~MPSSE()
{
    ftdi_set_bitmode(&_ftdi, 0xFF, BITMODE_RESET);
    ftdi_usb_close(&_ftdi);
    if (_handle) libusb_close(_handle);
    ftdi_deinit(&_ftdi);
    if (_libusb) libusb_exit(_libusb);
}
/******************************************************************************/
Error MPSSE::open(MPSSEConfig const & config)
{
    Error           check_error = Err::success;
    libusb_device * device;
    unsigned        chipid;

    CheckCleanupP(libusb_init(&_libusb), libusb_init_failed);
    CheckCleanupP(ftdi_init(&_ftdi), ftdi_init_failed);

    /*
     * Locate FTDI chip using it's VID:PID pair.  This doesn't uniquely identify
     * the programmer so this will need to be improved.
     */
    _handle = libusb_open_device_with_vid_pid(_libusb, config.vid, config.pid);

    CheckCleanupStringB(_handle, libusb_open_failed,
                        "No device found with VID:PID = 0x%04x:0x%04x\n",
                        config.vid, config.pid);

    CheckCleanupB(device = libusb_get_device(_handle), get_failed);

    /*
     * The interface must be selected before the ftdi device can be opened.
     */
    CheckCleanupStringP(ftdi_set_interface(&_ftdi,
                                           ftdi_interface(config.interface)),
                        interface_failed,
                        "Unable to set FTDI device interface: %s",
                        ftdi_get_error_string(&_ftdi));

    CheckCleanupStringP(ftdi_usb_open_dev(&_ftdi, device),
                        open_failed,
                        "Unable to open FTDI device: %s",
                        ftdi_get_error_string(&_ftdi));

    CheckCleanupStringP(ftdi_usb_reset(&_ftdi),
                        reset_failed,
                        "FTDI device reset failed: %s",
                        ftdi_get_error_string(&_ftdi));

    CheckCleanupP(ftdi_read_chipid(&_ftdi, &chipid), read_failed);

    debug(3, "FTDI chipid: %X", chipid);

    return success;

  read_failed:
  reset_failed:
    CheckStringP(ftdi_usb_close(&_ftdi),
                 "Unable to close FTDI device: %s",
                 ftdi_get_error_string(&_ftdi));

  open_failed:
  interface_failed:
  get_failed:
    libusb_close(_handle);
    _handle = 0;

  libusb_open_failed:
    ftdi_deinit(&_ftdi);

  ftdi_init_failed:
    libusb_exit(_libusb);
    _libusb = 0;

  libusb_init_failed:
    return check_error;
}
/******************************************************************************/
ftdi_context * MPSSE::ftdi(void)
{
    return &_ftdi;
}
/******************************************************************************/
