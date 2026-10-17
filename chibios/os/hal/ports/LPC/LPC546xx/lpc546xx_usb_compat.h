/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors
    SPDX-License-Identifier: Apache-2.0

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    LPC546xx/lpc546xx_usb_compat.h
 * @brief   LPC11Uxx USB register names, expressed in LPC546xx terms.
 *
 * @details The shared @p LLD/USB driver targets the NXP USB device IP — an
 *          endpoint command/status list in SRAM, @p DEVCMDSTAT, and five
 *          logical endpoints — which this part's USBHSD is also an instance of.
 *          Nine of the ten registers it touches exist here under the same
 *          names; only the bit macros differ, because NXP's newer headers
 *          suffix them with @p _MASK and name the interrupt bits per endpoint.
 *
 *          Mapping them here rather than forking the driver keeps the shared
 *          driver unmodified and upstreamable, and confines the difference
 *          between the two families to one file.
 *
 *          Every LPC546xx macro referenced below is compile-checked by
 *          @p chibios/test/register-check.c.
 *
 * @addtogroup HAL
 * @{
 */

#ifndef LPC546XX_USB_COMPAT_H
#define LPC546XX_USB_COMPAT_H

/**
 * @brief   The driver calls the peripheral @p LPC_USB; here it is @p USBHSD.
 *
 * @note    Not @p USB0. This part has both, they are separate peripherals, and
 *          the K100 is wired to the high-speed one -- see @p lpc_registry.h.
 *          Their register offsets are identical for everything this driver
 *          touches, which is what makes the rest of this file a rename.
 */
#define LPC_USB                     USBHSD

/*
 * NXP's header already defines a function-like setter of the same bare name for
 * each field, as in USB_DEVCMDSTAT_DEV_EN(x), which shifts a value into place.
 * Those are the full-speed controller's, and this file defines the same bare
 * names as plain bit masks for the driver, so the setters are undefined first.
 * The USBHSD_ spellings are left alone: nothing here defines those. Nothing else in a USB translation unit wants them, and leaving both
 * definitions in scope is a redefinition error rather than a silent hazard.
 */
#undef USB_DEVCMDSTAT_DEV_EN
#undef USB_DEVCMDSTAT_SETUP
#undef USB_DEVCMDSTAT_DCON
#undef USB_DEVCMDSTAT_DRES_C
#undef USB_DEVCMDSTAT_DSUS_C
#undef USB_DEVCMDSTAT_INTONNAK_CO

/**
 * @name    DEVCMDSTAT bits
 * @{
 */
#define USB_DEVCMDSTAT_DEVADDR_MASK USBHSD_DEVCMDSTAT_DEV_ADDR_MASK
#define USB_DEVCMDSTAT_DEV_EN       USBHSD_DEVCMDSTAT_DEV_EN_MASK
#define USB_DEVCMDSTAT_SETUP        USBHSD_DEVCMDSTAT_SETUP_MASK
#define USB_DEVCMDSTAT_DCON         USBHSD_DEVCMDSTAT_DCON_MASK
#define USB_DEVCMDSTAT_DRES_C       USBHSD_DEVCMDSTAT_DRES_C_MASK
#define USB_DEVCMDSTAT_DSUS_C       USBHSD_DEVCMDSTAT_DSUS_C_MASK
#define USB_DEVCMDSTAT_INTONNAK_CO  USBHSD_DEVCMDSTAT_INTONNAK_CO_MASK

/**
 * @brief   Device suspend.
 *
 * @details The two families name this bit differently and place it identically.
 *          LPC11Uxx's CMSIS header calls bit 17 @p DSUP; NXP's header for this
 *          part calls bit 17 @p DSUS. Every neighbouring bit agrees as well --
 *          @p DCON at 16, @p LPM_SUS at 19, @p LPM_REWP at 20, @p DCON_C at 24,
 *          @p DSUS_C at 25 and @p DRES_C at 26 -- so this is one register laid
 *          out the same way under two spellings, not two different bits.
 *
 *          Earlier versions of this file flagged the mapping as the one that
 *          was not certain and wanted checking against UM10912. Comparing the
 *          two headers settles it without one.
 */
#define USB_DEVCMDSTAT_DSUP         USBHSD_DEVCMDSTAT_DSUS_MASK
/** @} */

/**
 * @name    INFO bits
 * @{
 */
#define USB_INFO_FRAME_NR_POS       USBHSD_INFO_FRAME_NR_SHIFT
/** @} */

/**
 * @name    Endpoint list and data buffer base registers
 * @{
 */
#define USB_EPLISTSTART_MASK        USBHSD_EPLISTSTART_EP_LIST_PRG_MASK
#define USB_DATABUFSTART_MASK       USBHSD_DATABUFSTART_DA_BUF_MASK
/** @} */

/**
 * @name    INTSTAT and INTEN bits
 * @details This part names one bit per endpoint direction rather than giving a
 *          single endpoint macro, so the driver's shifted forms are rebuilt
 *          from @p EP0OUT, which is bit 0. The ten endpoint bits occupy
 *          positions 0 to 9, two per logical endpoint.
 * @{
 */
#define USB_INT_DEV_INT             USBHSD_INTSTAT_DEV_INT_MASK
#define USB_INT_FRAME_INT           USBHSD_INTSTAT_FRAME_INT_MASK
#define USB_INT_EPn_INT             USBHSD_INTSTAT_EP0OUT_MASK

/**
 * @brief   Interrupt bit for one endpoint direction.
 * @param[in] n     endpoint direction index, 0 to 9
 */
#define USB_INT_EP(n)               (USBHSD_INTSTAT_EP0OUT_MASK << (n))

/**
 * @brief   All endpoint interrupt bits.
 */
#define USB_INT_EP_ALL_INT          (USBHSD_INTSTAT_EP0OUT_MASK |              \
                                     USBHSD_INTSTAT_EP0IN_MASK  |              \
                                     USBHSD_INTSTAT_EP1OUT_MASK |              \
                                     USBHSD_INTSTAT_EP1IN_MASK  |              \
                                     USBHSD_INTSTAT_EP2OUT_MASK |              \
                                     USBHSD_INTSTAT_EP2IN_MASK  |              \
                                     USBHSD_INTSTAT_EP3OUT_MASK |              \
                                     USBHSD_INTSTAT_EP3IN_MASK  |              \
                                     USBHSD_INTSTAT_EP4OUT_MASK |              \
                                     USBHSD_INTSTAT_EP4IN_MASK)
/** @} */

/*
 * INTROUTING has no counterpart. On LPC11Uxx it selects whether USB interrupts
 * reach the main handler or the frame-toggle one; this part's USBHSD has no such
 * register, and its absence from NXP's USB_Type makes a reference to it a
 * compile error. The driver writes it once, as zero, in usb_lld_reset(), which
 * is the no-op case, so a capability flag is enough: LPC_USB_HAS_INTROUTING in
 * lpc_registry.h, guarding that one write.
 *
 * This was the only change the shared driver appeared to need, on a survey of
 * the registers it touches. Reading its initialisation code afterwards showed
 * otherwise: it also hardcodes LPC11Uxx's clocking, transceiver power, endpoint
 * RAM address and interrupt number. Those are answered by lpc546xx_usb.c
 * through a seam added to the shared driver; see chibios/patches/README.md.
 */

#endif /* LPC546XX_USB_COMPAT_H */

/** @} */
