# Can the K100 be unbricked?

**Yes, under two rules, with one unknown that only matters if those rules are broken.**

This is the document to re-read before any write to the keyboard. Evidence is quoted from
Corsair's own updater bundle and from ckb-next; see `firmware-format.md` and
`bragi-firmware-update.md` for how those were obtained.

## Hold B and L while plugging in, and the bootloader keeps the keyboard

**Decoded out of Corsair's bootloader and then measured on hardware.** It needs no tool, no
programmer and no working firmware — only that the bootloader itself is intact, which the one
rule below protects.

```
[ 29.193s] GONE      10-2  1b1c:1b7c
[ 54.912s] APPEARED  10-2  1b1c:1b7c  bcdDevice=000a
           product='CORSAIR K100 RGB Optical-Mechanical Gaming Keyboard'
```

Both cables out, hold `B` and `L` and nothing else, plug in while holding. The keyboard comes
up as the bootloader and `k100-bragi flash` can replace whatever was on it.

### How it was found

The bootloader registers a callback at `0x330` and then runs its own matrix scan at `0x1e38`.
The scan sets up eighteen sense lines from a table of port and pin pairs, masks each row's
reading to the positions that carry a key, and calls the callback with `row * 18 + col` and
whether it is down. The callback keeps three positions separately and everything else in one
flag:

| Flag | Position | Key |
| --- | --- | --- |
| `0x200002a6` | 1 | Esc |
| `0x200002a7` | `0x4f` | B |
| `0x200002a8` | `0x40` | L |
| `0x200002a5` | anything else | — |

The decision at `0x444` then branches to the application unless **all four** hold: B down, L
down, nothing else down, and Esc **not** down. Only then does it fall into the bootloader at
`0x458`.

The positions are this keyboard's own matrix, so they map straight through
`qmk/keyboards/corsair/k100/keyboard.json`: position 1 is row 0 column 1, which is Escape;
`0x40` is row 3 column 10, which is L; `0x4f` is row 4 column 7, which is B.

### What this means for the keyboard this project bricked

It was probably never a brick. The firmware that would not enumerate sat behind a bootloader
that would have kept control if B and L had been held — and the case was opened and a
programmer wired to the board instead.

Worse, the key that was tried was Escape, because that is what this firmware's own held-key
escape hatch reads. **Escape is the one key that guarantees the opposite**: the bootloader
treats it as a reason to start the application. The two mechanisms were in direct conflict and
nothing said so.

## Three further layers of recovery

### Layer 1 — the Bragi bootloader (primary, software only)

The bootloader is a **separate, independently versioned flash region**, not part of the
application image:

- Bragi exposes `BOOTLOADER_VERSION` as a property distinct from `FIRMWARE_VERSION`, and
  ckb-next recorded a real K100 reporting both (`fw 0.32.6, bld 0.10.45`). **This project has
  now read both off its own keyboard** and got the same pair of values, so the separate
  bootloader region is observed rather than inferred.
- The image-type enum lists `FIRMWARE = 1` and `BOOTLOADER = 2` as separate types.

**The official updater is purpose-built to rescue a device sitting in bootloader mode.** On
connecting any device it reads the operating mode and flags the state:

```js
const mode = await device.getOperatingMode({retries: 3, waitRequest: 100, waitResponse: 3000});
const inBootloader = mode === OperatingMode.BOOTLOADER;
if (inBootloader) update(id, {inBootloader: true});
```

and then, rather than treating it as an error:

- the device card renders `{type: "updates-available", message: "inBootloader", showButton: true}`
  — an actionable flash button;
- the modal title gains `FirmwareUpdateModal.required`;
- `inBootloader` short-circuits the version comparison, so an update is always offered:
  `let needs = !!device.inBootloader; if (!needs && current) needs = this.needsUpdate(current, target);`
- the update plan gains a dedicated step: `createBootloaderStep()` → `{id: Bootloader, title: "Bootloader Notice"}`.

So a device stranded in bootloader is a supported, documented state with an official way out.

**But not from Linux.** The updater is a web page driving the keyboard over WebHID, which on Linux
means hidraw `write()`, and hidraw cannot write to this keyboard's Bragi interface at all. Tested
twice, the second time with udev rules explicitly granting access: the page fails on its first
command either way. See `docs/bragi-firmware-update.md` and `udev/README.md`.

Layer 1 therefore costs a **Windows machine**, not just a cable. That does not weaken it as a
recovery path — it is still the supported route, and `k100-bragi` drives the same transitions over
usbdevfs from Linux — but it has to be available *before* the keyboard is put into bootloader
mode, not arranged afterwards.

### Layer 2 — the LPC54605 ROM ISP (hardware backstop)

The LPC546xx boot ROM is **mask ROM and cannot be erased by any software**. ISP entry is by
pin state at reset (the ISP pin together with PIO0_4 and PIO0_5), and it presents a USB MSC
volume. This survives the destruction of anything in flash, including the bootloader.

Costs: the case must be opened and the pins pulled, as the K100 has no external ISP pinhole.
Conditional on ECRP — see the unknown below.

#### Lead: extra contacts at the USB passthrough connector

**Unverified, secondhand — not yet confirmed on hardware.**

Source: **CAC2244**, *"K100: Removal of original thick cord to use USB pass-through port as
input!"* — <https://www.youtube.com/watch?v=MEwbK5u6r_k> (channel:
<https://www.youtube.com/@cac2244>).

That video replaces the K100's captive cable by repurposing the USB passthrough port as the
keyboard's own upstream input, which means the passthrough connector carries more to the main
PCB than the passthrough pair alone. Per the video there is an **additional connector inside
it along with a ground contact**, and its author speculated this relates to the keyboard's
reset functionality.

Worth chasing hard, because it lines up with how this chip enters ISP. LPC546xx boot source
is selected by pin state at reset — the ISP pin together with PIO0_4 and PIO0_5 — so a
factory test or programming header would plausibly expose exactly: reset, the ISP/boot pins,
and ground. If those contacts are that header, then layer 2 becomes reachable **without a
full teardown**, which would materially improve the project's safety margin.

Equally, it could be a USB shield ground, a passthrough data pair, or an unrelated
manufacturing fixture. The speculation about reset is the video author's, not a measurement.

To check, cheapest first:

1. Photograph the passthrough connector and count the contacts.
2. Continuity-test each contact against ground and against the MCU's reset and PIO0_4 /
   PIO0_5 pins.
3. If a reset line is confirmed, test whether holding it while plugging in produces an ISP
   MSC volume — the same observable Corsair's pinhole produces on their older boards.

