# The firmware hung in ChibiOS's reschedule path

For a while this firmware booted, enumerated, typed — and then stopped, within a
second of anyone using it, after which Corsair's bootloader had the keyboard
back. This is what it was, measured rather than guessed, and what it was not.

## The cause

**ChibiOS-Contrib's LPC system timer gives SysTick a priority that is not a
priority on this part.** Its header carried a literal:

```c
#define LPC_ST_IRQ_PRIORITY    8
```

The port turns a priority *level* into the hardware field by shifting it left by
`8 - CORTEX_PRIORITY_BITS`. This part has **three** priority bits, so it has
eight levels, 0 to 7, and the shift is by five:

```
8 << 5  =  0x100  →  truncated to a byte  →  0x00
```

A literal 8 is a level only where there are four priority bits or more. With
three it leaves the byte entirely and arrives as zero — not the weakest
priority but the strongest. Read off the part, SysTick sat above everything the
kernel uses:

| Handler | Priority |
| --- | --- |
| **SysTick** | **0x00** |
| SVCall | 0x40 |
| PendSV | 0x60 |
| USB | 0x60 |
| the kernel's `BASEPRI` | 0x60 |

What that costs is not a slow tick. `BASEPRI` at `0x60` does not mask priority
zero, so the tick handler ran *inside* the kernel's critical regions — and the
interrupt epilogue depends on not being interrupted. It hands an interrupt back
to thread level on an artificial exception frame whose only job is to reach a
`svc`, and ChibiOS says so in as many words:

```c
    /* Note, returning without unlocking is intentional, this is done in
       order to keep the rest of the context switch atomic.*/
```

A tick landing in that stretch leaves the stack pointer where the `SVC_Handler`
does not expect it, and the handler's fixed adjustment then lands on a stale
frame instead of the one the thread came from.

`0011-systick-priority-is-a-level.patch` makes the default
`CORTEX_MINIMUM_PRIORITY`, which is a level on any part, and adds the check that
was missing:

```c
#if !OSAL_IRQ_IS_VALID_PRIORITY(LPC_ST_IRQ_PRIORITY)
  #error "Invalid IRQ priority assigned to LPC_ST_IRQ_PRIORITY"
#endif
```

**Measured after the fix: 240 seconds of continuous typing, 5204 key events,
`bcdDevice` reading `0.01` at the start and at the end.** Before it, typing
stopped the firmware within a second.

## What it looked like from outside

The keyboard disappeared from the bus for about a second and returned as
`bcdDevice 0.0a` with Corsair's own product string: the bootloader. It stayed
there. Nothing was lost — the application was still in flash, and
`k100-bragi exit-bootloader` started it again, for another few seconds.

**The bootloader types.** That is the trap this cost the most time to. A sweep
of the whole keyboard was collected, 104 distinct keys, from a keyboard whose
custom firmware had been gone for minutes. Keys arriving prove only that
*something* is scanning the matrix. See `docs/recovery.md`.

## What set it off

Not time. Started by a hardware reset and left alone, the firmware ran **181
seconds** feeding its watchdog without a missed pass. It stopped within a second
of someone beginning to type.

The hangs before that one all followed `exit-bootloader`, whose client then
polls the keyboard for twenty seconds before giving up — which is also traffic.
What the two have in common is **endpoint traffic**, and traffic means context
switches: every endpoint completion wakes a thread from the USB interrupt
handler, and every one of those is another pass through the stretch of code a
tick must not interrupt. More traffic, more chances, sooner.

## Where it stopped

At **`__port_exit_from_isr + 2`**, which in that build was:

```
00010692 <__port_exit_from_isr>:
   10692:	df00      	svc	0
   10694:	e7fe      	b.n	10694 <__port_exit_from_isr+0x2>
```

That branch is an infinite loop, reached only when the `svc` in front of it does
not resume somewhere else. The registers at a breakpoint on it, which catches
the first arrival rather than the settled state:

