LED Blink Example
=================

This simple program demonstrates that your programmer is set up correctly.
When loaded onto the LPC1102, it toggles PIO0.9 at around 1Hz.  You can check
correct operation either with an oscilloscope, or by attaching an LED.

To use:

    $ make
    $ swddude -flash test.bin -fix_lpc_checksum
