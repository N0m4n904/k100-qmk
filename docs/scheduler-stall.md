# The firmware hangs in ChibiOS's reschedule path

This firmware boots, enumerates, binds four HID interfaces and types. Then,
somewhere between two seconds and a minute later, it stops — and Corsair's
bootloader takes the keyboard back.

This is what that is, measured rather than guessed, and what it is not.

## What it looks like from outside

The keyboard disappears from the bus for about a second and returns as
`bcdDevice 0.0a` with Corsair's own product string: the bootloader. It stays
there. Nothing has been lost — the application is still in flash, and
`k100-bragi exit-bootloader` starts it again, for another few seconds.

**The bootloader types.** That is the trap this cost the most time to. A sweep
of the whole keyboard was collected, 104 distinct keys, from a keyboard whose
custom firmware had been gone for minutes. Keys arriving prove only that
*something* is scanning the matrix. See `docs/recovery.md`.

Read against `bcdDevice`, though, this firmware does deliver keystrokes: 67
presses across 16 distinct keys, on four bound input nodes, with the device
reading `0.01` and `K100 RGB (OPX, ISO DE)` throughout. The matrix, the HID
reports and the USB path all work. It is only that they do not keep working.

## What sets it off

Not time. Started by a hardware reset and left alone, it ran **181 seconds**
feeding its watchdog without a missed pass, and a separate 60 s window with the
counter sampled throughout showed the same. It stopped within a second of
someone beginning to type.

The hangs before that one all followed `exit-bootloader`, whose client then
polls the keyboard for twenty seconds before giving up — which is also traffic.
So the thing the two have in common is **endpoint traffic**, not elapsed time,
and that fits where it stops: an endpoint completion is exactly what wakes a
thread from the USB interrupt handler and so asks for the reschedule that never
finishes.

That is a lead rather than a result. It has not been tested by typing on
purpose with the catcher attached, which is the next thing to do and costs one
run.

## What it is not

Each of these was tested on the part, not reasoned about.

| Hypothesis | How it was refuted |
| --- | --- |
| A fault — hard, bus, usage or memory | `CFSR` and `HFSR` both read zero at the moment of the hang, and a session that halts the core on every fault class ran a 90 s window without one. |
| The firmware's own ten-second USB timeout | Read out of the running firmware: `usb_device_state.configure_state` is `CONFIGURED`, `usb_was_ready` is 1, `starve_watchdog` is 0 and `first_task_at` is 0, so that path latched itself off and never started counting. |
| The host suspending the device | `power/runtime_status` stays `active` across a whole cycle and the kernel log records no suspend, only the disconnect at the end. `USBD1.state` reads `USB_ACTIVE`, not `USB_SUSPENDED`. |
| The watchdog feed not working | `WWDT->TV` sits one reload below `WWDT->TC` while the firmware runs, so the feed reaches the register every pass of the main loop. |
| A windowed-watchdog violation from feeding too early | With `TC` raised to its maximum the part survives indefinitely, which a window violation would not. |
| The debugger causing it | The catcher never halts the part to set up. Stretching the watchdog and arming the vector catch are memory writes, which the debug port performs without the core's help. |

## What it is

The core is stopped at **`__port_exit_from_isr + 2`**, which in this build is:

```
00010692 <__port_exit_from_isr>:
   10692:	df00      	svc	0
   10694:	e7fe      	b.n	10694 <__port_exit_from_isr+0x2>
```

That branch is an infinite loop, and it is reached only when the `svc 0` in
front of it does not resume somewhere else. ChibiOS leaves an interrupt that
requires a reschedule by returning to thread mode on a fake exception frame
whose program counter points here; the `svc 0` then enters `SVC_Handler`, which
discards that frame and lets the exception return land in the context that is
to run next. Arriving at the instruction *after* the `svc` means the return
came back to where it started.

The registers at that point:

```
pc        0x00010694     the infinite loop
lr        0x55555555     ChibiOS's fill pattern for unused thread stack
sp        0x20000980     process stack
xpsr      0x01000000     thread mode, no exception active
basepri   0x00           nothing masked
primask   0x00           interrupts enabled
control   0x06           process stack in use, FPCA set
CFSR      0x00000000
HFSR      0x00000000
```

Nothing is masked and nothing has faulted, so the exception was taken and
returned; it returned to the wrong place. The `lr` fill pattern confirms the
frame is one of ChibiOS's fake ones rather than a real saved context.

Once here the thread never leaves, interrupts keep being serviced, the main loop
never runs again, and the watchdog Corsair's bootloader handed over — about
1.6 s — resets the part. The bootloader then keeps control, which is why the
keyboard comes back as `0.0a`.

Ruled out as the mechanism behind the wrong return:

- **The vector table.** `VTOR` is `0x00010400` and the `SVCall` entry at
  `0x0001042c` is `0x00013cc5`, the real handler, not the unhandled stub.
- **The FPU settings the frame size depends on.** `CPACR` is `0x00f00000` and
  `FPCCR` is `0xc0000018`: `ASPEN` and `LSPEN` set, which is ChibiOS's own
  default, and the port is built for the FPU — `__port_switch` pushes
  `s16-s31` and the epilogue reserves the 104-byte extended frame.
- **Interrupt priorities.** `port_init` programs `SVCall` at `0x40` and the
  kernel mask at `0x60`, and the only driver priority this port sets,
  `LPC_USB_USB1_IRQ_PRIORITY`, is 3, which is that same `0x60`.
- **Stack exhaustion at that moment.** The process stack pointer is over a
  kilobyte clear of the main stack's end at `0x20000400`.

## Reproducing it

`swd/catch-failure.sh` is the instrument. It stretches the inherited watchdog
so the evidence is not erased, arms the core to halt on any fault, and then
polls the watchdog counter: firmware that is running feeds it every pass, so a
main loop that stops reaching the feed shows up as that counter falling.

```sh
k100-bragi exit-bootloader
ssh <programmer> sudo swd/catch-failure.sh --seconds 150
```

It halts the part in the act and prints the registers above, and says how long
it took. An idle keyboard can run the whole window; type during it.

## Where to look next

The question is narrow: why does the exception return from `SVC_Handler` come
back to the instruction after the `svc` instead of to the context ChibiOS
arranged. Reading the process stack around `sp` at the moment of the hang, and
comparing the frame there against the 104 bytes the handler skips, answers it
directly and has not been done yet.
