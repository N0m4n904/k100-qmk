<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Recovering a K100 with a Raspberry Pi

A Raspberry Pi is a complete SWD programmer. Its GPIOs are 3.3 V, the
LPC54605 is 3.3 V, and the two connect with nothing in between — no probe, no
level shifter, no adapter. This directory is everything the Pi needs.

[`../docs/handover.md`](../docs/handover.md) explains what is wrong with the
keyboard and what to write to it afterwards. This page is only about getting a
wire onto it and reading it out.

## The three rules

1. **Never connect the Pi's 3.3 V or 5 V pin to the keyboard.** The keyboard
   powers itself from USB. Two supplies tied together through a jumper is the
   one wiring mistake here that can damage a board which is currently only
   soft-bricked. Nothing in this directory asks for a supply pin.
2. **Power the keyboard from the Pi's own USB port.** That is not for
   convenience: it gives the two boards a shared ground through the USB cable's
   own shield and return, which SWD requires. Do this and no ground wire is
   needed, which is one less thing to hold against a pad.
3. **Dump before you erase.** Corsair's bootloader exists only on this chip.
   See [`dump-flash.sh`](dump-flash.sh).

## What the Pi needs

```sh
sudo apt install openocd
openocd -c "adapter list" 2>&1 | grep linuxgpiod
```

That second command must print something. The configs here use `linuxgpiod`,
which talks to the kernel's GPIO device rather than mapping `/dev/mem`, so it
does not need to be told the Pi's peripheral base address and cannot be pointed
at the wrong one. Every Pi model works with the same config.

Everything below runs as root, because `/dev/gpiochip0` is root-owned.

## Wiring

On the Pi, four consecutive pins on the header's even row, so a jumper block
lands on them without counting:

| Header pin | GPIO | Signal |
| --- | --- | --- |
| 16 | 23 | nRESET — optional, see below |
| 18 | 24 | SWDIO |
| 20 | — | GND — *not needed if the keyboard is powered from the Pi* |
| 22 | 25 | SWCLK |

On the keyboard, the connection point is **`J1`**: six plated through-holes at
0.1 in pitch, immediately beside the main controller and its crystal, with pin 1
marked both by a square pad and by a silkscreen triangle. Being plated holes
rather than flat pads, **this very likely needs no soldering** — a jumper wire
pushed into a hole and taped down makes a good enough contact for a read.

Four test points, `TP42`–`TP45`, sit directly beneath pins 2 to 5 — the shape
of a header carrying four signals between a supply and a ground. Which signal
is on which pin is **not known**, and is not guessed at here; step 1 measures
what can be measured and step 2 finds the rest by trying it.

nRESET is worth having if one of the six pins turns out to be it: this board's
firmware faults and is restarted by its watchdog every few seconds, and holding
it in reset makes the chip sit still. It is not required — reading flash goes
through the debug port's own bus bridge and does not need the core stopped, so a
dump works straight through the loop.

## Step 1a — survey the header

```sh
sudo swd/identify-header.sh 24
```

Probe wire from Pi GPIO24 to `J1` pin 1, run it, write the verdict down, move
the wire to pin 2, run it again. Six runs.

Each pin is read twice, once with the Pi's internal pull-up applied and once
with its pull-down. Reading is passive and cannot hurt anything.

**Measured on the bricked unit:** pins 1 to 5 held high, pin 6 held low.

That is enough to place the two outer pins. A six-pin debug header carries its
supply and its ground on the outside, and the board agrees: pin 6 is at ground,
and pin 1 is the only one of the high five with no test point beneath it, while
`TP42`–`TP45` sit under pins 2 to 5. Test points are put on signals; nobody
needs a test point for a rail they can reach anywhere on the board.

**So pins 1 and 6 are the supply and the ground, and neither is ever driven.**
Driving a supply rail low is a short across the keyboard's regulator, and it is
the Pi's own GPIO that would most likely die of it. Only pins 2 to 5 go any
further.

## Step 1b — check the four signals are not driven

A single reading cannot tell a pull-up resistor from a chip output holding the
same level: against the Pi's ~50 kΩ pull-down they look identical. One is
entirely safe to drive and the other is the case worth avoiding, so "held high"
on its own decides nothing.

The keyboard's own fault settles it. The watchdog restarts the part every
eleven seconds or so, and **a chip in reset has every pin high-impedance**. An
output therefore cannot hold a level through a restart; a resistor soldered to
the board does not care that one happened.

```sh
sudo swd/identify-header.sh --watch 45 24
```

Forty-five seconds covers four restarts. Run it on pins 2, 3, 4 and 5.

- **never changes** → static, so it is a pull-up and not the chip. Safe.
- **changes at any point** → the chip drives that pin. Leave it out of step 2.

That is the whole safety argument for what follows: driving a pin the keyboard
is itself driving puts two outputs in opposition, and this finds them before
rather than after.

## Step 2 — find SWDIO and SWCLK

```sh
sudo swd/find-swd.sh 24 25 23 22
```

