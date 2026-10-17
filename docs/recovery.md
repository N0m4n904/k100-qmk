# Can the K100 be unbricked?

**Yes, under two rules, with one unknown that only matters if those rules are broken.**

This is the document to re-read before any write to the keyboard. Evidence is quoted from
Corsair's own updater bundle and from ckb-next; see `firmware-format.md` and
`bragi-firmware-update.md` for how those were obtained.

## Three independent layers of recovery

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

Same idea, same ECRP caveat, needs pads located and a probe attached.

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

That is the whole mechanism. **The mode lives in EEPROM, and the chip soft-resets into the
bootloader, which reads it back.** The consequence that matters: any firmware that can write
that EEPROM word can enter the bootloader. It does **not** require Corsair's application, the
Bragi protocol, or a working USB stack.

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
  firmware that hangs cannot be recovered by unplugging it, and the "hold Escape at power-on"
  route works because the *firmware* checks Escape, not because the bootloader does.
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

### Where the Escape check sits, and what it therefore covers

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

## The firmware runs, and recovered itself — measured

Custom firmware was written to the keyboard and the bus watched while it booted. The device
left the bus, stayed off it for about nine seconds, and then the **bootloader** came back:

```
15:08:45  bcdDevice 000a     bootloader, before the write
15:08:48  ABSENT             reset into the new firmware
15:08:57  bcdDevice 000a     bootloader again
```

Nine seconds is the USB watchdog's ten. Nothing else in the system takes that long, and the
only code that can put the keyboard back into the bootloader is this firmware's own. So the
gap is a measurement of how far boot got, and it got all the way:

- the bootloader branched to the entry in the container header;
- `crt0` ran, installed `VTOR`, and `__cpu_init` clocked SRAM1 to SRAM3 before the 160 KB of
  `ram0` was zeroed;
- `halInit()` configured the clocks — the one step in the unprotected window that had never
  executed — and `palInit()` and `chSysInit()` followed;
- the Escape check ran and correctly decided Escape was not held;
- USB was started, and did not enumerate;
- **the main loop turned**, since `housekeeping_task_kb` is what counts the ten seconds;
- the EEPROM mode word was programmed and `NVIC_SystemReset` taken.

That last point is the one worth dwelling on: **the recovery path is no longer theoretical.**
The EEPROM write, the read-back, the reset and the bootloader's reading of the mode word have
all now happened on hardware, unattended, in exactly the situation they exist for.

What does not work is USB enumeration. That is now the whole of the problem, and it is a
problem with the keyboard reachable and reflashable rather than a brick.

### The first attempt also found a real bug

Before this, the same image returned to the bootloader after about *three* seconds rather than
nine — two boot cycles rather than one plus a timeout. The cause was the Escape check asking
only whether Escape differed from the *majority* of its row. A reading that is simply wrong
scatters bits arbitrarily, and Escape lands on the minority side of that about half the time,
so the check fired on a keyboard nobody was touching. It now requires every other populated
sense in the row to agree unanimously, across four consecutive samples, which a garbage read
fails on the first one.

It is worth noting what that near-miss looked like: firmware that silently put itself into the
bootloader on every boot, indistinguishable from firmware that would not run at all.

### Writing from the bootloader uses 64-byte reports, and that works too

The second write went out as **392 chunks** where the first, from host-controlled mode, took
24. Same image, same code: the report size comes from the endpoint descriptor, which is 64
bytes in the bootloader and 1024 in the application. Both paths are now exercised against
hardware.

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
2. Type something. Two HID interfaces and working keys means the matrix guesses were right
   too; keys that stick or report their neighbours mean `MATRIX_SENSE_ACTIVE_LOW` or
   `MATRIX_STROBE_SETTLE_US` need the other value, which is a reflash, not a brick.
3. Nothing at all after ten seconds means the watchdog should already have put it back in the
   bootloader; check for `bcdDevice 0.0a`.

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
