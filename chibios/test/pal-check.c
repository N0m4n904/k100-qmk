/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Compiles the real LPC546xx PAL header and pins down the pad configuration
 * words it produces.
 *
 * register-check.c can only confirm that the names this port uses exist in
 * NXP's header; it mirrors the port's expressions rather than including it, so
 * a mistake in the port's own macros would survive. The pad encoding is exactly
 * where such a mistake is most likely and least visible: LPC546xx places every
 * IOCON field at a different bit from its LPC11Uxx sibling, so a mode built
 * with the sibling's layout still compiles, still looks right, and quietly
 * configures the wrong thing.
 *
 * So this includes hal_pal_lld.h for real and asserts the exact word each
 * standard PAL mode writes, against the field positions in UM10912:
 *
 *     FUNC     bits 3:0     MODE  bits 5:4     INVERT  bit 7
 *     DIGIMODE bit 8        SLEW  bit 10       OD      bit 11
 *
 * A static assertion failing here means a pad would have been misconfigured on
 * hardware, with no symptom beyond a pin that does not work.
 *
 * Run it with chibios/test/check-registers.sh.
 */

#include <stdbool.h>
#include <stdint.h>

#include "LPC54605.h"

/* The handful of ChibiOS definitions the PAL header reads. Stubbed rather than
   pulled in, so that this stays a check of one header and needs no ChibiOS
   checkout to run. */
#define TRUE                    1
#define FALSE                   0
#define HAL_USE_PAL             TRUE
#define PAL_USE_WAIT            FALSE
#define PAL_USE_CALLBACKS       FALSE
#define PAL_LOW                 0
#define PAL_HIGH                1

#include "lpc_registry.h"
#include "hal_pal_lld.h"

/*
 * Field positions, restated from the user manual rather than taken from NXP's
 * macros. Deriving them from IOCON_PIO_*_SHIFT would make this assert that the
 * header agrees with itself, which is no test at all.
 */
#define UM_FUNC(n)              ((uint32_t)(n) << 0)
#define UM_MODE(n)              ((uint32_t)(n) << 4)
#define UM_INVERT(n)            ((uint32_t)(n) << 7)
#define UM_DIGIMODE(n)          ((uint32_t)(n) << 8)
#define UM_SLEW(n)              ((uint32_t)(n) << 10)
#define UM_OD(n)                ((uint32_t)(n) << 11)
#define UM_DIR_OUT              (1U << 31)

#define UM_PULL_NONE            UM_MODE(0U)
#define UM_PULL_DOWN            UM_MODE(1U)
#define UM_PULL_UP              UM_MODE(2U)
#define UM_DIGITAL              UM_DIGIMODE(1U)

/* The pull-mode field. Values are the same four as on LPC11Uxx; the shift is
   not, which is the trap this whole file exists for. */
_Static_assert(MODE_MODE_INACTIVE == UM_PULL_NONE, "pull-none encoding");
_Static_assert(MODE_MODE_PULL_DOWN == UM_PULL_DOWN, "pull-down encoding");
_Static_assert(MODE_MODE_PULL_UP == UM_PULL_UP, "pull-up encoding");
_Static_assert(MODE_MODE_REPEATER == UM_MODE(3U), "repeater encoding");

/* Four bits of function select, not LPC11Uxx's three: ALT7 must not spill into
   the pull-mode field, and a function above 7 must still fit. */
_Static_assert(MODE_FUNC_DEFAULT == UM_FUNC(0U), "FUNC0 encoding");
_Static_assert(MODE_FUNC_ALT7 == UM_FUNC(7U), "FUNC7 encoding");
_Static_assert(MODE_FUNC(15U) == UM_FUNC(15U), "FUNC15 encoding");
_Static_assert((MODE_FUNC(15U) & MODE_MODE_PULL_UP) == 0U,
               "FUNC must not overlap MODE");

/* The remaining single-bit fields. */
_Static_assert(MODE_AD_ANALOG == UM_DIGIMODE(0U), "analogue encoding");
_Static_assert(MODE_AD_DIGITAL == UM_DIGIMODE(1U), "digital encoding");
_Static_assert(MODE_INV_ENABLE == UM_INVERT(1U), "invert encoding");
_Static_assert(MODE_OD_ENABLE == UM_OD(1U), "open-drain encoding");
_Static_assert(MODE_SLEW_FAST == UM_SLEW(1U), "slew encoding");

