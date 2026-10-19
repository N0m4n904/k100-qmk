/*
 * Copyright (C) 2026 k100-qmk contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Memory setup for an image that runs entirely out of SRAM, loaded by a
 * debugger and started by hand. Nothing it produces belongs in flash.
 *
 * Why it exists: finding out why this firmware does not boot used to mean
 * writing it to flash, which erases a sector, leaves something permanent
 * behind, and -- once -- left a keyboard that would not come back. An image
 * loaded into SRAM leaves the part exactly as it was; the next reset runs
 * whatever is in flash again, and the chip has no memory of the attempt. Debug
 * from here, and only put something in flash once it is worth keeping.
 *
 * It is not a substitute for a flash build. The code runs at different
 * addresses, so anything that depends on where it lives -- a literal pool
 * assumption, a region the MPU would treat differently, the bootloader's
 * handover -- behaves differently or not at all. The flash build is still the
 * one that has to work in the end.
 *
 * -- The split, and why it is not one region --
 *
 * flash0 is SRAM too, despite the name; the name is ChibiOS's, and every
 * REGION_ALIAS below that says FLASH means "where code and constants live".
 * Putting code in a region separate from ram0 is not tidiness. crt0 zeroes the
 * whole of ram0 through __init_ram_areas before any C in the HAL runs, so an
 * image whose own code sat inside ram0 would erase itself partway through its
 * own startup. Code therefore goes in flash0 at 0x20000000 and nothing else
 * does, and ram0 starts above it.
 *
 * The boundary at 0x20010000 is a real one: this part's SRAM is four banks,
 * 32 KB at 0x20000000, 32 KB at 0x20008000, 32 KB at 0x20010000 and 64 KB at
 * 0x20018000. flash0 is the first two and ram0 the last two, so neither
 * straddles a bank.
 *
 * -- What the loader has to do first --
 *
 * Only the first bank is clocked out of reset; the other three are not. An
 * image this size does not fit in 32 KB, so whatever loads it must write 0x38
 * to AHBCLKCTRLSET0 at 0x40000220 -- SRAM1, SRAM2 and SRAM3 -- before copying
 * a single byte above 0x20008000. swd/run-from-ram.sh does that. It is the same
 * write __cpu_init makes, for the same reason, and it is write-one-to-set so
 * doing it twice costs nothing.
 *
 * ram1 and ram2 are as in the flash scripts: SRAMX left unaliased so that it
 * survives crt0 and can carry diagnostics across a reset, and USB1's dedicated
 * 8 KB at the fixed window USBHSD requires.
 */

MEMORY {
    flash0  : org = 0x20000000, len = 64k
    flash1  : org = 0x00000000, len = 0
    flash2  : org = 0x00000000, len = 0
    flash3  : org = 0x00000000, len = 0
    flash4  : org = 0x00000000, len = 0
    flash5  : org = 0x00000000, len = 0
    flash6  : org = 0x00000000, len = 0
    flash7  : org = 0x00000000, len = 0
    ram0    : org = 0x20010000, len = 96k
    ram1    : org = 0x04000000, len = 32k
    ram2    : org = 0x40100000, len = 8k
    ram3    : org = 0x00000000, len = 0
    ram4    : org = 0x00000000, len = 0
    ram5    : org = 0x00000000, len = 0
    ram6    : org = 0x00000000, len = 0
    ram7    : org = 0x00000000, len = 0
}

/* For each data/text section two region are defined, a virtual region
   and a load region (_LMA suffix).*/

/* Flash region to be used for exception vectors.*/
REGION_ALIAS("VECTORS_FLASH", flash0);
REGION_ALIAS("VECTORS_FLASH_LMA", flash0);

/* Flash region to be used for constructors and destructors.*/
REGION_ALIAS("XTORS_FLASH", flash0);
REGION_ALIAS("XTORS_FLASH_LMA", flash0);

/* Flash region to be used for code text.*/
REGION_ALIAS("TEXT_FLASH", flash0);
REGION_ALIAS("TEXT_FLASH_LMA", flash0);

/* Flash region to be used for read only data.*/
REGION_ALIAS("RODATA_FLASH", flash0);
REGION_ALIAS("RODATA_FLASH_LMA", flash0);

/* Flash region to be used for various.*/
REGION_ALIAS("VARIOUS_FLASH", flash0);
REGION_ALIAS("VARIOUS_FLASH_LMA", flash0);

/* Flash region to be used for RAM(n) initialization data.*/
REGION_ALIAS("RAM_INIT_FLASH_LMA", flash0);

/* RAM region to be used for Main stack. This stack accommodates the processing
   of all exceptions and interrupts.*/
REGION_ALIAS("MAIN_STACK_RAM", ram0);

/* RAM region to be used for the process stack. This is the stack used by
   the main() function.*/
REGION_ALIAS("PROCESS_STACK_RAM", ram0);

/* RAM region to be used for data segment.*/
REGION_ALIAS("DATA_RAM", ram0);
REGION_ALIAS("DATA_RAM_LMA", flash0);

/* RAM region to be used for BSS segment.*/
REGION_ALIAS("BSS_RAM", ram0);

/* RAM region to be used for the default heap.*/
REGION_ALIAS("HEAP_RAM", ram0);

/*
 * Kept so that the vector table's slot at 0x1c is defined and the image checker
 * reads the same thing here as in a flash build. Nothing uses the value: the
 * boot ROM computes this sum over an image it starts itself, and it does not
 * start this one.
 */
Vector1C = (0 -
        ((__main_stack_end__) +
        (Reset_Handler + 1) +
        (NMI_Handler + 1) +
        (HardFault_Handler + 1) +
        (MemManage_Handler + 1) +
        (BusFault_Handler + 1) +
        (UsageFault_Handler + 1))
    );

/* Generic rules inclusion.*/
INCLUDE rules.ld