Wire the pins that came back **safe** to those GPIOs, in any order, and this
tries every ordered pair until one answers. A listening debug port replies to
the line-reset sequence with its ID code and nothing else on a board does, so
one success identifies the pair outright. A wrong pair reads back all-ones and
OpenOCD gives up in well under a second.

If nothing answers at all, the order to check is: keyboard actually powered,
then ground actually shared, then whether these are the right six pins — and
only last, whether debug access is disabled on the part. That last one is the
only possibility a wire cannot fix.

## Step 3 — dump the flash

```sh
sudo swd/dump-flash.sh
```

The pins found in step 2 on a K100 are the defaults, so this takes no arguments
on that board. `--swclk` and `--swdio` override them anywhere else.

Reads all 512 KB twice and refuses to produce a dump unless the two passes
agree, because a lead that moved mid-read gives a file that looks fine and is
not. It then checks the result is not a single repeated byte, which is what a
read that never reached the flash looks like.

Out come `k100-flash-512k.bin` and `k100-bootloader-64k.bin`. **Copy both off
the Pi.**

## Step 4 — check what the first 64 KB actually are

The dump is taken with the core halted straight out of reset, and at that point
`0x00000000` may not be showing the flash at all. On the unit dumped here the
vector table read back from `0x00000000` was:

```
SP     0x20003fe0
reset  0x03000137
rest   0x03000b4d, 0x03000b4f, 0x03000135, ...
```

Every one of those handlers is in NXP's boot ROM at `0x03000000`, and they match
where OpenOCD reported the core halted: `pc 0x03000136`, `msp 0x20003fe0`. That
is what a part whose ROM is still mapped low looks like, which would mean the
first 64 KB of the dump are the ROM and **Corsair's bootloader has not been read
at all** — precisely the part that exists nowhere else.

Settle it rather than assume it:

```sh
sudo swd/read-memory.sh 0x03000000 0x10000 rom-64k.bin
cmp rom-64k.bin k100-bootloader-64k.bin
```

**On the unit dumped here the two differ from byte 513 on** — that is, they are
identical for exactly `0x200` bytes and unrelated after. So the shadowing covers
the vector table and nothing more: `0x00000200`–`0x0000FFFF` is genuinely
Corsair's bootloader, and only its first `0x200` bytes are missing.

What is past `0x200` is not in doubt. It holds Corsair's own symbol names and
NXP's SDK built with a Windows toolchain:

```
bsp\SPIHal.c   is31fl3743b.c   keyboard.c   ledDriver.c   ledInfo.c
opticalMatrixMask.c   keyToHidCodeMap.c   dataStorage.c
..\lib\sdk\drivers\fsl_eeprom.c   ..\lib\sdk\drivers\fsl_wwdt.c
```

Nor is the application region: at `0x00010000` the dump holds Corsair's
container header, with length, entry address, USB product ID and build
timestamp all matching the image last written to the part.

## Step 5 — read the vector table the ROM was covering

`--halt-late` resets the part, lets it run long enough for the ROM to hand over,
and halts it after, so the bottom of the address space is the flash's by then.

```sh
sudo swd/read-memory.sh --halt-late 0x00000000 0x10000 boot0-late.bin
cmp boot0-late.bin k100-bootloader-64k.bin
```

The watchdog is running again at that point, so the part is only still until it
is reset once more. 64 KB takes about two seconds and fits; if two passes will
not agree, ask for less — `0x1000` is more than enough, since only `0x200` is in
question.

Expect a difference confined to the first `0x200` bytes. Anything else means
the part moved under the read, not that the flash changed.

## Going faster

`linuxgpiod` is chosen for being unbreakable, not quick; a 512 KB dump over it
takes a few minutes. The `bcm2835gpio` driver is much faster but maps the SoC's
registers directly and so has to be told the right peripheral base — `0xFE000000`
on a Pi 4, `0x3F000000` on a Pi 2 or 3. Getting that wrong does nothing useful.

```tcl
adapter driver bcm2835gpio
bcm2835gpio peripheral_base 0xFE000000
bcm2835gpio speed_coeffs 236181 60
```

Not worth it for a dump you do once. It is worth it if you end up writing flash
repeatedly.

## Files

```
lpc54605.cfg        the target: LPC54605J512, SWD, reset via SYSRESETREQ
rpi4-swd.cfg        the adapter: Pi GPIOs, open-drain reset
swd-session.sh      sourced by the rest: the pins, and one call that opens
                    a session and reports whether the core actually stopped

finding the header
  identify-header.sh  classifies unknown header pins; --watch tells a pull-up
                      from a chip output by sampling across the reset loop
  find-swd.sh         sweeps pin pairs until the debug port answers

reading
  read-memory.sh      reads any region twice and compares before believing it
  dump-flash.sh       read-memory.sh with the flash's address and size
  flash-info.sh       asks the flash controller for its part id; writes nothing

running firmware without touching flash
  run-from-ram.sh     loads an image into SRAM and starts it

writing
  erase-sectors.sh    erases sectors and reads them back to prove it
  write-image.sh      writes a raw binary and has the part verify it
  reset.sh            resets and runs what was written

debugging
  fault-report.sh     halts where it is and says what faulted
  catch-failure.sh    watches firmware that is still running, and halts it in
                      the act of faulting or of losing its main loop
  decode-fault.py     turns the fault status registers into a sentence
```