This needs a multimeter, so it belongs with the acquisition step in `ARCHITECTURE.md` rather
than the zero-cost work. Step 1 needs nothing but a phone camera.

#### Holding Escape at plug-in is a reset, not bootloader entry

Worth stating because it is easy to assume otherwise. Corsair's reset procedure — unplug,
hold Escape, plug in, keep holding until the lighting flashes, then unplug and replug — is a
**settings reset**, and it **clears onboard profiles**, so saved lighting and hardware macros
are lost.

It is *not* a documented route into the bootloader. No official Corsair page describes a true
bootloader or recovery mode for the K100, and a community report describes a hardware-held
entry that did not permit replacing firmware, the keyboard merely enumerating under a
different device name.

So it does not provide the firmware-independent bootloader entry that would most strengthen
this document. The hardware route remains the LPC546xx ISP pins at reset, which needs the case
open.

One thing it is still worth doing once: hold Escape at plug-in and check `lsusb` for a changed
product ID or a mass-storage volume. If anything different enumerates, that *would* be a
recovery mode and would matter a great deal. Read-only, and the only cost is the profile wipe.

### Layer 3 — SWD

**This is the layer that actually recovered the keyboard.** See
[`../swd/README.md`](../swd/README.md) for the procedure and
[`handover.md`](handover.md) for the background.

No probe is needed: a Raspberry Pi's GPIOs are 3.3 V, the LPC54605 is 3.3 V, and OpenOCD will
bit-bang SWD between them. No soldering either — the connection point is an unpopulated six-pin
header in plated through-holes, `J1`, next to the controller, and a jumper wire pushed into a
hole and taped down is contact enough. The ECRP caveat turned out not to apply on this unit:
the debug port attached first try and all 512 KB read out.

## The two rules

### Rule 1 — never write a bootloader image

Only ever write image type `FIRMWARE = 1`. **Never `BOOTLOADER = 2` or
`FIRMWARE_WITH_BOOTLOADER = 3`.** Corrupting the bootloader is the one genuinely
unrecoverable mistake available to us, because it destroys layer 1 and leaves only the
layers that depend on ECRP not being set.

Corsair's own tool observes this rule. When it detects that a bootloader update would be
needed it refuses outright and carries on without one:

```js
this.needsBootloaderUpdate(device, kind, target) &&
  console.log("Chip needs isp update which is not supported for now");
plans.push({..., needsBootloaderUpdate: false});
```

Two things follow. The bootloader is never touched by the normal update path, so it stays
intact as the recovery mechanism. And Corsair themselves consider bootloader replacement an
ISP-only operation.

### Rule 2 — demonstrate recovery before it is needed

Prove the round trip with **official images only**, while the keyboard is still healthy and
before a single byte of custom firmware is written:

0. **Replug the keyboard, and run nothing else against it first.** The vendor command endpoint
   can be left unresponsive by a failed HID client -- Corsair's web updater is one, and it
   wedged this keyboard reproducibly. A single power cycle clears it. This is a precondition,
   not a troubleshooting step: see `docs/bragi-firmware-update.md`.
1. Enter bootloader: `SET OPERATING_MODE 3`.
2. Flash the *older* official image, 0.30.257.
3. Confirm it boots and reports the older version.
4. Flash 0.35.288 back and confirm again.
5. Exit via the documented path, `BOOTLOADER → SELF_OPERATED → HOST_CONTROLLED`.

All five steps are reachable:

```sh
k100-bragi info                                         # step 0, read-only
k100-bragi enter-bootloader                             # step 1
k100-bragi flash fw/Heracles_...v0.30.257.bin --commit  # step 2
k100-bragi exit-bootloader && k100-bragi info           # step 3
k100-bragi flash fw/Heracles_...v0.35.288.bin --commit  # step 4
k100-bragi exit-bootloader && k100-bragi info           # step 5
```

**Steps 1 and 5 are optional.** The keyboard's own manifest lists
`"operating-modes": ["host-controlled", "bootloader"]` for a firmware write, host-controlled
first, and that is the mode Corsair's updater prefers. So the bootloader is a *fallback* for a
device already stranded there, not a precondition for flashing one that works. Writing from
host-controlled mode leaves the application in charge of its own replacement, which is the
better-trodden path of the two.

Steps 1, 3 and 5 are proven on hardware on their own, with nothing written: see "The mode
round trip, proven" in `bragi-firmware-update.md`. That was worth doing first, because it
establishes the way back before anything needs it.

## Rule 2 is done. The round trip works.

Carried out on the keyboard, with Corsair's own verified image:

```
before   firmware 0.32.6,  bootloader 0.10.45, SelfOperated, 1024-byte reports
         took host control, the mode the manifest lists first
         wrote 150908 bytes in 148 chunks (1017 then 1021), file id 3
         applied as Firmware with checksum 0x835570e1, acknowledged
         rebooted, back in SelfOperated
after    firmware 0.35.288, bootloader 0.10.45, SelfOperated, 1024-byte reports
         bcdDevice 0.20 -> 0.23, three interfaces, usbhid bound to all of them
```

The version the keyboard reports is exactly the one the image declares, so the write reached
flash and the device is running it. Nothing was lost: the keyboard came back a working
keyboard, on newer official firmware than it started with.

That settles the parts of the recovery path that were previously argued from documentation:
the bootloader can be entered and left, a firmware image can be written and applied, and the
result is observable. What it does **not** settle is whether the bootloader re-enters when the
application is *broken* — see the residual risk below. That one cannot be tested without
breaking the application.

If that loop works, recovery is demonstrated empirically rather than argued from
documentation, and both images are already verified authentic against Corsair's published
sha512 hashes. If it does not work, we have learned so with the stock firmware still intact
and nothing lost.

This is the real answer to "can I unbrick it": establish the unbrick path first, and only
then risk something that needs it.

## What the write path refuses

`k100-bragi flash` is built to fail rather than to succeed. Each of these is a way the
keyboard could be lost, removed before anything is sent:

| Refused | Why it would matter |
| --- | --- |
| Any image type but `FIRMWARE = 1` | Rule 1. The one unrecoverable mistake |
| A declared length that disagrees with the file | A truncated download would flash as a valid image |
| A CRC that does not match the contents | The only integrity check the format has |
| An image built for another product id | `1bc5` is a different chip entirely; its image is ruin here |
| An even entry point | Not a Thumb address; the bootloader branches to it and faults |
| An entry outside `0x1002c..0x80000` | Branches into the bootloader or into nothing |
| A mode the device's manifest does not list | It names `host-controlled` and `bootloader`; self-operated refuses file commands |
| Any device other than the named product id | See below |
| Anything at all, without `--commit` | The default is a dry run that sends nothing |

