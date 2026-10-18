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
 * Memory setup for an LPC546xx application living behind a bootloader that
 * occupies the first 64 KB of flash, which is how the K100 is arranged: its
 * stock application is linked at 0x00010000, established in
 * docs/pin-mapping.md by matching string pointers against file offsets.
 *
 * Getting this wrong is not a soft failure. An image linked at zero and written
 * to the application slot has a reset vector pointing into the bootloader's own
 * code, so the bootloader jumps there instead of into the application, and the
 * working firmware has already been overwritten by then. That is a brick with
 * no upside, which is why this file exists separately rather than as an option.
 *
 * The 1 KB between 0x00010000 and 0x00010400 is left out of this region on
 * purpose. Corsair's container header occupies the first 0x2c bytes of the
 * application slot and is written to flash with the image, and a Cortex-M
 * vector table of eighty entries has to be 1 KB aligned, so the first address
 * the code can start at is 0x00010400. tools/pack-firmware.py pads the header
 * out to meet it, taking the offset from the ELF rather than assuming it.
 *
 * ChibiOS's crt0 sets VTOR from the vectors table's own address, so the vector
 * table is found correctly once the application runs; what has to be right for
 * it to run at all is the link address and the entry point in the header.
 *
 * That is verified in the linked image rather than assumed, because getting it
 * wrong would send every interrupt into the bootloader's handlers: CRT0_VTOR_INIT
 * defaults to TRUE, and the third thing crt0 does is store _vectors into
 * SCB->VTOR. In a build linked by this script those literals read 0x00010400 and
 * 0xE000ED08.
 *
 * ram0 is the main SRAM: three contiguous 32 KB banks plus one of 64 KB, which
 * the bus matrix presents as one 160 KB region.
 *
 * ram1 is SRAMX, tightly coupled to the core. It is deliberately left out of
 * every REGION_ALIAS below. SRAMX is fast for code and core data but it is not
 * reachable by every bus master, so ordinary static data must not live here --
 * moving .bss into it would break a DMA or controller access silently rather
 * than loudly.
 *
 * Being unaliased has a second use. Nothing is linked here, so crt0 -- which
 * zeroes the whole of ram0 -- leaves it alone, and it is the one place whose
 * contents survive a reset. On a part with a watchdog that cannot be turned off
 * and no debugger, that is how a boot reports what the previous boot was doing
 * when it died; see K100_USB_DIAGNOSTIC in the keyboard's bootloader.c.
 *
 * ram2 is USB1's dedicated 8 KB, at the fixed window this part's high-speed
 * device controller requires: USBHSD takes only the top bits of EPLISTSTART, so
 * its endpoint list cannot live anywhere else. That is the controller the K100
 * is wired to and the one this port drives.
 */

MEMORY {
    flash0  : org = 0x00010400, len = 447k
    flash1  : org = 0x00000000, len = 0
    flash2  : org = 0x00000000, len = 0
    flash3  : org = 0x00000000, len = 0
    flash4  : org = 0x00000000, len = 0
    flash5  : org = 0x00000000, len = 0
    flash6  : org = 0x00000000, len = 0
    flash7  : org = 0x00000000, len = 0
    ram0    : org = 0x20000000, len = 160k
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
 * Boot ROM image checksum. The ROM adds the first eight vectors and refuses to
 * start the image unless the sum is zero, so this slot carries the complement
 * of the other seven. Get it wrong and the part does not boot, with no output
 * of any kind to say why.
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