/* Direction travels outside the IOCON word, so it must survive the mask that
   splits the two apart. */
_Static_assert((MODE_DIR_OUT & MODE_IOCONF_MASK) == 0U,
               "direction must not land inside the IOCON word");
_Static_assert(MODE_DIR_IN == 0U, "input is the absence of the direction bit");

/* The standard modes, end to end. Each is the whole word the driver writes. */
_Static_assert(PAL_MODE_INPUT == (UM_PULL_NONE | UM_DIGITAL),
               "PAL_MODE_INPUT");
_Static_assert(PAL_MODE_INPUT_PULLUP == (UM_PULL_UP | UM_DIGITAL),
               "PAL_MODE_INPUT_PULLUP");
_Static_assert(PAL_MODE_INPUT_PULLDOWN == (UM_PULL_DOWN | UM_DIGITAL),
               "PAL_MODE_INPUT_PULLDOWN");
_Static_assert(PAL_MODE_INPUT_ANALOG == UM_PULL_NONE,
               "PAL_MODE_INPUT_ANALOG");
_Static_assert(PAL_MODE_OUTPUT_PUSHPULL == (UM_DIR_OUT | UM_DIGITAL),
               "PAL_MODE_OUTPUT_PUSHPULL");
_Static_assert(PAL_MODE_OUTPUT_OPENDRAIN ==
               (UM_DIR_OUT | UM_DIGITAL | UM_OD(1U)),
               "PAL_MODE_OUTPUT_OPENDRAIN");
_Static_assert(PAL_MODE_UNCONNECTED == (UM_PULL_DOWN | UM_DIGITAL),
               "PAL_MODE_UNCONNECTED is pulled down, not left floating");

/*
 * Every digital mode must set DIGIMODE. A pin left analogue reads nothing at
 * all on this part, and the symptom is a dead input rather than an error.
 */
_Static_assert((PAL_MODE_INPUT & UM_DIGITAL) != 0U, "input must be digital");
_Static_assert((PAL_MODE_INPUT_PULLUP & UM_DIGITAL) != 0U,
               "pulled-up input must be digital");
_Static_assert((PAL_MODE_INPUT_PULLDOWN & UM_DIGITAL) != 0U,
               "pulled-down input must be digital");
_Static_assert((PAL_MODE_OUTPUT_PUSHPULL & UM_DIGITAL) != 0U,
               "output must be digital");

/* Port and line packing. All six ports must be present and distinct, and a
   line must come apart into exactly the port and pad it was built from. */
_Static_assert(LPC_GPIO_PORTS == 6, "this family has six GPIO ports");
_Static_assert(IOPORT5 == LPC_IOPORT_ID(5U), "port 5 identifier");
_Static_assert(LPC_IOPORT_NUM(IOPORT5) == 5U, "port number round-trip");
_Static_assert(PAL_PORT(LPC_LINE(3U, 27U)) == IOPORT3, "line to port");
_Static_assert(PAL_PAD(LPC_LINE(3U, 27U)) == 27U, "line to pad");
_Static_assert(LPC_LINE(0U, 0U) != PAL_NOLINE,
               "P0_0 must be a usable line, which is why PAL_NOLINE is not 0");

/*
 * The accessor macros, so that their register and index expressions are type
 * checked and not merely parsed. Reading and writing a volatile is enough to
 * keep them.
 */
void lpc546xx_pal_accessor_check(void);

void lpc546xx_pal_accessor_check(void) {
  ioportid_t port = IOPORT1;
  iopadid_t pad = 18U;

  pal_lld_init();
  pal_lld_setpadmode(port, pad, PAL_MODE_OUTPUT_PUSHPULL);
  pal_lld_setgroupmode(port, 0xFFU, 8U, PAL_MODE_INPUT_PULLUP);

  (void)pal_lld_readport(port);
  (void)pal_lld_readlatch(port);
  pal_lld_writeport(port, PAL_WHOLE_PORT);
  pal_lld_setport(port, 1U << pad);
  pal_lld_clearport(port, 1U << pad);
  pal_lld_toggleport(port, 1U << pad);

  (void)pal_lld_readpad(port, pad);
  pal_lld_writepad(port, pad, PAL_HIGH);
  pal_lld_setpad(port, pad);
  pal_lld_clearpad(port, pad);
}
