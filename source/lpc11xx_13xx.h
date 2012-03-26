#ifndef LPC11XX_13XX_H
#define LPC11XX_13XX_H

/*
 * Definitions common to the NXP LPC11xx and 13xx series.
 */

#include "arm.h"

#include <stdint.h>
#include <stddef.h>


namespace LPC11xx_13xx
{

/*******************************************************************************
 * In-Application-Programming ROM information
 */
namespace IAP
{
    /*
     * ROM entry point.  Notice that this is an actual code pointer -- *not*
     * a Thumb-style address with bit 0 set.
     */
    static ARM::word_t const entry = 0x1FFF1FF0;

    static size_t const min_stack_bytes = 128;
    static size_t const min_stack_words = min_stack_bytes / sizeof(ARM::word_t);

    static size_t const max_command_words = 5;
    static size_t const max_response_words = 5;

    // We reuse the command table to hold the response; this gives its size.
    static size_t const max_command_response_words = 5;

    namespace Command {
        enum Index {
            unprotect_sectors      = 50,
            copy_ram_to_flash      = 51,
            erase_sectors          = 52,
            blank_check_sectors    = 53,
            read_part_id           = 54,
            read_boot_code_version = 55,
            compare                = 56,
            reinvoke_isp           = 57,
            read_uid               = 58,
        };
    }

}  // namespace LPC11xx_13xx::IAP

/*******************************************************************************
 * System Configuration (SYSCON) block
 */
namespace SYSCON
{
    /*
     * Determines what memory appears in the first 512 bytes of the address
     * space.  This is one of two ways to change the vector table; the other
     * is the non-proprietary VTOR register.
     */
    static ARM::word_t const SYSMEMREMAP(0x40048000);
    static ARM::word_t const SYSMEMREMAP_MAP_BOOTLOADER = 0 << 0;
    static ARM::word_t const SYSMEMREMAP_MAP_USER_RAM   = 1 << 0;
    static ARM::word_t const SYSMEMREMAP_MAP_USER_FLASH = 2 << 0;

}  // namespace LPC11xx_13xx::SYSCON

}  // namespace LPC11xx_13xx

#endif  // LPC11XX_13XX_H
