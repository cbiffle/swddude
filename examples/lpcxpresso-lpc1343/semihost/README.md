Semihosting Example
===================

This program uses semihosting to implement a console on the host computer.
You can interact with it using the swdhost tool.

When the program starts, it will wait for a debugger to attach.  It then prints
a startup message and begins echoing keystrokes entered on the host machine.

To use:

    $ make test.bin
    $ swddude -flash test.bin -fix_lpc_checksum
    $ swdhost