**The device is named, not searched for.** Read-only commands fall back to opening any
Corsair device, deliberately: a keyboard in bootloader mode may report a product id this tool
has never seen, and refusing to open it then would strand the device this exists to recover.
Writing cannot afford that generosity. `flash` and `enter-bootloader` open exactly one
product id — `0x1b7c` unless `--device` says otherwise — and refuse an ambiguous bus rather
than picking by scan order. `exit-bootloader` stays permissive, because it is the direction
that rescues.

`verify-image` runs the same container gate offline, with no device attached, so an image can
be rejected before the keyboard is even plugged in.

## How the bootloader is entered, and why our firmware must do it too

**This is the finding that makes custom firmware survivable.** Recovered from the stock
application image, not guessed, with every register offset cross-checked against NXP's
`LPC54605.h`.

`SET OPERATING_MODE` is handled at flash `0x12f8a`. It range-checks the value to 1–7 — the
same check the updater's `setOperatingMode` makes — reads the current mode, returns success if
it already matches, and otherwise calls the setter at `0x1593c`. That setter does only two
things:

1. writes the mode to **word index 2 of the EEPROM**, and
2. arms a 10 ms timer whose callback is an inlined `NVIC_SystemReset` at `0x1556c`.

That is the whole mechanism as the stock application performs it. The consequence drawn from
it here was that any firmware able to write that EEPROM word can enter the bootloader, without
Corsair's application, the Bragi protocol or a working USB stack.

### That consequence is wrong — tested

`Fn+Esc` was run on this firmware with the bus watched at 20 ms
(`tools/watch-bootloader-route.py`). The keyboard **left the bus and came back as itself**,
`bcdDevice 0.01`, 0.67 s later.

The reset is therefore not in doubt, and neither is the write. `k100_enter_bootloader()`
resets only after writing the mode word *and* reading it back, so a reset happening at all
means both succeeded. The bootloader then started this firmware anyway.

So writing word 2 and soft-resetting is **not sufficient**, and the route this project has
been treating as its escape hatch does not work. What the bootloader keys on is still open.
Two things are known about its side of it:

- It does read that array. The function at `0x1664` takes a word index, range-checks it
  against 31, and loads from `0x40000000 + 0x108000 + index * 4` — a computed address, which
  is why searching the dump for a literal `0x40108000` finds nothing.
- Read over SWD after the attempt, words 0 to 7 of the array were all zero. That reading is
  not conclusive on its own, because nothing had clocked the EEPROM since the reset.

Until this is understood, **SWD remains the only proven way back** from firmware that does not
speak Bragi: erase sector 2 and reset. See [`swd/README.md`](../swd/README.md).

### Bootloader mode does not survive a power cycle — tested

The obvious reading of "the mode lives in EEPROM" is that the choice persists, so a keyboard
left in bootloader mode would still be there after unplugging it. **That is wrong, and it was
measured rather than reasoned about.** Put into bootloader mode and then fully power-cycled,
the keyboard came back running the *application*: `bcdDevice` `0x23`, three interfaces,
1024-byte endpoints.

So the bootloader does not simply read word 2 and stay. Either it also requires the reset to
have been a soft one, or it clears the word once it has acted on it; nothing here distinguishes
the two, and Bragi exposes no way to read an EEPROM word back to find out. Both readings have
the same two practical consequences, which is why it is not worth guessing between them:

- **A power cycle always returns to the application.** It is not a rescue. A keyboard running
  firmware that hangs cannot be recovered by unplugging it, and the held-key route works
  because the *firmware* reads the key, not because the bootloader does.
- **Our route matches the one path known to work.** The firmware here writes the same word and
  then calls `NVIC_SystemReset`, which is a soft reset — exactly what the stock application
  does. Whichever of the two readings is true, this is the sequence that has been observed to
  land in the bootloader.

> **Unplugging this keyboard means both connectors.** The K100's captive cable ends in two USB
> plugs, and the keyboard keeps running on either one alone — pulling just the first does
> nothing at all. A real power cycle needs both out. This cost one confusing test before it was
> noticed, and `k100-bragi reset` exists partly because a USB port reset is easier to arrange
> than reaching behind a desk for two plugs.

The access path, from the driver at `0x233f8` which is NXP's EEPROM word write:

| What | Where |
| --- | --- |
| EEPROM controller | `0x40014000`, `EEPROM_BASE` in NXP's header |
| EEPROM memory window | `0x40108000`, 16 KB, word-addressable |
| Operating mode | word index 2, so `0x40108008` |
| Clock gate | `AHBCLKCTRL0` bit 9, `SYSCON_AHBCLKCTRL_EEPROM_MASK` |

and the sequence, which the disassembly and the header agree on byte for byte:

```c
EEPROM->AUTOPROG   = 1;    /* program on a single word write; no page erase, so
                              neighbouring settings words are never touched */
EEPROM->INTSTATCLR = 4;    /* EEPROM_INTSTATCLR_PROG_CLR_ST_MASK */
*(volatile uint32_t *)0x40108008 = 3;              /* BOOTLOADER */
while (!(EEPROM->INTSTAT & 4)) { }                 /* END_OF_PROG */
NVIC_SystemReset();
```

`AUTOPROG = 1` is the detail that makes this safe: the write programs one word, so there is no
read-modify-write of a page and nothing else in the EEPROM is disturbed.

### Why this is a requirement, not a nicety

**QMK does not implement Bragi.** The moment custom firmware runs, `k100-bragi` cannot reach
the keyboard at all — there is no vendor interface to talk to, and the stock write path is
gone with the stock application. So without a route back, the *first* custom flash is
permanent whether or not it works: a perfectly good QMK build that cannot be replaced is as
much a dead end as one that does not boot.

The firmware therefore needs its own way into the bootloader, and more than one, because each
possible route depends on a different part of the firmware being correct:

| Route | Depends on | Covers |
| --- | --- | --- |
| `QK_BOOT` keycode | USB and the matrix working | an ordinary reflash |
| Escape held at power-on | the matrix read only | USB broken, keyboard otherwise alive |
| USB enumeration watchdog | neither | the firmware failing before it is usable |

The third is the one that matters for a first flash, because it is the only one that survives
the firmware being wrong about the things this project is still guessing at — the matrix sense
polarity, the strobe settling time, the SPI pins. It arms a timer early in boot and, if USB has
not reached a configured state by the time it fires, writes the EEPROM word and resets. A
firmware that never becomes usable then puts itself back in the bootloader without anyone
touching the keyboard.

### Which key, and why it is not a particular one

