swddude
=======

*`swddude` is very young pre-alpha software.  Caveat downloader.*

`swddude` is a simple program that can flash code onto ARM Cortex
microcontrollers, such as the Cortex-M0, using SWD.  It's designed to use
programming interfaces built on the FTDI FT232H interface chip.


Why?
----

Larger ARM microcontrollers have JTAG interfaces.  OpenOCD and friends already
do a great job flashing these micros.  But for smaller parts, such as the
LPC11xx/LPC13xx series, ARM has defined a new low-pin-count debug interface
called SWD.  OpenOCD doesn't yet support SWD.

I wanted to use these little microcontrollers in my projects, so (with help from
Anton Staaf) I wrote `swddude` to scratch this itch.


What Can It Do?
---------------

Out of the box, `swddude` can flash code onto the NXP LPC11xx series.  It almost
supports the larger Cortex-M3 parts as well, like the LPC13xx -- stay tuned!


How Do I Use It?
----------------

You'll need an FTDI FT232H breakout board -- and, of course, a microcontroller
with a SWD interface.  For now, it's best to use an LPC11xx series.

Wire up your micro using the configuration described in `swd_mpsse.h`.

Build a recent version of `libftdi`.  The version commonly included in package
managers as of this writing (0.19) doesn't support the FT232H's MPSSE feature,
which we use to convert it into a SWD interface.  For best results, build
`libftdi` from HEAD.

After checking out `swddude`, build it like so:

    $ cd swddude/source
    $ make release/swddude

This will deposit a `swddude` binary in `swddude/source/release`.

To program your microcontroller, you'll need to have your desired firmware in
binary format -- not ELF, and not Intel hex.  Assuming it's in a file called
`firmware.bin`, you run:

    $ swddude -flash firmware.bin -fix_lpc_checksum

That last option, `-fix_lpc_checksum`, adds the vector table checksum expected
by the NXP LPC series.  Without it, your firmware won't run!  If some other tool
has already written the correct checksum into your firmware, you can omit that
option.


Status and Known Issues
-----------------------

`swddude` can flash the LPC1114 on the LPCxpresso breakout board, as long as the
proprietary programming system (on the side of the board with the USB connector)
has been disabled (by cutting the bridge traces).

It can *almost* flash the LPC1343.  Stay tuned.

Known issues:

 * Error reporting is not great.  Most failures just print a stack trace, which
   isn't helpful if you're not familiar with the source code.  In general, it's
   worth retrying at least once -- sometimes the SWD communications just need to
   be reset.
 * `swddude` makes no attempt at identifying the chip it's programming.  If you
   try programming an unsupported chip, it may do very bad things -- there is no
   safety net.


Brief Tour of the Source
------------------------

The source code contains the following top-level directories:

 * `build`: Anton Staaf's build system.
 * `libs`: Anton Staaf's support libraries, several of which we use.  (We
   currently include more than we strictly need here.)
 * `source`: `swddude` itself.