## Catching firmware that fails later

`fault-report.sh` answers for a part that has already stopped. Firmware that
runs for a while and then goes needs the opposite: everything arranged before
the failure, because the watchdog resets the part a second or so afterwards and
a reset clears every register that would have said why.

```sh
sudo swd/catch-failure.sh --seconds 150
```

It never halts the core to set itself up — the watchdog is stretched and the
vector catch armed through the debug port while the firmware runs, because a
stall that only appears after a debugger interrupted the firmware is the
debugger's stall. Then it watches two things at once: the core, which it halts
on any fault, and the watchdog counter, which falls as soon as a main loop
stops reaching its feed. Either way the part ends up stopped in the act, and
the registers are read and decoded there.

[`docs/scheduler-stall.md`](../docs/scheduler-stall.md) is what it found.

## Trying firmware without writing any

```sh
LDSCRIPT=LPC546xx_sram qmk/test/build-k100.sh <work-dir>   # on the build machine
sudo swd/run-from-ram.sh corsair_k100_default.bin          # on the Pi
```

That one command loads it, runs it, and says what happened — on purpose, and
not for brevity. A firmware that faults here spins in a handler that does not
feed the watchdog, so a few seconds later the part resets, every fault status
register is cleared and flash starts running again. A separate `fault-report.sh`
afterwards would arrive to find a clean machine running Corsair's firmware and
report that nothing had gone wrong. The loader therefore arms the core's vector
catch before resuming, so a fault stops it before its own handler runs, and
reads the registers in the same session.

`--no-run` loads without starting, to step from. `--watch <seconds>` changes how
long it waits before concluding the firmware is still going.

The image runs entirely out of SRAM. Nothing is erased, nothing is written to
flash, and the next power cycle runs whatever is in flash again as though this
had not happened — so an attempt costs seconds rather than a flash sector and
something permanent.

Three things make it work, and all three are easy to get wrong silently:

- **The upper SRAM banks are not clocked out of reset.** Only the first 32 KB
  at `0x20000000` is live; `SRAM1`, `SRAM2` and `SRAM3` need `0x38` written to
  `AHBCLKCTRLSET0` at `0x40000220` first. Without it an image larger than 32 KB
  loads into nothing. The script does this before loading, and `verify_image`
  afterwards is what would catch it if the write were wrong.
- **Code must not live in `ram0`.** `crt0` zeroes the whole of `ram0` before any
  HAL code runs, so an image whose own text sat there would erase itself
  mid-startup. `LPC546xx_sram.ld` puts code in a separate region below
  `0x20010000` and starts `ram0` above it.
- **`VTOR` still points into flash** until `crt0` moves it. The script sets it
  to the loaded vector table before resuming, so nothing can interrupt through
  the bootloader's handlers in the meantime.

The two scripts refuse each other's images. A flash-linked build has its reset
vector in `0x0001xxxx`, and `run-from-ram.sh` will not load it; an SRAM-linked
build has its vectors in `0x2000xxxx`, and `write-image.sh` will not put it in
flash, where it would branch into memory that holds nothing at power-up with
the working firmware already erased to make room.

This is not a replacement for a flash build. The code runs at different
addresses, so anything that depends on where it lives behaves differently or
not at all, and the bootloader's handover is not exercised. It is where the
boot faults get found; flash is where the result has to work.

## Debugging firmware with it

`fault-report.sh` is the one that changes how this project is worked on. A
Cortex-M records why it faulted and, for most kinds, the address involved, so
one command answers what was previously guessed at from how long a keyboard
stayed off the USB bus:

```
CFSR 0x00008200  HFSR 0x40000000
  BusFault: PRECISERR: a data access the bus refused; BFAR holds the address
  BusFault: the address it touched was 0x40108000
  HardFault: FORCED: a fault that escalated because its own handler was
             disabled or it happened at too high a priority
```

It halts without resetting, because a reset clears those registers and the
stack frame behind them. Reset first and every machine looks clean.

## What a rescue route does not buy

It lowers what a mistake costs. It does not lower the care a write deserves,
and three things stay true:

- The route is proven against **one** failure, on **one** unit. It assumes the
  bootloader is intact and the part still answers. Neither an overwritten
  bootloader nor a damaged chip has any route at all.
- Every attempt means an opened case, wires on a board and flash cycles spent.
- Writing below `0x10000` is refused by every script here, and the flag that
  lifts the refusal is spelled out at a length nobody types by accident.

The cautious order is therefore: read before writing, prefer somewhere that is
not flash, make one change per observation, and keep the claim no larger than
the measurement.
