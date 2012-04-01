swddude
=======

*`swddude` is very young pre-alpha software.  Caveat downloader.*

`swddude` is a collection of simple tools for programming and using ARM Cortex
microcontrollers, such as the Cortex-M0 and M3, using the SWD protocol.


Why?
----

Larger ARM microcontrollers have JTAG interfaces.  OpenOCD and friends already
do a great job flashing these micros.  But for smaller parts, such as the
LPC11xx/LPC13xx series, ARM has defined a new low-pin-count debug interface
called SWD.  OpenOCD doesn't yet support SWD.  In fact, when we started writing
`swddude`, no software on Mac or Linux could do what we wanted!

It's a shame not to be able to use these powerful little microcontrollers, so we
wrote `swddude` to scratch this itch.


What Can It Do?
---------------

Currently:

 * `swddude` itself can flash code onto the NXP LPC11xx (Cortex-M0 based) and
   LPC13xx (Cortex-M3 based).
 * `swdprobe` can interrogate a SWD-compatible chip and dump information about
   what it finds.  This is useful when adding support for new chips to
   `swddude`.
 * `swdhost` provides semihosting I/O for an attached microcontroller.  With
   semihosting, embedded software can send `printf`-style messages to a host
   computer through the debug connection -- no UART required.
 * `swddump` extracts the contents of Flash from a supported microcontroller.

We're working to extend the tools to support more microcontroller varieties.
Specifically, we're focusing on microcontrollers without JTAG ports -- devices
that can't be easily added to OpenOCD (yet).


How Do I Use It?
----------------

You'll need a supported programmer and, of course, a supported microcontroller
with a SWD interface.  Currently `swddude` supports using the Bus Blaster (v2.5
programmed with the KT-link compatible CPLD configuration) or any FTDI
development board with an FT232H or FT2232H chip that has been wired to the SWD
lines of your microcontroller.

Wire up your micro using the configuration described in `swd_mpsse.h`.

Install `libusb` 1.0 and the `libusb-compat` package.  The version supplied by
your package manager (apt, Homebrew, etc.) should be fine.

Build a recent version of `libftdi`.  As of this writing, you must build
`libftdi` from HEAD -- the released version (0.20) still uses the legacy
`libusb` 0.1 APIs and is incompatible with `swddude`.

After checking out `swddude`, build it like so:

    $ cd swddude/source
    $ make swddude release

This will deposit a `swddude` binary in `swddude/source/release`.

To program your microcontroller, you'll need to have your desired firmware in
binary format -- not ELF, and not Intel hex.  Assuming it's in a file called
`firmware.bin`, you run:

    $ swddude -flash firmware.bin -fix_lpc_checksum

This will default to the um232h programmer (FTDI's FT232H development board)
configuration.  If you are using a Bus Blaster, you should add
`-programmer bus_blaster` to the command line above.

That last option, `-fix_lpc_checksum`, adds the vector table checksum expected
by the NXP LPC series.  Without it, your firmware won't run!  If some other tool
has already written the correct checksum into your firmware, you can omit that
option.


Status and Known Issues
-----------------------

These boards are known to work:

 * LPCxpresso LPC1114.
 * LPCxpresso LPC11C24.
 * LPCxpresso LPC1343.

Note that the LPCxpresso boards will only work if you disable the proprietary
LPC-Link programming device on the board.  On newer boards, you can do this by
clearing solder jumpers between the two sections of the board; older boards make
you physically cut traces in the same position.

Known issues:

 * Error reporting is not great.  Most failures just print a stack trace, which
   isn't helpful if you're not familiar with the source code.  In general, it's
   worth retrying at least once -- sometimes the SWD communications just need to
   be reset.
 * `swddude` makes no attempt at identifying the chip it's programming.  If you
   try programming an unsupported chip, it may do very bad things -- there is no
   safety net.
 * On the LPC1343 specifically, the SWD interfaces sometimes gets "stuck" and
   requires a power-cycle.  This will show up as failures very early during
   communication (often referencing the IDCODE register).


Brief Tour of the Source
------------------------

The source code contains the following top-level directories:

 * `build`: Anton Staaf's build system.
 * `libs`: Anton Staaf's support libraries, several of which we use.  (We
   currently include more than we strictly need here.)
 * `source`: `swddude` and friends.

The source tree uses git's submodule feature aggressively.  If you check out
`swddude` using a simple `git clone`, you'll be left with empty directories that
won't build.  You can fix this by running

    git submodule update --recursive

Alternatively, you can make this happen automatically when you clone, like so:

    git clone --recursive ${git_url}
