// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * The interrupt handler of the shared LPC USB driver on the host, driven through ChibiOS's USB
 * layer. hal_usb_lld.c is included, so that a test can reach the endpoint list and the buffers the
 * controller would use.
 *
 * The controller is memory at the addresses the device header names, and the functions below do
 * between two runs of the handler what the controller would do on the bus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hal_usb_lld.c"

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* What the family supplies around the driver, none of which the handler reaches. */
void lpc_usb_clock_init(void) {}
void lpc_usb_power_on(void) {}
void lpc_usb_power_off(void) {}
void nvicEnableVector(uint32_t vector, uint32_t priority) {
    (void)vector;
    (void)priority;
}
void nvicDisableVector(uint32_t vector) {
    (void)vector;
}

/* The register block and the endpoint memory, at the addresses the driver writes to. */
static uint8_t *controller_first;
static size_t   controller_size;

static void map_controller(void) {
    const uintptr_t page  = (uintptr_t)sysconf(_SC_PAGESIZE);
    const uintptr_t first = (uintptr_t)USBHSD & ~(page - 1U);
    const uintptr_t end   = (LPC_USB_SRAM_END + page - 1U) & ~(page - 1U);

    void *mapped = mmap((void *)first, end - first, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    REQUIRE(mapped == (void *)first);
    controller_first = mapped;
    controller_size  = end - first;
}

/* The controller, one bus event at a time. */

#define EP_OUT_BIT(ep) USB_INT_EP(2U * (ep))
#define EP_IN_BIT(ep) USB_INT_EP(2U * (ep) + 1U)
#define OUT_ENTRY(ep) (4U * (ep))
#define IN_ENTRY(ep) (4U * (ep) + 2U)

static uint32_t entry_bytes(uint32_t entry) {
    return (entry & EPLIST_ENTRY_NBYTES_MASK) >> EPLIST_ENTRY_NBYTES_POS;
}

/*
 * INTSTAT and the latched bits of DEVCMDSTAT clear on a write of one. The handler writes INTSTAT
 * back whole and acknowledges each latched bit it acts on, which memory does not turn into a
 * clear, so the clear happens here.
 */
static void run_handler(void) {
    const uint32_t latched = USBHSD->DEVCMDSTAT & USB_DEVCMDSTAT_LATCHED_MASK;

    VectorFC();

    USBHSD->INTSTAT = 0U;
    USBHSD->DEVCMDSTAT &= ~latched;
}

static void raise_interrupt(uint32_t bits) {
    USBHSD->INTSTAT |= bits;
}

static void reset_bus(void) {
    USBHSD->DEVCMDSTAT |= USB_DEVCMDSTAT_DRES_C;
    raise_interrupt(USB_INT_DEV_INT);
}

static void receive_setup(const uint8_t packet[8]) {
    memcpy(USBD1.setup_buffer, packet, 8);
    USB_EPLIST->entry[OUT_ENTRY(0)] &= ~EPLIST_ENTRY_ACTIVE;
    USB_EPLIST->entry[IN_ENTRY(0)] &= ~EPLIST_ENTRY_ACTIVE;
    USBHSD->DEVCMDSTAT |= USB_DEVCMDSTAT_SETUP;
    raise_interrupt(EP_OUT_BIT(0));
}

/** The host takes the IN packet armed on @p ep. @return its length. */
static uint32_t send_in_packet(usbep_t ep) {
    volatile uint32_t *entry = &USB_EPLIST->entry[IN_ENTRY(ep)];

    REQUIRE((*entry & EPLIST_ENTRY_ACTIVE) != 0U);
    *entry &= ~EPLIST_ENTRY_ACTIVE;
    raise_interrupt(EP_IN_BIT(ep));
    return entry_bytes(*entry);
}

/** The host sends an OUT packet, which an armed entry on @p ep accepts. */
static void receive_out_packet(usbep_t ep, const uint8_t *data, uint32_t length) {
    volatile uint32_t *entry = &USB_EPLIST->entry[OUT_ENTRY(ep)];
    const uint32_t     room  = entry_bytes(*entry);

    REQUIRE((*entry & EPLIST_ENTRY_ACTIVE) != 0U);
    REQUIRE(length <= room);
    if (length > 0U) {
        memcpy(USBD1.epn_buffer[2U * ep], data, length);
    }
    *entry = (*entry & ~(EPLIST_ENTRY_ACTIVE | EPLIST_ENTRY_NBYTES_MASK)) | EPLIST_ENTRY_NBYTES(room - length);
    raise_interrupt(EP_OUT_BIT(ep));
}

/* The device: a descriptor in read-only memory, where a write faults, and an output report. */

static const uint8_t device_descriptor[18] = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x1c, 0x1b,
                                              0x7c, 0x1b, 0x11, 0x01, 1, 2, 0, 1};
static const USBDescriptor device_descriptor_view = {sizeof device_descriptor, device_descriptor};

static uint8_t  output_report[96];
static unsigned output_reports_received;

static const USBDescriptor *get_descriptor(USBDriver *usbp, uint8_t type, uint8_t index, uint16_t language) {
    (void)usbp;
    (void)type;
    (void)index;
    (void)language;
    return &device_descriptor_view;
}

static void count_output_report(USBDriver *usbp) {
    (void)usbp;
    output_reports_received++;
}

enum { SET_REPORT_REQUEST_TYPE = 0x21, SET_REPORT = 0x09 };

static bool handle_set_report(USBDriver *usbp) {
    if (usbp->setup[0] != SET_REPORT_REQUEST_TYPE || usbp->setup[1] != SET_REPORT) {
        return false;
    }
    usbSetupTransfer(usbp, output_report, (size_t)(usbp->setup[6] | usbp->setup[7] << 8), count_output_report);
    return true;
}

