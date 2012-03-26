#ifndef ARM_H
#define ARM_H

/*
 * Common definitions for ARM processors.
 */

namespace ARM
{

namespace Register {

/*
 * Registers are numbered by the processor-internal scheme (as seen in the ADIv5
 * Debug Control Block).  This assigns a common indexing scheme to
 * general-purpose, special-purpose, and floating-point registers, but has some
 * warts: in particular, there are gaps in the numbering!
 *
 * Register naming and capitalization should match the architecture reference
 * manual.
 */
enum Number
{
    // General-purpose registers by number
    R0  =  0,  R1  =  1,  R2  =  2,  R3  =  3,
    R4  =  4,  R5  =  5,  R6  =  6,  R7  =  7,
    R8  =  8,  R9  =  9,  R10 = 10,  R11 = 11,
    R12 = 12,  R13 = 13,  R14 = 14,  R15 = 15,

    // General-purpose registers that have distinguished names
    SP = R13,  // Stack Pointer - alias of MSP or PSP, below, depending on state
    LR = R14,  // Link Register
    PC = R15,  // Program Counter

    // Special-purpose registers
    xPSR              = 16,  // Union of various Processor Status Registers
    MSP               = 17,  // Main Stack Pointer (used by interrupts/kernels)
    PSP               = 18,  // Process Stack Pointer (used by applications)
    _unused19         = 19,  // Index 19 is currently unused.
    CONTROL_and_masks = 20,  // CONTROL, PRIMASK, and friends (packed bitfield)

    /*
     * When the floating-point unit is present on ARMv7-M, there are some
     * higher register numbers defined -- but we ignore the FPU.  So:
     */
    highest_register_index = CONTROL_and_masks,
};

/*
 * Because the register index sequence contains gaps, this predicate helps to
 * determine whether an integer can be safely cast to a Register::Number.
 */
inline bool is_index_valid(unsigned n)
{
    return n <= highest_register_index && n != _unused19;
}

}  // namespace ARM::Register

}  // namespace ARM

#endif  // ARM_H