**Hold any single key on the top row** while the keyboard powers up — Escape, G1, any of the
function keys, Print Screen. One key, not two.

Naming one key was the first design and it is the wrong one. The strobe and sense assignment
this firmware uses was decoded out of Corsair's firmware image rather than measured on the
board, and on the top row the macro key `G1` sits at column 0 with Escape next to it at column
1. An off-by-one anywhere in that decoding would move the escape hatch one key sideways
without changing anything observable: the person holding Escape sees a keyboard that does not
come back, which is indistinguishable from firmware that never ran. Since the whole point of
this route is to survive the parts of the project that are still guesses, it must not itself
rest on one of them.

So `recovery_key_held()` asks a question that does not name a column: is exactly one sense on
row 0 reading differently from all the others? That is true for a single held key under either
switch polarity, either pull direction, and any permutation of the columns. It is also a
*stricter* garbage filter than the named-column version — a scrambled reading now has to
produce exactly one odd bit out of seventeen, four samples running, rather than merely put one
particular bit in the minority.

### Where the key check sits, and what it therefore covers

All three routes run *inside* the firmware, so the question is how much of boot happens before
the earliest of them. The answer is less than it looks, because `keyboard_pre_init_kb` is not
reached from `keyboard_init()` — it is reached from `keyboard_setup()`, which QMK calls
earlier:

```c
int main(void) {
    platform_setup();     // halInit(): clocks, palInit, boardInit;  then chSysInit()
    protocol_setup();     // usb_device_state_init(), trivial
    keyboard_setup();     // matrix_setup(); keyboard_pre_init_quantum() -> keyboard_pre_init_kb()
    protocol_pre_init();  // init_usb_driver()
    keyboard_init();      // matrix_init(), RGB init, ...
    while (true) { ... housekeeping_task(); }
}
```

So the Escape check runs after the clocks are right and the kernel is up — both of which it
needs, one for the EEPROM divider and one for `wait_us()` — and **before** the USB driver, the
matrix and the LED driver. Everything this project is actually guessing at is downstream of it.

That matters because of what is in the USB driver. The shared LPC driver in ChibiOS-Contrib,
which has never run on this part, contains five unbounded spins:

```
usb_lld_disable_endpoints   while (LPC_USB->EPSKIP) {}
usb_lld_read_setup          while (usbp->setup_buffer == 0) {}
usb_lld_stall_out/in        while (LPC_USB->EPSKIP & skip_flag) {}
usb_lld_clear_out/in        while (LPC_USB->EPSKIP & skip_flag) {}
```

Several are reached from interrupt context during enumeration, and `read_setup`'s in
particular would spin forever if the endpoint memory were not where the driver thinks it is.
Those are exactly the hangs the Escape route has to survive, and it does, because it has
already run by then.

### The near miss, and the rule that came out of it

The firmware reached a state no tool in this project could reach: running
normally, never enumerating, and feeding its watchdog. Invisible and immortal.
The keyboard was dark, absent from the host's entire USB tree, and stayed that
way — it came back only because a power cycle happened to land in the bootloader.

Three things combined, and each looked reasonable on its own:

1. **Both software routes back called the same function.** The Escape check and
   the ten-second "USB never came up" timeout both go through
   `k100_enter_bootloader()`, which writes the EEPROM mode word. One failure
   took out both routes at once. That write had never been seen to succeed on
   hardware: every mode change this project has relied on was performed by
   Corsair's bootloader, not by our code.
2. **The fallback latched instead of resetting.** When the write failed, the old
   code set `usb_was_ready` and let the keyboard keep running, reasoning that
   this beat a reboot loop. For a keyboard whose only remaining lifeline is the
   watchdog, it is far worse.
3. **Fixing a bug removed the safety net.** Every failed experiment until then
   had been rescued by the watchdog, because the firmware died before feeding
   it. The moment the system-timer fix let the firmware actually run, it started
   feeding — and the net disappeared precisely when the firmware got better.

**A second rule, learned the hard way after the first one failed:** the Escape
check must be the *first* thing `keyboard_pre_init_kb()` does. It is the only
lever a person has on firmware that is already running — everything else this
project can do needs the keyboard to be answering on USB. A build that put two
diagnostic delays and a failing EEPROM write ahead of it moved the check about
fifty seconds into boot, then stalled before reaching it, and the keyboard could
not be recovered by any software means at all. Nothing goes above that check.

**The rule:** the last line of defence must not be the mechanism that has never
been proven. `housekeeping_task_kb()` now stops feeding the watchdog when
`k100_enter_bootloader()` fails, which reaches the bootloader through something
this project has repeatedly observed working — an application that dies soon
after handover makes the bootloader stay put — rather than through the EEPROM
write that has not.

A corollary worth keeping: **never flash an image whose only way back is
untested.** Prove `k100_enter_bootloader()` on hardware, from a firmware that is
otherwise known good, before relying on it again.

### The loss

The corollary above was written, committed, and then violated within the hour. The keyboard
did not survive it. This section is the record, because a recovery document that only lists
near misses teaches nothing.

The build that ended it was an attempt to test `k100_enter_bootloader()` on hardware — the
right experiment. It was arranged the wrong way round:

```
bl lpcWwdtFeed
mark 0 s
mark 10 s
bl k100_enter_bootloader        ← the untested call
mark 40 s
bl k100_matrix_configure_pins   ← the held-key check, ~50 s into boot
```

The call faulted on the powered-down EEPROM array instead of returning, so the held-key check
below it was never reached. One unproven mechanism, placed in front of the backup, removed
both ways back at once. The keyboard has not enumerated since.

**The experiment was fine; its placement was not.** Testing a recovery mechanism by making the
boot path depend on it means a failure costs the thing the mechanism was for. It belonged
*after* the held-key check, or in a build that still enumerated so that a failure cost nothing.

Three further facts were established afterwards, each by measurement rather than argument, and
each closing off a hoped-for escape:

- **The firmware never reaches USB.** The host's kernel log contains no enumeration attempt of
  any kind after the flash — not even a failed descriptor read. Since the EEPROM write loop is
  bounded at a million spins and would *return* if it merely failed, the access must fault.
- **There is no hardware-held bootloader entry.** Escape and the topmost macro key were each
  held through a verified power cycle for a full minute, with every vendor ID on the host
  watched at 20 ms resolution. The power cycle itself was timestamped by a USB stick in the
  keyboard's passthrough port, so a null result means the test ran rather than that it did not.
  Nothing enumerated.
- **The passthrough port is not a way in.** A stick in it reaches the host directly; the MCU is
  not in that path.

What remains is [`docs/handover.md`](handover.md), for someone with a programmer.

