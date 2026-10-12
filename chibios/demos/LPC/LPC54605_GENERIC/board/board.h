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

#ifndef BOARD_H
#define BOARD_H

/*
 * Setup for a bare LPC54605J512.
 *
 * Deliberately knows nothing about any particular board: it exists so the port
 * can be built and linked, and names no pin that is not on the die. A real
 * board definition belongs beside the project that uses it.
 */
#define BOARD_LPC54605_GENERIC
#define BOARD_NAME                  "Generic LPC54605J512"

/*
 * The part macros cannot be defined here, however natural it looks. cmparams.h
 * includes NXP's fsl_device_registers.h and is itself reached from crt1.c and
 * from vectors_lpc.S, neither of which includes a board header -- so by the
 * time this file is read, the decision has already been made. They belong on
 * the command line; see PARTDEFS in the Makefile. Checking for them here turns
 * a missing one into a legible error rather than "No valid CPU defined!".
 */
#if !defined(CPU_LPC54605J512ET100)
#error "define CPU_LPC54605J512ET100 on the command line, for NXP's headers"
#endif
#if !defined(LPC54605)
#error "define LPC54605 on the command line, for lpc_registry.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
  void boardInit(void);
#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
