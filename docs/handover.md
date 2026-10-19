<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Recovering a K100 with a programmer

A self-contained page for someone whose K100 does not start and who has never seen this
project. Everything needed is here; no other document has to be read first.

**This worked.** A K100 bricked on 18 October 2026 was recovered on 19 October by the route
below. It needed a Raspberry Pi, four jumper wires and no soldering. The step-by-step
procedure, with the scripts, is in [`../recovery/README.md`](../recovery/README.md); this page
is the background to it.

## What the device is

A Corsair K100 RGB (OPX optical switches). The controller is an **NXP LPC54605J512ET100** —
Cortex-M4F, 512 KB internal flash at `0x00000000`, 160 KB SRAM at `0x20000000`, 32 KB SRAMX at
`0x04000000`.

## What was wrong with it

It was a software brick. No flash region was erased, nothing was over-volted, and the chip was
undamaged — confirmed afterwards, when it attached over SWD, reported its part id and ran
Corsair's firmware again.

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

One trap, found the hard way. With the core halted straight out of reset, the boot ROM is
still mapped over the bottom `0x200` bytes, so the vector table read back from `0x00000000` is
the ROM's and not the flash's — all of its handlers point into `0x0300xxxx`. Read the region
again after letting the part run far enough to hand over (`--halt-late` in
[`../recovery/read-memory.sh`](../recovery/read-memory.sh)) and splice the real `0x200` bytes
in. A complete image is one whose word at `0x1c` is the two's complement of the seven before
it; that is NXP's boot checksum, and it is how to tell the two apart.

## Getting access

### SWD

The main PCB carries an unpopulated six-pin header, **`J1`**, in plated through-holes at 0.1 in
pitch, immediately beside the controller and its crystal, with pin 1 marked by a square pad and
a silkscreen triangle. Four test points, `TP42`–`TP45`, sit directly beneath pins 2 to 5.
Plated holes take a jumper wire pushed in and taped down, so **no soldering is needed**.

Measured on one unit, pin 1 leftmost:

```
1  a supply, held high            4  signal, pulled up
2  SWDIO                          5  signal, pulled up
3  SWCLK                          6  ground
```

Only pins 2 and 3 were identified by what they do; the rest by level. Nothing on this header
was driven until the Pi had measured each pin under its own pull-up and pull-down across
several of the part's restarts, which tells a pull-up resistor on the board from an output the
chip is driving. [`../recovery/README.md`](../recovery/README.md) has that procedure and the
scripts; do it rather than trusting the table, in case the header differs between revisions.

**Debug access is not disabled**, at least on this unit. The port attached first try and
reported `DPIDR 0x2ba01477`, a Cortex-M4 r0p1 and a full ROM table, and the flash controller
answered IAP with part id `0xfff54605`. An earlier guess here said the opposite, reasoning from
a `DWT->CTRL` write that faults in code running on the part; that inference was wrong, and the
measurement replaces it.

### ROM ISP over UART

The LPC546xx ROM bootloader is entered by holding the ISP pin low through reset, after which it
speaks over UART. `lpc21isp` or NXP's `blhost` drive it. This route does not depend on the debug
block being unlocked, so it is worth trying first if SWD will not attach.

Note that the chip's full-speed USB controller — the one the ROM's USB ISP mode would use — is
not the controller this keyboard is wired to, so USB ISP is not reachable from the keyboard's own
connector.

## What to write

### Option A — erase the application and let the bootloader do the rest

**This is what worked, and it writes almost nothing over SWD.**

Corsair's bootloader stays put when it finds no valid application. It does not need to be
coaxed, and no EEPROM word has to be understood: erasing flash sector 2 — `0x00010000` to
`0x00017FFF`, which holds the container header the bootloader reads — is enough. On the next
power-up the keyboard enumerated as `1b1c:1b7c` with `bcdDevice=000a`, which is bootloader
0.10.45, and sat there waiting.

From there the vendor's own update path takes over, which is far better tested than anything
done through a probe:

```sh
tools/k100-bragi/target/release/k100-bragi info          # expect mode Bootloader
tools/k100-bragi/target/release/k100-bragi flash \
    firmware/Heracles_Application_Firmware_v0.35.288.bin --device 0x1b7c --commit
```

The image is 150908 bytes, fetched and checksum-verified by `tools/fetch-firmware.py`. After
the apply the keyboard rebooted into `SelfOperated`, came back as `bcdDevice=0023` with three
interfaces and its original serial number, and answered `physical layout Iso` — which only the
application can do, and only by reading its own hardware.

Note the keyboard's USB appetite. On a Raspberry Pi's own port the bootloader enumerated, held
for half a minute and then dropped the port into `cannot reset (err = -71)`. Do the USB half of
this on a properly powered host.

### Option A′ — write the application over SWD instead

If the bootloader will not stay, write the same file, **header included, to `0x00010000`**. The
first `0x2c` bytes are a container header that belongs in flash; the bootloader reads the
application's entry address out of it rather than using a vector table. Erase sectors 2 to 6
first. This was not needed and so has not been tested.

Either option requires the bootloader at `0x00000000` to be intact. If it has been erased, a
small shim at `0x00000000` that branches to `0x00012569` (the entry address in that image's
header) would be needed instead. Nobody has tested that either.

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

- Whether Corsair's bootloader verifies anything beyond the CRC in the container header.
- Whether the stock application runs correctly if started by something other than Corsair's
  bootloader.
- What the bootloader's rule actually is for staying put. It stays when the application is
  gone, which is all the recovery needed; whether it also reads an operating-mode word from the
  on-chip EEPROM, as the firmware's own route back assumed, was never established.

## Settled

- **Read-out protection is not set**, on this unit. All 512 KB read out over SWD, twice,
  identically.
- **Corsair's bootloader is dumped**, 64 KB with a valid NXP vector checksum. It is their
  copyrighted code and is not redistributed here.
