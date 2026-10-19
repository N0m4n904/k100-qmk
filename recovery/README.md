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

What its six pins actually are is **not known**, and is not guessed at here.
Four test points (`TP42`–`TP45`) sit directly beneath pins 2 to 5, which is the
shape of a header carrying four signals between a supply and a ground, but that
is an inference from a photograph and nothing is wired on the strength of it.
The next section determines it by measurement instead.

nRESET is worth having if one of the six pins turns out to be it: this board's
firmware faults and is restarted by its watchdog every few seconds, and holding
it in reset makes the chip sit still. It is not required — reading flash goes
through the debug port's own bus bridge and does not need the core stopped, so a
dump works straight through the loop.

## Step 1 — find out which pins are safe to touch

```sh
sudo recovery/identify-header.sh 24
```

Probe wire from Pi GPIO24 to `J1` pin 1, run it, write the verdict down, move
the wire to pin 2, run it again. Six runs.

This reads each pin twice, once with the Pi's internal pull-up applied and once
with its pull-down, and sorts it into one of three classes:

- **follows the pull** — nothing else is driving it, so it is safe to drive.
- **held low** — ground, or an output the keyboard is driving.
- **held high** — a supply, or an output the keyboard is driving.

Only the first class gets driven in step 2. That is the entire safety argument:
driving a clock into a pin the keyboard's own chip is driving puts two outputs
in opposition, and this is how you find out which pins those are before doing
it rather than after. Reading is passive and cannot hurt anything.

## Step 2 — find SWDIO and SWCLK

```sh
sudo recovery/find-swd.sh 24 25 23 22
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
sudo recovery/dump-flash.sh --swclk 25 --swdio 24
```

Reads all 512 KB twice and refuses to produce a dump unless the two passes
agree, because a lead that moved mid-read gives a file that looks fine and is
not. It then checks the result is not a single repeated byte, which is what a
read that never reached the flash looks like.

Out come `k100-flash-512k.bin` and `k100-bootloader-64k.bin`. **Copy both off
the Pi.** From that moment the only irreplaceable thing on the keyboard exists
somewhere else, and nothing that happens afterwards can be worse than where this
started.

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
lpc54605.cfg        the target: LPC54605J512, SWD, no reset or halt in it
rpi4-swd.cfg        the adapter: Pi GPIOs, slow clock, open-drain reset
identify-header.sh  classifies unknown header pins as safe or driven
find-swd.sh         sweeps pin pairs until the debug port answers
dump-flash.sh       reads 512 KB twice and compares before believing it
```