static USBInEndpointState in_state_ep1;

static const USBEndpointConfig ep1_config = {
    .ep_mode    = USB_EP_MODE_TYPE_INTR,
    .in_maxsize = 64,
    .in_state   = &in_state_ep1,
};

static void configure_endpoints(USBDriver *usbp, usbevent_t event) {
    if (event == USB_EVENT_CONFIGURED) {
        usbInitEndpointI(usbp, 1, &ep1_config);
    }
}

static const USBConfig config = {
    .event_cb          = configure_endpoints,
    .get_descriptor_cb = get_descriptor,
    .requests_hook_cb  = handle_set_report,
};

static const uint8_t set_configuration[8]     = {0x00, 0x09, 1, 0, 0, 0, 0, 0};
static const uint8_t get_device_descriptor[8] = {0x80, 0x06, 0, 1, 0, 0, sizeof device_descriptor, 0};

/* A driver after power-up and a bus reset, with nothing left from an earlier test. */
static void start_driver(void) {
    memset(controller_first, 0, controller_size);
    memset(output_report, 0, sizeof output_report);
    output_reports_received = 0;

    usb_lld_init();
    REQUIRE(usbStart(&USBD1, &config) == MSG_OK);
    /* The start writes ones to INTSTAT to clear it, as run_handler() says. */
    USBHSD->INTSTAT = 0U;
    reset_bus();
    run_handler();
    REQUIRE(USBD1.state == USB_READY);
}

/* The status stage of a transfer whose data went to the device: one IN packet, empty. */
static void finish_out_transfer(void) {
    REQUIRE(send_in_packet(0) == 0U);
    run_handler();
    REQUIRE(USBD1.ep0state == USB_EP0_STP_WAITING);
}

static void receive_output_report(const uint8_t *data, uint16_t length) {
    const uint8_t setup[8] = {SET_REPORT_REQUEST_TYPE, SET_REPORT, 0, 2, 2, 0,
                              (uint8_t)length, (uint8_t)(length >> 8)};
    const unsigned received_before = output_reports_received;

    receive_setup(setup);
    run_handler();
    const uint32_t packet_size = ep0config.out_maxsize;
    for (uint32_t sent = 0; sent < length; sent += packet_size) {
        const uint32_t remaining = length - sent;
        receive_out_packet(0, data + sent, remaining < packet_size ? remaining : packet_size);
        run_handler();
    }
    finish_out_transfer();

    REQUIRE(output_reports_received == received_before + 1U);
    REQUIRE(memcmp(output_report, data, length) == 0);
}

/*
 * The crash in the field. The entry for EP0 OUT is armed with every EP0 IN packet, so the status
 * packet after the last IN packet is accepted and can arrive in the same interrupt. A completed
 * output report before it leaves the byte count of that entry at zero. Served OUT first, against
 * an OUT state that shared memory with the IN state, that copied 64 bytes into the descriptor.
 */
static void test_status_packet_in_the_same_interrupt_as_the_last_in_packet(void) {
    start_driver();
    const uint8_t two_bytes[2] = {0x01, 0x02};
    receive_output_report(two_bytes, sizeof two_bytes);
    REQUIRE(entry_bytes(USB_EPLIST->entry[OUT_ENTRY(0)]) == 0U);

    receive_setup(get_device_descriptor);
    run_handler();
    REQUIRE(send_in_packet(0) == sizeof device_descriptor);
    receive_out_packet(0, NULL, 0);
    run_handler();

    REQUIRE(USBD1.ep0state == USB_EP0_STP_WAITING);
    REQUIRE(USBD1.receiving == 0U && USBD1.transmitting == 0U);
    REQUIRE(device_descriptor[0] == 18 && device_descriptor[8] == 0x1c);
}

static void test_output_report_over_two_packets(void) {
    start_driver();
    uint8_t data[96];
    for (size_t i = 0; i < sizeof data; i++) {
        data[i] = (uint8_t)(0xa0 + i);
    }
    receive_output_report(data, sizeof data);
}

/* An OUT bit with no receive started belongs to no transfer. */
static void test_out_bit_without_a_started_receive_is_ignored(void) {
    start_driver();
    raise_interrupt(EP_OUT_BIT(0));
    run_handler();

    REQUIRE(USBD1.ep0state == USB_EP0_STP_WAITING);
    REQUIRE(USBD1.receiving == 0U);
}

/*
 * A bus reset in the same interrupt as an IN packet: the reset clears the endpoint the packet
 * belonged to before the endpoint loop runs, and serving the bit would follow its cleared pointer.
 */
static void test_in_bit_latched_before_a_reset(void) {
    start_driver();
    receive_setup(set_configuration);
    run_handler();
    finish_out_transfer();
    REQUIRE(USBD1.state == USB_ACTIVE && USBD1.epc[1] == &ep1_config);

    static const uint8_t key_report[8] = {0, 0, 0x04};
    osalSysLockFromISR();
    usbStartTransmitI(&USBD1, 1, key_report, sizeof key_report);
    osalSysUnlockFromISR();

    send_in_packet(1);
    reset_bus();
    run_handler();

    REQUIRE(USBD1.state == USB_READY);
    REQUIRE(USBD1.epc[1] == NULL && USBD1.transmitting == 0U);
}

#define RUN(test)                                                              \
    do {                                                                       \
        fprintf(stderr, "%s\n", #test);                                        \
        test();                                                                \
    } while (0)

int main(void) {
    map_controller();

    RUN(test_status_packet_in_the_same_interrupt_as_the_last_in_packet);
    RUN(test_output_report_over_two_packets);
    RUN(test_out_bit_without_a_started_receive_is_ignored);
    RUN(test_in_bit_latched_before_a_reset);

    puts("usb interrupt: all checks passed");
    return 0;
}
