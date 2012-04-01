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
    bl delay

    ldr r0, =start_banner
    bl semihost_puts

    ldr r4, =0x50008000     @ Load address of GPIO0DIR into r4.
    movs r5, #(1 << 7)      @ Load GPIO pin mask into r5.
    movs r6, #0             @ Get a handy zero into r6.

    str r5, [r4]            @ Make LED pin an output.

    ldr r4, =0x50003FFC     @ Load unmasked GPIO0DATA address into r4.

1:  str r5, [r4]            @ Light LED.
    bl echo                 @ Echo a character.
    str r6, [r4]            @ Turn off LED.
    bl echo                 @ Echo another character.

    b 1b                    @ Repeat ad nauseum.

.thumb_func
bad_interrupt:
	b .

@ echo
@ Reads a character using the semi-hosting interface and reports it back.
@
@ Inputs: none.
@ Outputs: none.
@ Clobbers:
.thumb_func
echo:
    push {r4, lr}           @ Free up some registers.

    bl semihost_getchar     @ Read character into R0.
    movs r4, r0             @ Copy it up into callee-saved space.
    
    ldr r0, =echo_message   @ Prepare the echo message.
    bl semihost_puts        @ Print it.

    movs r0, r4             @ Move the character back into r0.
    bl semihost_putc        @ Print it.

    movs r0, #10            @ Move a newline into r0.
    bl semihost_putc        @ Print it.

    pop {r4, pc}            @ Restore context and return.


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

@ semihost_puts
@ Prints a null-terminated string to a debugger using semi-hosting.
@
@ Inputs:
@  r0 - address of first character to print.
@ Outputs: none
@ Clobbers: r0, r1
.thumb_func
semihost_puts:
    movs r1, r0             @ Move address into parameter register.
    movs r0, #0x4           @ Set operation to SYS_WRITE0.
    bkpt 0xAB               @ Go!
    bx lr                   @ Return.

@ semihost_putc
@ Prints single character to the debugger using semi-hosting.
@
@ Inputs:
@  r0 - character to print.
@ Outputs: none
@ Clobbers: r0, r1
.thumb_func
semihost_putc:
    movs r1, r0             @ Move character into parameter register.
    movs r0, #0x3           @ Set operation to SYS_WRITEC.
    bkpt 0xAB               @ Go!
    bx lr                   @ Return.

@ semihost_getchar
@ Reads a single character from the debugger using semi-hosting.
@
@ Inputs: none
@ Outputs:
@  r0 - character from debugger.
@ Clobbers: r1
.thumb_func
semihost_getchar:
    movs r1, #0             @ Clear parameter register.
    movs r0, #0x7           @ Set operation to SYS_READC.
    bkpt 0xAB               @ Go!
    bx lr                   @ Return.

.section .rodata
    .align 4
start_banner:
    .asciz "Hello Semi-Hosted World!\n"

    .align 4
echo_message:
    .asciz "You typed: "