### The recovery

Done on 19 October 2026, the day after the loss, with a Raspberry Pi 4, four jumper wires and
no soldering. In order:

1. **The header was measured, not guessed.** Each of `J1`'s six pins was read with the Pi's
   internal pull-up and then its pull-down, repeated for 45 s per pin so that several of the
   part's eleven-second watchdog restarts fell inside each measurement. A level that never
   changes across a restart is a resistor on the board; one that changes is the chip driving
   the pin, and driving that in turn is the one way this could have damaged a board that was
   only soft-bricked. Pins 2 to 5 never changed, in about 1300 samples across some sixteen
   restarts.
2. **SWDIO and SWCLK were found by sweeping.** Twelve ordered pairs of those four pins, one
   second each. `J1` pin 3 and pin 2 answered with `DPIDR 0x2ba01477`.
3. **All 512 KB were read out**, twice, with the core halted and the passes compared. The first
   attempt was rejected by that comparison: one pass had failed to halt and was read from a
   part being reset underneath it.
4. **The boot ROM was caught impersonating the flash.** Halted straight out of reset, the
   bottom `0x200` bytes of the address space are the ROM's, so the vector table read back had
   every handler in `0x0300xxxx`. Reading the ROM from `0x03000000` and comparing proved it:
   identical for exactly `0x200` bytes, unrelated after. Re-reading with the part allowed to
   run first recovered the real vector table, whose word at `0x1c` is the two's complement of
   the seven before it — NXP's boot checksum, and the proof that the 64 KB image is complete.
5. **Flash sector 2 was erased**, and read back to confirm it. That removed the container
   header, so the bootloader had nothing to start.
6. **The bootloader stayed, and enumerated.** `bcdDevice=000a`, the original serial number.
   Which answers the question this document could not: it stays put when the application is
   gone, with no held key and no EEPROM word involved.
