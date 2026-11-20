// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/** What the USB frame counter shows about the host. See host_presence.c. */
enum k100_host_presence {
    /** No frame since power-up, within the time the bus may take to come up. */
    K100_HOST_NOT_SEEN,
    /** Frames are arriving. */
    K100_HOST_SENDING,
    /** The frames stopped, or none arrived in the time the bus may take to come up. */
    K100_HOST_GONE,
};

/** @brief Reads the frame counter. Once per trip round the main loop, which hands the result on. */
enum k100_host_presence k100_host_presence_sample(void);