```
pc        0x00010694     the infinite loop
lr        0x20000cb8
sp        0x20000b20     process stack
xpsr      0x01000000     thread mode, no exception active
basepri   0x00           nothing masked
primask   0x00           interrupts enabled
control   0x06           process stack in use, FPCA set
CFSR      0x00000000
HFSR      0x00000000
```

The stack said the rest. At `sp` lay a sound exception frame — `pc`
`0x000149b8`, inside `port_rt_external_counter_value`, with `lr` returning into
`chSysPolledDelayX`, which is where the main thread spends most of its time
because the matrix scan waits 500 us per row. The thread's real context was
sitting right there, and the thread could not reach it.

`SVC_Handler` advances the stack pointer by a fixed 104 bytes, the size of an
exception frame carrying FPU state. For its result to be `0x20000b20` and the
return to land on `0x10694`, the stack pointer at the `svc` must have been 104
bytes **below** the frame it should have returned into — pointing at a spent
frame from an earlier round trip, whose stored return address was the `svc`'s
own. From there the thread never leaves: interrupts keep being serviced, the
main loop never runs again, and the watchdog Corsair's bootloader handed over —
about 1.6 s — resets the part. The bootloader then keeps control, which is why
the keyboard came back as `0.0a`.

## What it was not

Each of these was tested on the part, not reasoned about. They are kept because
the cause was none of them, and because a reader with a similar symptom will
reach for the same ones.

| Hypothesis | How it was refuted |
| --- | --- |
| A fault — hard, bus, usage or memory | `CFSR` and `HFSR` both zero at the moment of the hang, and a session halting the core on every fault class ran a 90 s window without one. |
| The firmware's own ten-second USB timeout | Read out of the running firmware: `usb_device_state.configure_state` is `CONFIGURED`, `usb_was_ready` is 1, `starve_watchdog` is 0 and `first_task_at` is 0, so that path latched itself off and never started counting. |
| The host suspending the device | `power/runtime_status` stays `active` across a whole cycle and the kernel log records no suspend, only the disconnect at the end. `USBD1.state` reads `USB_ACTIVE`. |
| The watchdog feed not working | `WWDT->TV` sits one reload below `WWDT->TC` while the firmware runs. |
| A windowed-watchdog violation from feeding too early | `WWDT->WINDOW` is `0x00ffffff`, its maximum, so no feed can be early. |
| The vector table | `VTOR` is `0x00010400` and the `SVCall` entry at `0x0001042c` is `0x00013cc5`, the real handler, not the unhandled stub. |
| The FPU settings the frame size depends on | `CPACR` is `0x00f00000` and `FPCCR` is `0xc0000018`: `ASPEN` and `LSPEN` set, which is ChibiOS's own default. |
| Stack exhaustion | The process stack runs from `0x20000400` to `0x20000c00` and the pointer was over a kilobyte clear of the end. |
| The debugger causing it | The catcher never halts the core to set itself up. |

## A trap in measuring it

A run that looked like a stall was a reset: the part had already restarted, and
the registers read out described Corsair's bootloader — `VTOR` zero, a stack at
`0x20028000`, a program counter nowhere near this image. The watchdog counter
had fallen below the threshold not because a loop stopped feeding it but because
the constant it reloads from went back to the bootloader's on reset.

`catch-failure.sh` re-reads that constant when it triggers. A constant that is
no longer the one it set means the part reset, and it says so instead of
printing registers that belong to something else.

Stretching that constant turned out to be the wrong default for this
investigation anyway. The inherited timeout is 1.6 s and a stall is detectable
after a few hundred milliseconds, so there is over a second to halt the core
without touching the watchdog at all — `--no-stretch`.

## Reproducing it

`swd/catch-failure.sh` is the instrument, and the breakpoint is what makes it
exact: `0x10694` is only ever executed once the context switch has already
failed, so stopping there catches the first arrival with the frames that caused
it still in place.

```sh
ssh <programmer> sudo swd/catch-failure.sh --seconds 150 --no-stretch \
    --break 0x00010694
```

Type during the window. An idle keyboard can run it out.
