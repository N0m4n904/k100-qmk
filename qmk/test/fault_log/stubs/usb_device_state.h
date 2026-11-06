// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

typedef enum {
    USB_DEVICE_STATE_NO_INIT    = 0,
    USB_DEVICE_STATE_INIT       = 1,
    USB_DEVICE_STATE_CONFIGURED = 2,
    USB_DEVICE_STATE_SUSPEND    = 3,
} usb_configure_state_t;

struct usb_device_state {
    uint8_t               idle_rate;
    uint8_t               leds;
    int                   protocol;
    usb_configure_state_t configure_state;
};

void notify_usb_device_state_change_kb(struct usb_device_state usb_device_state);
void notify_usb_device_state_change_user(struct usb_device_state usb_device_state);