7. **Corsair's own update path did the rest.** `k100-bragi flash
   Heracles_Application_Firmware_v0.35.288.bin --commit` wrote 150908 bytes, applied it, and
   the keyboard rebooted into `SelfOperated` reporting firmware 0.35.288, `bcdDevice=0023`,
   three interfaces, and `physical layout Iso` — which only the application can answer.

Total writing done through the probe: one 32 KB erase.

One practical note. On the Pi's own USB port the bootloader enumerated, held for 32 seconds and
then dropped the port into `cannot reset (err = -71)`, from which it did not come back. The
keyboard is hungrier than a Pi 4's port likes. Doing the USB half on a properly powered host
made it uneventful.

### The window none of them covers

| Stage | Unbounded waits in it | Confidence |
| --- | --- | --- |
| `crt0`, `__cpu_init` | none | small, and now known to need the SRAM clock enable |
| `halInit()` — clocks, `palInit()` | two, both `lpc_set_divider` waiting on `REQFLAG` | FRO at 48 MHz straight to core and USB; the PLL lock wait is compiled out of this configuration. Every register field is checked by `chibios/test/check-registers.sh` and every pad mode by the PAL encoding check |
| `chSysInit()` | none | ChibiOS's own, not this project's |

That is the whole of it: a fault, or one of two divider waits, in code that is either trivial
or statically checked. A hang there is unrecoverable — as established above, a power cycle does
not help — but it is a much smaller target than the USB bring-up, which is covered.

One more hang mattered and has been removed. `lpcEepromWrite`'s completion poll was unbounded,
which quietly defeated the read-back guard around it: a flag that never arrives would hang the
recovery path itself rather than reporting that the write failed. It is now bounded, and the
function returns `false` instead of spinning.

## The firmware does run. It was the inherited watchdog.

**Corsair's bootloader starts the windowed watchdog before it hands control to an
application, and on this part that cannot be undone.** NXP's description of
`WDEN` is explicit: "once this bit is set to one and a watchdog feed is
performed, the watchdog timer will run permanently". So an application handed one
has no option but to feed it, for as long as it runs. This firmware did not, and
was reset about a second and a half into every boot.

### Why it looked like a rejection for so long

The failure mode is unusually good at lying. Firmware that does not feed an
inherited watchdog starts, runs briefly, and is reset. From the host that is
indistinguishable from a bootloader declining to start the image: the keyboard
reappears almost at once, still in bootloader mode, apparently never having run.

Worse, the obvious test agrees with the wrong answer. A long marker delay placed
at the earliest point any of this code runs still produced a short absence,
because the reset cut the marker short too. That was read as proof the firmware
never executed, and it was written into this document as such — twice.

What broke the deadlock was feeding the watchdog *while* marking time. The same
marker then ran to completion: **23 seconds of absence against the 2 seconds
every unfed attempt produced.** The firmware had been running the whole time.

The lesson worth keeping: a short absence proves nothing. It is equally
consistent with "never started" and with "started and killed", and only an
observation that survives the killing can tell them apart.

### What this cost, in wrong conclusions

Both of these were written here as fact and were not:

- that the firmware ran and its USB watchdog recovered it. It did not run that
  far; the nine seconds was an apply plus a bootloader reboot.
- that the bootloader was rejecting the image, and the search was for which field
  it disliked. Four hypotheses were tested and eliminated against that premise —
  version, build stamp, image length, and a payload starting with zeros — and all
  four were answering a question that was not being asked.

One good thing came out of that search, and it is worth keeping: an image
identical to Corsair's except for its version, **checksummed by this project
rather than by Corsair, boots.** So there is no signature. The container's CRC is
the only integrity check, custom firmware is permitted by this bootloader, and
`read-firmware` separately confirmed the written image is byte-exact in flash.

### How the watchdog is now serviced

Feeding is not a one-liner, because the longest unattended stretch of boot falls
between two of QMK's hooks: it starts the USB driver in `protocol_pre_init()`,
after `keyboard_pre_init_kb()` and before the first housekeeping call, and that
driver contains several unbounded waits. So:

| Where | Why |
| --- | --- |
| `early_hardware_init_pre()` | before `crt0` has even initialised RAM |
| `board_init()` | after `halInit()` configures the clocks |
| `keyboard_pre_init_kb()` | before QMK starts USB |
| a ChibiOS virtual timer, every 100 ms | covers USB bring-up and anything else that does not return to the main loop |
| `housekeeping_task_kb()` | every iteration thereafter |

The virtual timer has a cost worth stating: a watchdog fed from an interrupt no
longer rescues firmware that wedges with interrupts still running. That was a
pleasant side effect rather than a design, and the two deliberate routes back —
Escape at power-on and the USB watchdog — are unaffected.

### Boot, stage by stage, and the seven faults between them

The firmware now reaches QMK's `keyboard_post_init_kb`, which is the end of
`keyboard_init()`. Getting there meant finding seven separate faults, each of
which produced the *same* external symptom — the keyboard away for a few seconds
and back in bootloader mode — because the inherited watchdog resets the chip
within about a second and a half of anything going wrong.

The method that worked: hold the keyboard off the bus for a measurable number of
seconds at successive boot stages, **feeding the watchdog throughout**, and read
how far it got from the total absence. `K100_USB_DIAGNOSTIC` in `bootloader.c`
carries it. Markers tens of seconds apart are necessary; a few seconds apart is
lost in the variation of the apply and the bootloader's own boot.

The markers are deliberately loop-counted rather than timed by `wait_us()`, which
matters more than it sounds: for several rounds the thing being measured *was*
the timer, and a marker timed by it cannot report on it.

| Stage | Hook | Verdict |
| --- | --- | --- |
| before RAM init | `early_hardware_init_pre` | reached |
| after RAM init, before `main` | `__late_init` | reached |
| end of `halInit` | `board_init` | reached, **after four fixes** |
| after `chSysInit` | `matrix_setup` | reached |
| after QMK starts USB | `keyboard_pre_init_kb` | reached, **after one more fix** |
| end of `keyboard_init` | `keyboard_post_init_kb` | reached, **after one more fix** |
| first scan of the main loop | `matrix_scan_custom` | **not reached** |
| end of the first loop pass | `housekeeping_task_kb` | **not reached** |

One ordering fact is worth stating plainly, because getting it wrong sent this
search into the wrong driver for several rounds. In `quantum/main.c`,
`protocol_pre_init()` — which is where QMK starts the USB driver — runs *before*
`keyboard_init()`, not after it. So `keyboard_pre_init_kb` being reached already
meant USB initialisation had completed, and the failure was never inside it.

The measured absences, against a working image's 0.76 s: 76.67 s for the build
whose markers summed to 70 s at `keyboard_post_init_kb`, and 8.15 s — the
no-marker failure baseline — for the builds marking the first matrix scan and the
first housekeeping call. Two independent bus recordings agreed on each.

#### The confound in reading those totals

A total of absence is only a sum of markers if each marker ran **once**, and on
this keyboard that does not follow. A firmware the bootloader cannot start is
*retried*, so every marker runs again on every attempt — the 8.15 s failure
baseline is itself several attempts of a firmware that dies in about a second and
a half. 76 s therefore reads equally well as 70 s of markers once, or 30 s of
markers two and a half times, and those name different stages.

Changing a marker that sits *after* the failure point cannot separate them, which
is why two runs agreeing on 76 s proved less than it looked: the value changed
between them, 60 s at the housekeeping call and then 80 s at the first matrix
scan, was past the failure either way.

What separates them is making the markers run on the first boot only, which needs
something that survives a reset to count boots in — see the note on `ram1` in the
linker script, and `DIAGNOSTIC_SURVIVOR` in the keyboard's `bootloader.c`. That
counter is itself only trustworthy if the area really does retain, so it is
checked directly rather than assumed: a build whose *second* boot alone pauses
produces a pause the keyboard cannot produce any other way.

What had to be fixed to get that far:

1. **The inherited watchdog was never fed.** Covered above; it is also what made
   every other fault look identical.
2. **`chSysPolledDelayX` before `chSysInit`.** Used for the USB PHY settling
   delay. It counts cycles with the DWT, which ChibiOS only enables in
   `chSysInit()` — and the USB clock setup is reached from `halInit()`, earlier.
   The counter is frozen, so the delay never ends. Replaced with a plain loop.
3. **The DWT is not accessible at all on this part.** ChibiOS's `port_init()`
   enables the cycle counter unconditionally, and the read-modify-write of
   `DWT->CTRL` faults — Corsair ships this chip with debug access disabled. This
   is what stopped `chSysInit()` dead. Fixed by
   `chibios/patches/0002-optional-dwt-enable.patch` and `CORTEX_ENABLE_DWT FALSE`
   in the keyboard's `chconf.h`. Located by performing exactly those two writes
   inside `board_init`, with a marker on each side of them.
4. **Flash wait states were set for the target clock before the clock changed.**
   This was the one that unblocked `halInit`. The bootloader hands over with the
   core at roughly 180 MHz and this configuration drops it to 48; setting 48 MHz
   wait states while still running at 180 means instruction fetches return
   garbage. The requirement reverses depending on which way the clock is going,
   and an application cannot satisfy both by writing the final value once,
   because it does not know what it inherited. `lpc_clock_init()` now goes to the
   slowest setting, changes the clock, and only then drops to the target.
5. **A periodic watchdog feed on a virtual timer crashed when armed**, and was
   never needed: the stretch it was meant to cover is microseconds against a
   watchdog period of about a second and a half. Removed. Its callback also had a
   nested `chSysLockFromISR()`, since ChibiOS calls a timer callback with the
   kernel already locked.
6. **The USB PHY and its PLL were powered in the wrong register.** They are
   `PDRUNCFG1` bits 0 and 1; `VD5` is `PDRUNCFG0` bit 28. NXP's header names
   every bit `SYSCON_PDRUNCFG_PDEN_*` with no register index, and the collision
   is not harmless — bits 0 and 1 of `PDRUNCFG0` are unrelated blocks, and bit 16
   is `RAM3` rather than anything to do with USB. **This is the third instance of
   that trap on this part**, after `AHBCLKCTRL`; treat every
   `SYSCON_<REG>_<FIELD>` macro as needing an index from somewhere else, and get
   it from NXP's clock and power drivers, which do carry it.
7. **Turning the DWT off took the realtime counter with it, and every
   microsecond delay in the firmware with that.** Fault 3 disabled the cycle
   counter because touching it faults. But ChibiOS's realtime counter *is* that
   cycle counter, and on this platform QMK's `wait_us()` is
   `chSysPolledDelayX()`, which samples the counter, computes an end value and
   spins until the counter passes it. Against a counter that never advances it
   does not run short — it never returns. The first `wait_us()` in boot is the
   settling delay of the first matrix read, called from the Escape check in
   `keyboard_pre_init_kb`, so the firmware hung there and the watchdog reset it.

   Fixed by giving the platform a counter of its own: the RIT, a free-running AHB
   counter which with `AHBCLKDIV` at one ticks at exactly the rate `CYCCNT`
   would, so `REALTIME_COUNTER_CLOCK` needs no adjusting. See
   `chibios/os/hal/ports/LPC/LPC546xx/lpc546xx_rit.c` and
   `chibios/patches/0007-external-realtime-counter.patch`, which routes
   `port_rt_get_counter_value()` to it behind `PORT_USE_EXTERNAL_RT_COUNTER`.

   Two things about this one are worth carrying forward. The gate is a **fourth**
   instance of the index trap: `SYSCON_AHBCLKCTRL_RIT_MASK` is bit 1, and bit 1
   of `AHBCLKCTRL0` is `ROM` — the register is `AHBCLKCTRL1`, per
   `kCLOCK_Rit = CLK_GATE_DEFINE(AHB_CLK_CTRL1, 1)` in NXP's clock driver. And
   the failure mode is bad enough to be worth a build-time guard, so
   `tools/check-firmware-image.py` now refuses an image that references the DWT
   and requires one that references the RIT — the second half being the control
   that stops the first from passing vacuously.

### Where it still fails

Between `keyboard_post_init_kb()` and the first `matrix_scan_custom()`. That gap
is tiny: `keyboard_post_init_user()`, which compiles to `bx lr`, the return
through `keyboard_init()`, `protocol_post_init()`, which sets one function
pointer, and the start of the first loop pass. `matrix_can_read()` compiles to
`movs r0, #1`, so `matrix_scan_custom()` is three instructions from the top of
`keyboard_task()`.

