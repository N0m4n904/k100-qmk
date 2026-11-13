// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/*
 * What the USB driver and ChibiOS's USB layer need from the HAL, on the host. The device headers
 * and the family's USB headers are the real ones; the kernel is a single thread with no lock.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FALSE 0
#define TRUE 1

#include "fsl_device_registers.h"
#include "lpc_registry.h"
#include "lpc546xx_usb_compat.h"

#define HAL_USE_USB TRUE
#define USB_USE_WAIT FALSE
#define LPC_USB_USE_USB1 TRUE
#define LPC_USB_USB1_IRQ_PRIORITY 3

#include "lpc546xx_usb.h"

typedef uint32_t syssts_t;
typedef int32_t  msg_t;
typedef void    *thread_reference_t;

#define MSG_OK ((msg_t)0)
#define HAL_RET_SUCCESS MSG_OK

#define osalDbgAssert(condition, remark) assert(condition)
#define osalDbgCheck(condition) assert(condition)
#define osalDbgCheckClassI()
#define osalSysLock()
#define osalSysUnlock()
#define osalSysLockFromISR()
#define osalSysUnlockFromISR()
#define osalSysGetStatusAndLockX() ((syssts_t)0)
#define osalSysRestoreStatusX(status) ((void)(status))
#define osalOsRescheduleS()

#define OSAL_IRQ_HANDLER(id) void id(void)
#define OSAL_IRQ_PROLOGUE()
#define OSAL_IRQ_EPILOGUE()

void nvicEnableVector(uint32_t vector, uint32_t priority);
void nvicDisableVector(uint32_t vector);

#include <assert.h>

#include "hal_usb.h"
