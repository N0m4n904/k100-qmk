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
