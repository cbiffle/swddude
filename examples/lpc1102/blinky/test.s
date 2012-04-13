@ Note: this code is written in an ARMv6M-compatible subset.

.cpu cortex-m3
.syntax unified
.thumb

.equ initial_sp, 0x10002000 - 32  @ 32 bytes used by IAP ROM.

.section .isr_vector

vectors:
    .word initial_sp     @ initial stack
    .word _start         @ reset vector
    .word bad_interrupt  @ NMI
    .word bad_interrupt  @ hard fault
    .word bad_interrupt  @ MPU
    .word bad_interrupt  @ bus fault
    .word bad_interrupt  @ usage fault
    .word 0,0,0,0        @ reserved
    .word bad_interrupt  @ SVC
    .word bad_interrupt  @ debug mon
    .word bad_interrupt  @ reserved
    .word bad_interrupt  @ pend SVC
    .word bad_interrupt  @ systick

.section .text

.thumb_func
_start:
@    ldr r4, =0x40044064     @ IOCON_R_PIO0_9
@    ldr r5, =((0 << 0) | (3 << 6) | (1 << 7))
@    str r5, [r4]

    ldr r4, =0x50008000     @ Load address of GPIO0DIR into r4.
    ldr r5, =(1 << 9)      @ Load GPIO pin mask into r5.
    movs r6, #0             @ Get a handy zero into r6.

    str r5, [r4]            @ Make LED pin an output.

    ldr r4, =0x50003FFC     @ Load unmasked GPIO0DATA address into r4.

1:  str r5, [r4]            @ Light LED.
    bl delay                @ Wait
    str r6, [r4]            @ Turn off LED.
    bl delay                @ Wait

    b 1b                    @ Repeat ad nauseum.
    .pool

.thumb_func
bad_interrupt:
	b .

@ delay
@ Spin-waits for a while.  "A while" will depend on the processor revision and
@ clock speed, but it's set up to be roughly a second using the default
@ processor clock.
@
@ Inputs: none.
@ Outputs: none.
@ Clobbers: r0.
.thumb_func
delay:
    ldr r0, =0x100000       @ Load iteration count into r0.

1:  subs r0, #1             @ Decrement.
    bne 1b                  @ Repeat if not equal to zero.

    bx lr                   @ Return!

    .align 4