**Correction: the stage above is wrong, and the table below records how.** The
marker lengths are not seconds. The inner loop was assumed to cost about five
core cycles an iteration and costs roughly ten, so a marker asking for *n*
seconds holds the bus for about 2.28n at 48 MHz. Decoded with that factor, the
steady 76.7 s absence is `matrix_setup` plus `keyboard_pre_init_kb` only -- 30
nominal seconds -- and `keyboard_post_init_kb` never runs at all.

The error was caught by a control that should have been run much earlier:
lengthening one marker and checking the total grows by the same amount. Changing
the post-init marker from 40 to 60 moved the total by nothing, 76.73 s to
76.71 s, which is only possible if that marker never executes. Three further
cycles in another recording gave 76.73, 76.72 and 76.71 -- a figure stable to
twenty milliseconds, which is itself the clue that it is two fixed markers
rather than a sum that includes a third.

The same factor, measured against the boot-count probe, gives the clock the
bootloader hands over at: the identical loop runs 3.7 times faster before
`halInit()` than after, which is an independent confirmation of the ~180 MHz
figure deduced earlier from a completely different measurement.

So the failure is inside `keyboard_init()`, between the Escape check and the end
of that function, and everything below about the gap after `keyboard_init()`
describes a stage boot never gets to.

It is a **hang**, not slowness and not a fault. Both of those were tested rather
than argued:

- **Not a fault.** All four fault vectors in a stock image point at ChibiOS's
  unhandled-exception stub, which is an infinite loop, so a fault would look
  exactly like a hang from the bus. A build that caught hard, memory-management,
  bus and usage faults and reported the class by marker length reported none:
  8.21 s, the bare baseline.
- **Not the watchdog.** `lpcWwdtStretch()` raises the inherited watchdog's
  timeout constant to its maximum, turning roughly a second and a half of grace
  into a couple of minutes. Boot still never reached the main loop — the
  keyboard stayed off the bus for over 145 s, where a firmware that reaches
  `housekeeping_task_kb()` hands itself back after ten. The build also marked a
  distinctive length if the part refused the write, and it did not refuse.
- **Not the USB interrupt.** Masking it at `keyboard_post_init_kb()` changed
  nothing, 8.21 s again.

Other things ruled out while looking:

| Suspected | Why it is not that |
| --- | --- |
| `protocol_pre_task()`'s suspend loop | compiled out by `NO_USB_STARTUP_CHECK`; `protocol_pre_task` is now a single tail call, and the absence was 8.15 s before and 8.16 s after |
| the USB event queue refilling faster than it drains | masking the interrupt makes refilling impossible, and nothing changed |
| `usb_idle_task()` | fixed-length loops, and every send is guarded on `USB_ACTIVE` |
| QMK's EEPROM config writes | this build uses the transient backend: `eeprom_write_block` is a `memcpy` into a 40-byte RAM buffer |
| the LED driver | `rgb_matrix` is off in `keyboard.json`, so none of the SPI path runs in the loop |

What has not yet been reconciled is an apparent contradiction between two builds.
One, with markers summing to 70 s, reports that `keyboard_post_init_kb()` is
reached. Another, with a marker immediately after `keyboard_init()` returns,
reports that that point is not reached — and the code between them cannot hang.
The difference between the builds is that the first one's markers feed the
watchdog continuously and the second one's do not, which is the next thing to
settle: markers that run right up to each point remove that difference.

### The cause: the kernel's clock never advances

`CH_CFG_ST_TIMEDELTA` is 2 in QMK's shared ChibiOS configuration, which asks for
a **free-running** system timer with a comparator. The shared LPC system-timer
driver cannot provide one. It is SysTick: a 24-bit *down*-counter with no
compare register. Its free-running entry points say as much if you read them --
`st_lld_get_counter()` returns `SysTick->VAL`, which counts downwards and wraps
at `LOAD` instead of increasing monotonically, and `st_lld_set_alarm()` writes
that same current-count register as though it were a comparator.

The driver knows it cannot do this, and tries to refuse:

```c
#if OSAL_ST_MODE_PERIODIC == OSAL_ST_MODE_FREERUNNING
  #error "OSAL_ST_MODE_FREERUNNING is not supported. Check CH_CFG_ST_TIMEDELTA"
#endif
```

That compares the two mode *constants*, 1 and 2, rather than comparing the mode
in force against one of them. It is never true, the `#error` never fires, and a
configuration the driver cannot implement builds cleanly. What comes out is a
kernel whose clock does not advance and in which every `chThdSleep()` blocks for
ever.

**The fix** is `CH_CFG_ST_TIMEDELTA 0` in the keyboard's `chconf.h`, which
selects the periodic mode SysTick can actually do, with `CH_CFG_ST_FREQUENCY`
at 10 kHz. `chibios/patches/0008-systick-freerunning-guard.patch` corrects the
guard so the next person is told rather than having to find this.

Why it presented as a USB fault for so long: nothing in a QMK boot sleeps until
`init_usb_driver()` does, so that is where the firmware always stopped.
`wait_us()` kept working throughout, because it polls the RIT and never involves
the scheduler, and `escape_held()` uses it — so the matrix read before USB
worked perfectly while everything after it did not.

Proved before fixing, by putting `chThdSleepMilliseconds(1)` immediately after
the Escape check with a marker on the far side of it: the marker never ran. That
probe depends on nothing USB-related at all.

