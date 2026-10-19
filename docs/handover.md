<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Recovering a K100 with a programmer

A self-contained page for someone who has an SWD probe or a USB-serial adapter and has never
seen this project. Everything needed is here; no other document has to be read first.

## What the device is

A Corsair K100 RGB (OPX optical switches). The controller is an **NXP LPC54605J512ET100** —
Cortex-M4F, 512 KB internal flash at `0x00000000`, 160 KB SRAM at `0x20000000`, 32 KB SRAMX at
`0x04000000`.

## What is wrong with it

It is a software brick. No flash region was erased, nothing was over-volted, and the chip is
believed undamaged.

Flash from `0x00010000` holds a custom application that faults a few seconds into boot: it
writes to the on-chip EEPROM array at `0x40108000` while the EEPROM is still powered down,
because the `PWRDWN` bit at EEPROM controller offset `0x18` was never cleared. The access takes
a bus error, the fault handler spins without feeding the watchdog, the watchdog resets the
part, and the bootloader starts the same image again. The cycle repeats roughly every eleven
seconds and never reaches USB initialisation, so the keyboard never appears on the bus.

Corsair's bootloader occupies the first 64 KB and was never written to, so it is intact. It
reads an operating-mode word from the on-chip EEPROM at startup and decides whether to stay or
to start the application. It makes that decision *before* putting itself on the bus — verified
by watching the host's USB tree at 20 ms resolution across a power cycle, which recorded no
enumeration at all — so there is no window in which a host can talk to it.

There is no key held at power-up that changes this. Escape and the topmost macro key were each
tested through a verified power cycle, held for a minute, while watching every vendor ID on the
host: nothing enumerated.

## Do this first: dump the whole flash

**Before erasing or writing anything, read all 512 KB out and save it.**

No public copy of Corsair's bootloader exists. Their update protocol exposes a file ID for the
application and none for the bootloader, so it cannot be read out over USB by any known means.
If it is erased without a dump, it is gone permanently — not just for this keyboard, but for
anyone who ever attempts this again. A dump is the single most valuable thing that can come out
of this session, and it costs one command.

```sh
# SWD, via OpenOCD
dump_image k100-full-512k.bin 0x00000000 0x80000
```

Keep `0x00000000`–`0x0000FFFF` separately: that is the bootloader.

## Getting access

### SWD

The main PCB carries an unpopulated six-pin header, **`J1`**, in plated
through-holes at 0.1 in pitch, immediately beside the controller and its
crystal, with pin 1 marked by a square pad and a silkscreen triangle. Four test
points, `TP42`–`TP45`, sit directly beneath pins 2 to 5. Which pin is which is
not known and is not worth guessing: [`recovery/`](../recovery/README.md)
determines it by measurement, and holds a complete procedure for doing all of
this with a Raspberry Pi and no other hardware.

**Debug access is probably disabled.** A read-modify-write of `DWT->CTRL` at `0xE0001000` from
code running on this part takes a fault, which is what a locked debug block does. If the probe
cannot attach, the remaining option is the debug mass-erase sequence, which erases the
bootloader along with everything else — so the dump above becomes impossible rather than merely
skipped. Try to attach and dump before committing to an erase.

### ROM ISP over UART

The LPC546xx ROM bootloader is entered by holding the ISP pin low through reset, after which it
speaks over UART. `lpc21isp` or NXP's `blhost` drive it. This route does not depend on the debug
block being unlocked, so it is worth trying first if SWD will not attach.

Note that the chip's full-speed USB controller — the one the ROM's USB ISP mode would use — is
not the controller this keyboard is wired to, so USB ISP is not reachable from the keyboard's own
connector.

## What to write

### Option A — restore Corsair's stock application

The preferred outcome: a normal, working K100.

The official image is `Heracles_Application_Firmware_v0.35.288.bin`, 150908 bytes, fetched and
checksum-verified by `tools/fetch-firmware.py` in this repository. **Write the whole file,
header included, to `0x00010000`.** The first `0x2c` bytes are a container header that belongs in
flash; the bootloader reads the application's entry address out of it rather than using a vector
table.

This requires the bootloader at `0x00000000` to be intact. If it has been erased, a small shim
at `0x00000000` that branches to `0x00012569` (the entry address in that image's header) would be
needed instead. Nobody has tested that.

### Option B — bootloader-less custom firmware

`chibios/os/common/startup/ARMCMx/compilers/GCC/ld/LPC546xx_512k_160k.ld` links an image to run
from `0x00000000` with no bootloader beneath it. This is the fallback if the bootloader is gone.

Be aware that the QMK firmware in this repository **has never completed a boot on hardware**, so
this option does not currently produce a working keyboard.

## Flash map

```
0x00000000   Corsair's bootloader, 64 KB — intact, irreplaceable, dump it
0x00010000   container header, 0x2c bytes, written to flash as part of the image
0x0001002c   application code
             stock entry point 0x00012569, branched to by the bootloader
```

## Known unknowns

- Whether read-out protection (ECRP) is set. If it is, a dump may be refused and only a mass
  erase will be possible.
- Whether Corsair's bootloader verifies anything beyond the CRC in the container header.
- Whether the stock application runs correctly if started by something other than Corsair's
  bootloader.