### How the markers can lie, and what was done about it

Three ways, all of which produced a wrong reading at least once:

1. **Repeats.** A total of absence is a sum of markers only if each ran once.
   That depends on how often the bootloader restarts an image it cannot run,
   which was never measured until now. It is **once**: a build with a single
   unguarded 10 s mark in `early_hardware_init_pre()` was absent 14.31 s against
   a baseline of 8.15 s, and one mark there is worth about 6.1 s.
2. **A guard that suppressed them.** The first-boot guard written to remove (1)
   counted boots in SRAMX, which survives a *reflash* as well as a reset, so the
   count carried over between experiments and the markers silently vanished —
   reporting the baseline, which reads as "this stage is not reached". A
   measurement that fails by looking like a result is the worst kind. Removed
   once the retry count was known to be one.
3. **Instruments that were never wired.** `CH_CFG_SYSTEM_TICK_HOOK` was used to
   feed the watchdog from the tick; QMK sets `CH_CFG_ST_TIMEDELTA 2`, so ChibiOS
   runs tickless and that hook is never called. The build succeeded and the test
   was inert. It was caught by looking for the feed sequence in the image and
   finding only one, inside `lpcWwdtFeed` itself.

The lesson each time is the same: check that the instrument can produce the
signal before believing an absence of it. The image checks in
`tools/check-firmware-image.py` are built the same way, requiring a positive
control alongside each absence.

## Reading the result of a custom flash

**`k100-bragi` timing out afterwards is the expected outcome, not a brick.** QMK does not
implement Bragi, so once custom firmware runs there is no vendor interface to answer. Judging
the flash by whether `info` still works would read every success as a failure.

What to look at instead is `bcdDevice`, which each of the three programs sets differently and
which needs no command sent at all:

| `bcdDevice` | Running |
| --- | --- |
| `0.23` | Corsair's application, firmware 0.35.288 |
| `0.0a` | Corsair's bootloader, 0.10.45 |
| `0.01` | **this project's firmware** — `DEVICE_VER 0x0001` from `keyboard.json` |

The USB vendor and product ids stay `1b1c:1b7c` in all three cases, deliberately: the udev
rules keep working and nothing on the host has to be reconfigured. `bcdDevice` is the only
field that distinguishes them.

So, after writing a custom image:

1. `lsusb -v -d 1b1c:1b7c | grep bcdDevice` — `0.01` means it booted.
2. Type something, and read `bcdDevice` again in the same breath. **Corsair's bootloader
   presents a working keyboard of its own**, so keys arriving proves only that *something* is
   scanning the matrix. A whole keyboard was swept this way, 104 distinct keys reported, and
   the firmware under test had already been gone for minutes: the device on the bus was
   `bcdDevice 0.0a` with Corsair's product string, and the evidence was worth nothing. Keys
   that arrive *while* `bcdDevice` reads `0.01` mean the matrix guesses were right; keys that
   stick or report their neighbours mean `MATRIX_SENSE_ACTIVE_LOW` or
   `MATRIX_STROBE_SETTLE_US` need the other value, which is a reflash, not a brick.
3. Nothing at all after ten seconds means the watchdog should already have put it back in the
   bootloader; check for `bcdDevice 0.0a`.

### How long a working boot takes, measured

Worth knowing, because the whole diagnostic method in this project is reading lengths of
absence from the bus, and a length only means something against a baseline.

Watching the bus across an apply of Corsair's own 0.35.288 — written *from bootloader mode*,
which is the slower of the two routes — the keyboard was gone for **0.76 s** and came back as
`bcdDevice=0023`, the running application.

Two things follow:

- **An apply does start the image it just wrote**, including from bootloader mode. The
  operating mode reported afterwards is therefore an accept/reject oracle: `SelfOperated` means
  the image was started and came up, `Bootloader` means the bootloader is what answered, so the
  image was started and did not. The mode is read from whichever program replies, not from a
  stored word, which is why it can be read this way at all.
- **Under a second is the scale of a good boot.** So an absence of several seconds already
  means something ran and stopped, and the useful markers are tens of seconds — far enough
  apart that the two or three seconds of variation in an apply and a bootloader start cannot be
  mistaken for a stage.

A custom image that is started and fails shows up as a longer absence ending in `bcdDevice=0a`.
One observed case: 8.15 s away, then the bootloader, and then no further bus activity for the
next 115 s — so whatever happened settled rather than looping. What fills those 8 s is not
established. Several starts of an image that dies to the watchdog in about a second and a half
would account for it, and so would one start that survived longer; the measurement does not
separate them, and nothing yet depends on which it is.

## The unknown

**Whether ECRP is set on the shipped part.** NXP community threads describe LPC546xx devices
shipping with SWD access prohibited and ECRP configured such that a failed MSC programming
attempt permanently bricks the part. If Corsair set that, layers 2 and 3 are gone and layer 1
is all we have.

How much this matters depends entirely on rule 1. With the bootloader intact, layer 1 is
sufficient and ECRP is irrelevant. ECRP only becomes decisive if the bootloader is destroyed,
which only happens if rule 1 is broken.

Worth determining early anyway, since it tells us how much margin we actually have.

## One residual risk worth naming

Layer 1 assumes the bootloader runs on every power-on and can be commanded into staying put,
even when the application image is garbage. That is the normal design, and a CRC-validating
bootloader should refuse to jump into a bad image — the whole format is CRC-based, which is
consistent with that.

But we have not proven it. If the bootloader instead jumps blindly into a corrupt
application, the device could hang before any Bragi command could be delivered, and recovery
would fall to layer 2 and therefore to ECRP.

Mitigation: rule 2 establishes that the bootloader can be entered and exited reliably, and
the first custom image should be one already proven to run on the LPCXpresso devboard rather
than a first attempt written blind.

**The devboard half of that mitigation is not in place.** No LPC546xx hardware other than the
keyboard itself is available to this project, so the first custom image written to it will be
one that has never executed anywhere. Nothing in this repository changes that, and no amount
of static checking substitutes for it. It is recorded here rather than left implicit because
it is the largest single risk the project carries, and it is a deliberate choice rather than
an oversight.

What is in place instead:

- the container writer reproduces both official images byte for byte, so the image the
  bootloader receives is well formed by demonstration rather than by argument;
- the recovery loop in rule 2 is run with Corsair's own images *first*, which proves
  enter → write → apply → exit end to end before a custom image is written, and leaves a
  known-good image in the slot if the write path turns out to be broken;
- the application is linked at `0x00010400`, past the bootloader, so a bad application cannot
  overwrite the thing that recovers it.
