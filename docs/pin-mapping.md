# Key matrix pin mapping, from firmware static analysis

Recovered by disassembling Corsair's official K100 OPX firmware
(`Heracles_Application_Firmware_v0.35.288`, PID `0x1b7c`). **No hardware was touched and the
case was never opened.**

Peripheral addresses are from NXP's own CMSIS header for the exact part
(`mcux-sdk/devices/LPC54605/LPC54605.h`), not from memory.

## Establishing the load address

Needed first, or no disassembly means anything.

```
application is linked at flash 0x00010000
flash address A  <->  file offset A - 0x10000
```

The first 64 KB of flash is therefore not part of this image, consistent with the bootloader
living there as its own independently versioned region — see `recovery.md`.

### How it was settled, and a method note

A first attempt scored candidate bases by whether internal function pointers landed on Thumb
prologues. It favoured base `0x00000000` at 29.3%, and **that conclusion was wrong.** The
test cannot discriminate here: base `0x00010000` scored 27.3% on a different pointer
population, close enough to be noise. It was recorded as settled on far too little margin.

The decisive test is string pointers, because a hit is unambiguous:

| Candidate flash base | Aligned pointers resolving to a valid NUL-terminated string |
| --- | --- |
| `0x00000000` | 15 / 1199 — 1.3%, every hit a misaligned fragment such as `", taskQueueSet) == pdPASS"` |
| **`0x00010000`** | **272 / 635 — 42.8%**, every hit a clean symbol: `ActionProcessTimer`, `processSemaphore`, `loadKeyAssignmentsSemaphore`, `dialStopSemaphore` |

Independently corroborated by the mask table pointers below: they are `0x0002FD04` …
`0x0002FD84` for data sitting at file offsets `0x1FD04` … `0x1FD84` — exactly `0x10000` apart.

The pin-mapping conclusions below were unaffected, because they rest on instruction *content*
— register offsets, IOCON word indices and immediates — not on absolute addresses. Only the
quoted addresses of the functions themselves changed; they are given below as flash addresses
under the corrected base.

## The matrix

**8 strobe outputs and 18 sense inputs, all on GPIO port 1.**

| Role | Pins | Count |
| --- | --- | --- |
| Strobe / drive | **P1_18 .. P1_25** | 8 |
| Sense / read | **P1_0 .. P1_17** | 18 |

8 × 18 = 144 positions for 123 keys, the remainder masked off.

### Initialisation, at flash `0x17210`

```asm
ldr   r3, [pc]              ; r3 = IOCON  (0x40001000)
mov.w r2, #0x300
str.w r2, [r3, #0xc8]       ; IOCON word 50 = P1_18
str.w r2, [r3, #0xcc]       ; ... through
str.w r2, [r3, #0xe4]       ; IOCON word 57 = P1_25
ldr   r2, [pc]              ; r2 = GPIO (0x4008C000)
ldr   r1, [r2, r0]          ; r0 = 0x2004 -> GPIO->DIR[1]
orr.w r1, r1, #0x3fc0000    ; bits 18..25
str   r1, [r2, r0]          ; P1_18..P1_25 = output
ldr   r1, [r2, #0x2204]     ; GPIO->SET[1]
orr.w r1, r1, #0x3fc0000
str   r1, [r2, #0x2204]     ; drive them high -- the idle, unasserted state
...
cmp   r2, #1                ; flag at 0x20000158
moveq r0, #0x100            ; digital, no pull
movne r0, #0x120            ; digital, pull-up
; loop writes r0 to IOCON words 32..49  = P1_0 .. P1_17
ldr   r3, [r1, #0x2004]
lsrs  r3, r3, #18
lsls  r3, r3, #18           ; clear DIR[1] bits 0..17 -> P1_0..P1_17 = input
```

IOCON word index is `port * 32 + pin`, so words 50–57 are P1_18–P1_25 and words 32–49 are
P1_0–P1_17.

IOCON values decode as:

| Value | Meaning |
| --- | --- |
| `0x300` | FUNC0, digital, **input filter off**, no pull — the strobes |
| `0x100` | FUNC0, digital, no pull — the senses |
| `0x120` | FUNC0, digital, **pull-up** — the senses, alternate mode |

Decoded against NXP's `IOCON_PIO_*` field masks, not by eye. On this part the fields sit at
`FUNC` 3:0, `MODE` 5:4, `I2CSLEW` 6, `INVERT` 7, `DIGIMODE` 8, `FILTEROFF` 9, `SLEW` 10 and
`OD` 11, so `0x300` is bits 8 and 9 — digital with the input glitch filter disabled.

**An earlier version of this file read `0x300` as open-drain. It is not.** Open-drain is bit 11,
`0x800`, and it is not set on any matrix pin: the strobes are ordinary push-pull outputs. The
mistake came from reading the bit positions off the LPC11Uxx sibling, where the fields sit
elsewhere — the same trap that made the PAL driver family-local, see `chibios/README.md`.

Two cross-checks fall out of this. `0x120` is bit-for-bit what this project's independently
written PAL driver produces for `PAL_MODE_INPUT_PULLUP`, derived from UM10912 rather than from
this disassembly. And `0x300` is exactly `PAL_MODE_OUTPUT_PUSHPULL | MODE_FILTER_OFF`. Two
unrelated derivations agreeing is the strongest evidence either of them is right.

The pull-up choice is runtime-selectable via a byte at SRAM `0x20000158`.

### The scan loop, at flash `0x170b8`

A four-state machine dispatched through a `tbb` jump table. The sampling state:

```asm
movw  r3, #0x2104           ; GPIO->PIN[1]
ldr   r4, [r1, r3]          ; read all 32 port-1 pins
ubfx  r4, r4, #0, #18       ; keep bits 0..17  -> 18 sense lines
str.w r4, [r3, r5, lsl #2]  ; samples[strobe] = sensed
ldr.w r6, [r6, r7, lsl #2]  ; mask[strobe]
ands  r4, r6                ; mask off non-existent positions
str.w r4, [r3, r5, lsl #2]
ldr   r4, [r2]
cmp   r4, #7                ; strobe index range 0..7
bhi   ...
adds  r4, #18               ; pin = strobe + 18
add   r1, r4
movs  r4, #1
strb.w r4, [r1, #32]        ; GPIO->B[1][pin] = 1, deassert the strobe
isb   sy
```

Three independent confirmations of the geometry fall out of this: `ubfx #0, #18` fixes the
sense width at 18, `cmp #7` fixes the strobe count at 8, and `adds #18` fixes the strobe pins
as P1_18 onward.

Runtime state, all in SRAM:

| Address | Contents |
| --- | --- |
| `0x20000158` | pull-up mode flag |
| `0x20000159` | scan state machine index |
| `0x20001300` | per-strobe valid-position mask, 8 words |
| `0x20001328` | second 8-entry table, purpose unidentified |
| `0x2000134C` | sample buffer, 8 words |
| `0x20001374` | current strobe index |

## The valid-position masks — one per physical layout

The SRAM mask at `0x20001300` is filled at init by a routine at flash `0x1758C`, which copies
**exactly 32 bytes — 8 words** from a pointer returned by the selector at flash `0x16BFC`:

```asm
bl    0x16bfc              ; returns the mask base for this board's layout
ldr   r5, [pc, #84]        ; r5 = 0x20001300
add.w ip, r0, #32          ; copy 8 words
ldr   r0, [r3, #0]
ldr   r1, [r3, #4]
stmia r2!, {r0, r1}
```

The selector holds **five** mask pointers, spaced exactly `0x20` apart:

| Pointer | File offset | Popcount |
| --- | --- | --- |
| `0x0002FD04` | `0x1FD04` | 120 |
| `0x0002FD24` | `0x1FD24` | **123** |
| `0x0002FD44` | `0x1FD44` | 122 |
| `0x0002FD64` | `0x1FD64` | 119 |
| `0x0002FD84` | `0x1FD84` | 118 |

Five is exactly the number of physical layouts ckb-next documents in
`src/daemon/structures.h`: `LAYOUT_ANSI 1`, `LAYOUT_ISO 2`, `LAYOUT_ABNT 3`, `LAYOUT_JIS 4`,
`LAYOUT_DUBEOLSIK 5`.

Every word uses only bits 0–17, matching the 18 sense lines, and each group is 8 words,
matching the 8 strobes. The popcount is therefore the key count for that layout.

### Which group is ours — settled, and my earlier guess was wrong

I previously inferred `0x0002FD24` (popcount 123) was the ISO mask because 123 matched
ckb-next's ISO scan-key count. **That was wrong.** It was flagged here as an inference rather
than a proof, and it did not survive evidence.

The firmware also contains `HidKeyCodes` (below), which holds every physical layout's keys at
once. Because a handful of HID usage codes exist only on one layout, intersecting each mask
with that table identifies the masks unambiguously:

| Mask | Positions | Marker keys it enables | Layout |
| --- | --- | --- | --- |
| `0x2FD84` | 118 | ANSI backslash only | **ANSI** |
| **`0x2FD64`** | **119** | **ISO hash + ISO non-US backslash** | **ISO** |
| `0x2FD44` | 122 | ISO hash + all four JIS keys | **JIS** |
| `0x2FD24` | 123 | ANSI + ISO hash + all four JIS | a superset |
| `0x2FD04` | 120 | ANSI + ISO hash + ISO backslash | a superset |

The markers are `0x31` ANSI backslash, `0x32` ISO hash, `0x64` ISO non-US backslash, and
`0x87`/`0x89`/`0x8A`/`0x8B` for JIS `ro`, `yen`, `henkan` and `muhenkan`. An ISO board must
have the hash and non-US backslash and must *not* have the ANSI backslash or any JIS key, and
exactly one mask satisfies that.

**The ISO mask is `0x0002FD64`, with 119 positions — not the 123-position group.**

Why the popcount reasoning failed: a mask counts *matrix positions*, and 15 of them are keys
with no HID usage code at all (the macro keys, media keys and wheel). ckb-next's 123 counts
scan keys from its own geometry, which is a different quantity. The two were never comparable.

### What the layout variants actually cost

The controller **was** designed for layout variants — five masks, matching ckb-next's five
physical layouts — but the variation is only the familiar layout keys, with nothing
speculative provisioned.

| Mask | Positions |
| --- | --- |
| ANSI | 118 |
| ISO | 119 |
| JIS | 122 |
| the two others (ABNT, Dubeolsik) | 120 and 123 |
| **union of all five** | **124** |

Beyond an ISO board the union adds exactly five positions: `KC_BSLS` (the ANSI backslash) and
the four JIS keys `KC_INT1`, `KC_INT3`, `KC_INT4` and `KC_INT5`. ISO in turn has two that ANSI
lacks, `KC_NUHS` and `KC_NUBS`, so **ANSI has one key fewer than ISO, not more** — and
therefore one fewer key LED.

Two checks worth recording because they came back clean:

- **No position anywhere carries a HID code or Bragi id that no mask enables.** There is no
  provisioned-but-unused key, so nothing was wired for a variant that never shipped.
- **The numpad is identical on every layout.** All seventeen populated positions on strobe 6
  are enabled by all five masks. Strobe 6 sense 17 is enabled by none of them and has no
  keycode, so it is plain unused matrix space rather than a reserved variant. In particular
  there is **no provision for a split zero key**.

The 20 positions of the 144 that no layout uses are ordinary headroom in an 8x18 grid, not
reserved for anything identifiable.

### `HidKeyCodes`: the position-to-key table

Found at flash **`0x0002FEBD`**, **144 bytes = 8 strobes x 18 senses**, indexed by
`strobe * 18 + sense` and holding each position's HID usage code. Zero means no HID code.

It was located from the firmware's own assert strings. `keyToHidKeycode` in
`keyToHidCodeMap.c` asserts `key < ELEMENTS(HidKeyCodes)`, and the function ends:

```asm
ldr  r3, [pc, #44]     ; r3 = 0x0002FEBD
ldrb r0, [r3, r4]      ; return HidKeyCodes[key]
```

The same function reveals the firmware's internal layout numbering in passing: it remaps
`0x65` to `0x88` and `0x32` to `0x89` when the layout getter returns **3**, which are
Katakana and Yen substitutions, so internal layout 3 is JIS.

A sibling table `ProtocolKeyToHidCode` sits at flash `0x0002FF4D` with 138 entries, bounds
checked at 137, mapping Bragi protocol key indices to HID codes.

### The second table names the rest

A sibling 144-byte table at flash **`0x0002FDA4`**, same `strobe * 18 + sense` shape, carries
a **Bragi key index** for exactly the positions `HidKeyCodes` leaves at zero. It is
byte-identical in 0.30.257 and 0.35.288, so it is stable across firmware versions.

Cross-referenced against ckb-next's `keymap_bragi`, that names all fifteen:

| Positions | Bragi ids | Keys |
| --- | --- | --- |
| strobes 0-5, sense 0 | 131-136 | **G1-G6** |
| strobe 5, sense 12 | 122 | **Fn** |
| strobe 7, senses 0-3 | 128, 137, 114, 102 | Profile, wheel button, Win lock, Mute |
| strobe 7, senses 13-16 | 123, 126, 124, 125 | Stop, Prev, Play, Next |

Six consecutive ids on six consecutive strobes at sense 0 is what confirms G1-G6; previously
that was only a reading of the shape.

**The ISO matrix is now completely identified: all 119 positions.**

### Both tables are stable across firmware versions

Located by signature in each image and compared:

| Table | in 0.30.257 | in 0.35.288 | sha256 of 144 bytes |
| --- | --- | --- | --- |
| Bragi key ids | `0x2F264` | `0x2FDA4` | `9b54dc6d97ff…` |
| `HidKeyCodes` | `0x2F37D` | `0x2FEBD` | `1347150ae6f9…` |

Byte-identical in both, with only their addresses moving between builds. The two images are
nearly three years apart (December 2020 and August 2023), so the matrix has not changed in
that span — unsurprising, since it describes the hardware.

That matters for this analysis: the tables were read from 0.35.288 while the unit on hand
runs 0.32.6, a version that sits between the two compared here. The mapping can therefore be
relied on for that unit without reading its flash.

Note the signature run occurs in **both** tables, so a signature search alone picks the wrong
one. `HidKeyCodes` is identified by the literal the disassembly of `keyToHidKeycode` loads,
not by searching.

### The four keys that are not matrix-scanned

The generated logical layout has 123 keys and the matrix has 119 positions. Subtracting the
named sets leaves exactly four: **`ctrlwheelccw`, `ctrlwheelcw`, `volup` and `voldn`**.

Those are the two rotary controls — the iCUE dial and the volume roller. Neither rotation is
a matrix key, which is consistent with the firmware having a `dialAssignmentMutex` and a
`dialStopSemaphore` and treating the dial as its own subsystem. Note the wheel *button* **is**
in the matrix, at strobe 7 sense 1; only the rotation is not.

For the QMK port this means **two encoders** plus the 119 matrix positions, and `encoder_map`
rather than matrix entries for the rotations.

Reproduce the decoded matrix with `tools/extract-matrix-map.py`.

## What this means for the project

**The OPX optical switches present as an ordinary contact matrix.** This is the big one. The
firmware drives push-pull strobes low and reads pulled-up senses — textbook switch matrix. The
optical detection happens *inside* the switch, which outputs a plain digital signal. So no IR
emitter/receiver front-end is needed in firmware, and QMK's conventional matrix model applies
almost directly.

That removes what `ARCHITECTURE.md` called "the unknown that matters most" and shrinks
stream C substantially. It also means a controller transplant is more viable than previously
assessed, should that ever be revisited.

## The RGB driver

Settled from the firmware, not inferred.

**Three ISSI IS31FL3743B drivers on SPI3.** The chip is named outright by the firmware's own
symbol `is31fl3743bSetPwmChannelValue`, and the SPI HAL at flash `0x12510` loads
**`0x40089000` = FLEXCOMM3**, so the bus is SPI3. A `cbz` before its `master < SPIMasterCount`
assert shows `SPIMasterCount` is 1: there is a single SPI master.

`PWM_PAGE_SIZE` is **198**, matching the IS31FL3743B's 18 x 11 channels exactly.

### The LED driver protocol, read out of the firmware

The IS31FL3743B datasheet was not obtainable, so the protocol was taken from the firmware
instead. All of it, which turned out to be possible because the firmware uses NXP's MCUXpresso
SDK: its SPI wrapper at `0x12510` builds an `fsl_spi` `spi_transfer_t` on the stack
(`{txData, rxData, configFlags, dataSize}`) and sets `configFlags` to `0x100000`, which is
`kSPI_FrameAssert` -- that is `SPI_FIFOWR_EOT_MASK`, so the chip select is released at the end of
each transfer.

Exactly one function in the whole image sends anything over SPI, which makes the trail short.

**The bus**, from the `SPI_MasterInit` call at `0x124ac` and its literals:

| Setting | Value | Where |
| --- | --- | --- |
| Peripheral | `0x40089000`, SPI3 / Flexcomm 3 | literal at `0x124fc` |
| Bit rate | **12 MHz** | literal at `0x12500` |
| Frame width | 8 bits | `dataWidth = 7` at config offset 12 |
| Chip select | hardware `sselNum`, per driver | config offset 13 |
| Chip select polarity | active low | config offset 14 is zero |
| Clock polarity, phase | both zero, so **mode 0** | left at `GetDefaultConfig` values |

**Which driver sits on which chip select** comes from a four-byte-per-driver table at flash
`0x30052`, read in the construction loop at `0x177a2` which runs three times (`cmp r4, #12`,
stride 4):

| Driver | Chip select | SPI master |
| --- | --- | --- |
| 0 | SSEL0 | 0 |
| 1 | SSEL1 | 0 |
| 2 | SSEL2 | 0 |

**The frame**, from the builder at `0x1523c`:

```
byte 0   0x50 | page          built as orr r1, r1, #0x50
byte 1   register address     for PWM and scaling, the channel number plus one
byte 2+  values               at most 198, the assert being cmp r4, #198
```

Registers are numbered from one, so channel 0 is register `0x01`. The two-byte header sits
immediately before the page in a 204-byte per-driver allocation (`movs r0, #204` at `0x15368`),
so a whole page is sent from where it was built with no copy -- a shape worth keeping.

**The pages and the function registers**, from the five callers of the builder:

| Page | Register | Value | What |
| --- | --- | --- | --- |
| 0 | channel + 1 | PWM | brightness, 198 channels |
| 1 | channel + 1 | scaling | current limit; initialised to `0xFF` throughout |
| 2 | `0x00` | `0x09` / `0x08` | configuration: running, or software shutdown |
| 2 | `0x01` | `0xFF` | global current; **found by measurement, not by reading** |
| 2 | `0x2F` | `0xAE` | reset |
| 2 | `0x25` | `0x80` | set from a flag; purpose not identified |

**Register `0x01` was missing from this table**, and it is the one register without which
nothing lights at all: a reset leaves the global current at zero, and zero there is not a dim
setting. Five callers of the frame builder were found by reading the image, and this write was
not among them.

It was found by running Corsair's own firmware on the keyboard, letting it light, and setting a
hardware breakpoint on the builder at `0x1523c` from a reset. Its arguments are
`(buffer, page, register, data)`, and the first writes it makes to each of the three drivers in
turn are:

```
reg=0x2f  bytes=0xae        reset
reg=0x01  bytes=0xff        global current
reg=0x25  bytes=0x80        the flag register, whose value is 0x80 and not 0xC0
```

The same run settled the `0x25` value, which static reading could only give as one of two.

### The LED bus, identified at the pins

Sampling GPIO port 0 while each firmware runs, and counting how often each pin reads high,
identifies the bus without a logic analyser. A mode-0 clock rests low and pulses high; a chip
select rests high and dips low; data follows its content. Under Corsair's firmware, lighting
the keyboard, 600 samples:

| Pin | High | What it is |
| --- | --- | --- |
| `P0_0` | 2/600 | SCK |
| `P0_3` | 522/600 | MOSI |
| `P0_1` | 599/600 | chip select |
| `P0_7` | 598/600 | chip select |
| `P0_9` | 599/600 | chip select |
| `P0_13` | 552/600 | an open-drain output it pulses; purpose unknown |
| `P0_8`, `P0_29` | held high | the enables, which this firmware now holds high too |

Under this firmware the same pins move, with one exception: **`P0_13` never moves at all**,
4000 samples out of 4000 high. It is the only pin the stock firmware works that this one does
not, and it is the next thing to chase.

The other difference in those counts is explained and is not a fault. Only one chip select was
seen to assert here, which looked like two drivers never being addressed -- but a breakpoint on
the transport shows all three addressed during initialisation, and afterwards only driver 2.
QMK sends a driver's page only when a colour in it changed, and the effect in use is a static
one, so after the first frame there is nothing to send. Sparse bus activity is the correct
behaviour, not a symptom.

### Where the lighting stands

It does not light. Every layer below it has been compared against Corsair's own firmware while
that firmware was running and lighting the keyboard, and every one of them matches:

| | Verified against the stock firmware |
| --- | --- |
| Peripheral | Flexcomm 3, and the sticky chip-select bits show both firmwares transmitting |
| Pin mux | IOCON port 0 identical under both |
| Bus | `CFG 0x05`, `DLY 0`, `DIV 0`, `FCLKSEL 0` identical under both |
| Frames | `0x52`/`0x51`/`0x50` headers, lengths 3 and 200, read out of both by breakpoint |
| Function registers | `0x2F`←`0xAE`, `0x01`←`0xFF`, `0x25`←`0x80`, `0x00`←`0x09` |
| Scaling | `0xFF` throughout, read back out of the buffer |
| Enable outputs | `P0_8` and `P0_29` high, as the stock firmware holds them |

Four defects were found and fixed along the way, each of which alone keeps the keyboard dark
while every part of the path reports success: the two LED tables disagreeing on their length,
the scaling page never being given a value, the global current register never being written,
and both enable outputs being left low. None of them announced itself.

The enable outputs are the most recent and were the hardest to see: both firmwares configure
the *same* outputs, and only the level differed. A comparison of GPIO directions -- which is
the obvious comparison -- shows nothing.

What has not been compared yet is the one thing the bus cannot be asked about from the
processor's side: whether the signals arrive. Flexcomm 3's own status says it transmits, but
that is the peripheral's account of itself and not a measurement at the pins. The next step is
to read the pins while a frame is being sent, or to break in the stock firmware's transport and
compare the state of the whole Flexcomm block word for word at the moment of a transfer rather
than at rest.

The page numbers and the reset magic match the IS31FL3743A, the I2C sibling QMK already drives,
which is corroboration from an independent source: the two parts share the register model and
differ in the bus.

`0x09` and `0x08` differ in one bit, which is the software shutdown bit on the sibling. The other
bit set in both is reproduced rather than interpreted, there being no datasheet to interpret it
against.

### `LedInfoMap`

At flash **`0x00030159`**, **190 entries of 7 bytes**. The address comes from the literal
`ledDriverSetLedColorChannel` loads, and its `cmp r0, #189` fixes the count:

```asm
ldr  r4, [pc, #96]     ; r4 = 0x00030159 = LedInfoMap
lsls r3, r0, #3        ; led * 8
subs r5, r3, r0        ; led * 7
add  r5, r4            ; &LedInfoMap[led]
ldrb r5, [r5, #6]      ; isRgb
...
ldrb r1, [r3, #2]      ; chip
ldrb r0, [r3, #3]      ; channel
movs r4, #198          ; PWM_PAGE_SIZE
mla  r3, r4, r1, r3    ; buffer + 198 * chip
strb r2, [r3, r0]      ; buffer[198 * chip + channel] = value
```

Each entry is three `(chip, channel)` pairs — one per colour — plus an `isRgb` flag. The PWM
staging buffer lives in SRAM at `0x20001684`.

| | |
| --- | --- |
| LEDs | **190** — 184 RGB, 6 single colour |
| Chips | 3, indices 0 to 2 |
| chip 0 | 198 of 198 channels used |
| chip 1 | 195 of 198 |
| chip 2 | 165 of 198 |

Consecutive LEDs step 18 channels apart — LED 0 is channels 0, 1, 2 and LED 1 is 18, 19, 20 —
which is the driver's 18-column geometry showing through.

### The three LED counts

| Count | Source | What it is |
| --- | --- | --- |
| **190** | this firmware's `LedInfoMap` / `LedIdCount` | **LEDs the hardware actually addresses** |
| 193 | ckb-next's `led_bragi.c` | its own host-side count, 3 higher; unexplained |
| 174 | our `rgb_matrix.layout` | only those ckb-next's GUI gives coordinates to |

All 190 channel triples are **distinct**, so there are no aliases and 190 is a genuine
physical count. An earlier guess here that the map contained duplicates was wrong: the twelve
apparently repeated channel references on chip 2 come from the six single-colour LEDs, each
repeating its one channel three times *inside its own entry*, which inflates the reference
count without affecting the distinct count (177 - 18 + 6 = 165).

The six single-colour LEDs are chip 2 channels **174, 175, 176, 192, 193 and 194**. The
firmware has a status-LED concept — `lightingsManagerSetStatusLed`, `getStatusZoneLedList`
and a `StatusLedCount` bound — which these plausibly are, though that is not confirmed.

Model variation does **not** explain the gap. Corsair ships **one image for both the OPX and
the Cherry MX variants** — the 0.30.257 file is byte-identical under product IDs `1b7c` and
`1b7d` (sha256 `316319e65a42…`) — and all four published images across both IDs carry
byte-identical key tables. Both variants also share the chassis, so a shared image is not a
superset covering hardware this board lacks.

### The Bragi index to LedInfoMap index mapping is unsolved

`LedInfoMap` says which driver and channels light LED *n*. ckb-next's `keymap_bragi` says which
key Bragi index *b* is. **Nothing found relates the two**, and without that relation a channel
table cannot be keyed to physical keys.

There is no translation table in the firmware. The only two references to `LedPresenceFlag` are
`checkLedPresence` itself and a per-index attribute lookup; neither compacts an index. So any
mapping is a hypothesis, and a wrong one is silent: every key still lights, just the wrong one.

The obvious candidate was tested and **rejected**. It was that the *n*th present Bragi LED is the
*n*th RGB entry of `LedInfoMap` — the RGB entries being 0 to 172 and 179 to 189, with the six
single-colour ones occupying 173 to 178 as a contiguous block.

`tools/check-led-mapping.py` falsifies it on geometry. The three drivers sit on one PCB and a
channel advances by 18 per switch row, so a correct mapping should put each driver's channels in a
*region* of the board. Under this one:

```
  driver  LEDs   x span   as a fraction of the board
       0    58      258   0.88
       1    64      275   0.94
       2    53      294   1.00

  driver  corr(channel row, y)  corr(channel column, x)
       0  +0.139                +0.742
       1  -0.120                -0.168
       2  +0.157                -0.018
```

Every driver spans essentially the whole keyboard and the correlations are noise. Three drivers
with no reason to interleave that finely do not wire like that, so the mapping is wrong.

The test is kept as a tool rather than as a note, so the next candidate costs one command. What
would settle it instead of testing guesses: reading the mapping off the hardware, by lighting one
channel at a time from custom firmware and seeing which key lights. That is a few lines once
anything runs, and it is the same answer as for the SPI pins and the encoders.

### `LedPresenceFlag` reconciles all three counts

A third table settles it. `checkLedPresence` asserts `index < ELEMENTS(LedPresenceFlag)` with
`cmp r0, #192`, so that space is **193 wide** — exactly ckb-next's count — and the function
body gives its address:

```asm
ldr  r3, [pc, #24]   ; r3 = 0x0003214A = LedPresenceFlag
ldrb r0, [r3, r0]
clz  r0, r0
lsrs r0, r0, #5      ; 1 if the byte is zero, else 0
```

The 193 bytes at flash **`0x0003214A`** hold **9 zeros and 184 non-zeros**, and 184 is
precisely `LedInfoMap`'s RGB entry count. So:

| Count | Is | Arithmetic |
| --- | --- | --- |
| **193** | the Bragi LED index space | ckb-next's figure |
| **9** | indices absent on this hardware | zero bytes in `LedPresenceFlag` |
| **184** | real Bragi LEDs | 193 - 9, and `LedInfoMap`'s RGB count |
| **190** | `LedInfoMap` entries | 184 Bragi LEDs + 6 status LEDs |
| **174** | our generated layout | the subset ckb-next's GUI positions |

The gap is therefore **16 = 10 + 6**: ten present Bragi LEDs that ckb-next's geometry gives no
coordinates to, plus the six single-colour status LEDs, which live outside the Bragi index
space entirely.

The absent indices are **3, 103, 104, 112, 113, 120, 121, 129 and 130**. Cross-referenced
against `keymap_bragi` those are a reserved slot, `volup` and `voldn` (the volume roller has
no LED of its own), `rwin` (not on this board), `light`, two unnamed, and `ctrlwheelccw`.

One caveat: the table is **not boolean**. Its values are `0xff` (177 entries), `0x08` (5) and
`0x0a` (2) besides the zeros, so it carries a per-LED attribute whose non-zero meanings are
unknown. Only the zero-versus-non-zero split is relied on here.

**For the driver, 190 and `LedInfoMap` are the numbers that matter**, since they are what the
hardware addresses. ckb-next's 193 is host-side bookkeeping. Our generated `rgb_matrix.layout`
carries 174 because that is all ckb-next's geometry positions, so **up to 16 addressable LEDs
are currently missing from it** — worth closing before the RGB driver is written.

Corsair's own product page is consistent on the key side, listing **110 keys** and **6 macro
keys**: a full-size ISO board is 105, this one drops right-Win for Fn giving 104, plus the six
macro keys. It gives no LED or LightEdge figures.

Print the map with `tools/extract-matrix-map.py <image> --leds`.

## The complete pin configuration

Enumerated rather than sampled: exactly **three** instructions in the whole image load the
IOCON base (`0x40001000`), at flash `0x11a9c`, `0x121a0` and `0x17214`, and every pin write in
each was collected by walking forward to the function's end.

The method validated itself on the one function already analysed by hand: the matrix init at
`0x17214` comes out as exactly `P1_18`-`P1_25`, and nothing else.

| Pins | Value | Meaning | Role |
| --- | --- | --- | --- |
| `P1_0`-`P1_17` | `0x120` | digital, pull-up | **matrix senses** |
| `P1_18`-`P1_25` | `0x100`, then `0x300` | digital; later filter off | **matrix strobes** |
| `P0_23`-`P0_28` | `0x106` | **FUNC6**, digital | six consecutive — see below |
| `P1_26`, `P1_27` | `0x110` | digital, **pull-down** | encoder candidates |
| `P0_18` | `0x110` | digital, pull-down | encoder candidate |
| `P0_13` | `0x900` / `0xd00` | FUNC0 with the I2C-specific bits | the part's true I2C pin |
| most others | `0x100` | digital GPIO | — |

Two of the three functions (`0x11a9c` and `0x121a0`) are **whole-chip pin initialisers**
covering nearly all of ports 0 and 1, differing in only a handful of pins — `P0_0`/`P0_1` are
FUNC2 in one and GPIO in the other, and `P0_2` has a pull-down in one. They look like two
configurations or board revisions rather than two subsystems. An earlier note in this document
guessed one of them was "the SPI pin config" from its proximity in flash to the SPI HAL; that
guess was unfounded and is withdrawn.

### The SPI pins: P0_23-P0_28 is the SPIFI bus, not the LED drivers

An earlier version of this section proposed `P0_23`-`P0_28` as the LED driver bus, on the
grounds that it is six consecutive pins on FUNC6 and the drivers need six signals, and that
NXP's datasheet listed `FC3_RXD_SDA_MOSI` as an alternate function of `PIO0_24`.

**Those six pins are the SPIFI bus** — external quad-SPI flash. `CSN`, `IO(0)`, `IO(1)`, `CLK`,
`IO(3)`, `IO(2)` on `P0_23` through `P0_28` respectively, all at **FUNC6**.

This is not inferred. NXP's own SDK SPIFI example for the LPCXpresso54628 configures those exact
six pins, in that order, with `IOCON_PIO_FUNC6` — the same value, `0x106`, that this firmware
writes. The coincidence of arity with the LED drivers' six signals is what made the old reading
look strong, and a matching count is not evidence.

That the K100 carries an external serial flash is unsurprising: 190 LEDs of stored animation is
more than is comfortable in 512 KB alongside the application.

#### A trap in NXP's pin lists, recorded because it produced a wrong answer

NXP's `pin_mux.c` files carry a `pin_signal:` string per pin, listing its functions. **The
position in that list is not the FUNC number**, because undefined functions are omitted rather
than held open. `SPIFI_CSN` is the sixth entry for `PIO0_23`, at index 5, and is FUNC6.

An intermediate version of this section published a FUNC table built on the assumption that index
equals FUNC. It was checked against NXP's code only at index 1, where it happens to hold, and the
generalisation was wrong. **Use these lists to learn whether a signal exists on a pin, never to
learn its FUNC number** — for that, find NXP code configuring the signal and read the
`IOCON_PIO_FUNCn` it uses.

### Where the LED driver's SPI pins are: not in this image

The pin enumeration has been reproduced independently and its coverage is now closed, which
changes the conclusion.

Exactly three literals in the image hold the IOCON base `0x40001000`, at `0x11b34`, `0x124a4` and
`0x17284`, and the base is never constructed with `movw`/`movt` instead. Every store in all three
functions goes through `r3` at a literal offset between 0 and 252 — IOCON words 0 to 63, which is
**ports 0 and 1 and nothing else.** Port 2 begins at word 64 and is never written.

Across the whole firmware, the only pins given a non-zero FUNC are:

| Pins | FUNC | What |
| --- | --- | --- |
| `P0_0`, `P0_1` | 2 | in one initialiser only |
| `P0_23`-`P0_28` | 6 | the SPIFI bus, above |

**So the application configures no Flexcomm 3 pins at all**, while its SPI HAL demonstrably
drives FLEXCOMM3 at `0x40089000`. The pins must therefore be muxed by something else, and the
only candidate is the **bootloader**, which is a separate flash region that Corsair does not
publish and which is absent from the `Heracles_Application_Firmware` images. It would have reason
to: an updater that cannot light the keyboard cannot show progress.

FC3 signals do exist elsewhere, and on more than one port:

| Pin | Function | Found in |
| --- | --- | --- |
| `PIO2_18` | `FC3_RXD_SDA_MOSI` | NXP's LCD example |
| `PIO2_19` | `FC3_TXD_SCL_MISO` | " |
| `PIO2_20` | `FC3_RTS_SCL_SSEL1` | " |
| `PIO2_21` | `FC3_CTS_SDA_SSEL0` | " |
| `PIO0_6` | `FC3_SCK` | NXP's LPCXpresso54628 GPIO example |
| `PIO2_2` | `FC3_SSEL3` | " |

So FC3 is not confined to port 2, and `FC3_SCK` sits on `P0_6` — a pin the keyboard's firmware
leaves as plain GPIO. None of this says where the keyboard's bus *is*, only where it could be:
port 2 is never configured at all, and `P0_6` is configured as GPIO. The sample still covers well
under half the part's pins.

**This question is closed to static analysis of what is obtainable.** It is wide open to the
obvious alternative: once custom firmware runs on the part, reading `IOCON->PIO[port][pin]` back
reports whatever configuration the bootloader left, for all 160 pins, in a few lines of code. That
is the same argument `ARCHITECTURE.md` makes for the encoder pins — past a first flash, GPIO is a
better instrument than disassembly — and it now applies here too.

### The encoder pins: a hypothesis, not a finding

`P1_26` and `P1_27` are the **only pull-down-configured pins on port 1**, sitting immediately
past the matrix strobes, and `P0_18` carries a pull-down in both initialisers. Pull-downs are
what encoder contacts want, and two quadrature encoders need four inputs.

**What argues against calling this settled:** the read path was not found. There is no `ubfx`
at bits 26-27, no byte read of `B[1][26]` or `B[1][27]`, and no word-register access for those
pins. The matrix by contrast reads `PIN[1]` with a literal `ubfx r4, r4, #0, #18`, which is how
its senses were proved. Until the encoder read is located this stays a hypothesis resting on
pull-up/pull-down configuration alone.

## Still unknown

- ~~Which `(strobe, sense)` pair is which key.~~ **Answered** by `HidKeyCodes` above. The
  full ISO matrix is 119 populated positions: 104 carrying a HID usage code and 15 without.

  What the 15 are individually is still open. Their positions are known — sense 0 on strobes
  0 to 5, one at strobe 5 sense 11, and eight on strobe 7 — and sense 0 across six
  consecutive strobes is a clean fit for the six macro keys G1 to G6, but that is a reading of
  the shape rather than evidence.

  Related arithmetic worth resolving: the generated logical layout has 123 keys while the
  matrix has 119 positions, so **four logical keys are not matrix-scanned**. The wheel
  rotations are the strongest candidates for two of them, since the firmware has a
  `dialAssignmentMutex` and `dialStopSemaphore` and treats the dial as its own subsystem. The
  other two are unidentified.
- **Regenerating the QMK definition on the real matrix.** `qmk-layout.md` still describes a
  *logical* 7x22 matrix, which was the right call while the electrical one was unknown. It can
  now be replaced by the real 8x18 mapping, which also removes the need for the custom matrix
  driver to translate between the two.
- ~~RGB driver bus and pins.~~ **The bus is answered**; the pins are **not in this image** and
  are most likely set by the bootloader, which is not published — see below. Reading IOCON back
  from custom firmware answers it. Flexcomm 3's options are fixed by the
  LPC546xx datasheet once the instance is known.
- **Encoder pins — attempted, not found.** Much harder than the matrix, because nothing
  asserts on an encoder table, so there is no string naming one. What was established:

  | Finding | Evidence |
  | --- | --- |
  | No pin-interrupt path | **No reference to PINT** (`0x40004000`) anywhere in the image |
  | `INPUTMUX` *is* configured | reached by `sub.w r5, r5, #0x87000` from the GPIO base, giving `0x40005000` — consistent with timer-capture or pin-interrupt input selection |
  | Not polled via a literal port-0 read | no access to `PIN[0]` (`0x2100`); the matrix reads `PIN[1]` (`0x2104`) with a literal, so the encoders use a different path |
  | Pin configuration goes through helpers | functions at flash `0x1381c` and `0x13858` take port and pin arguments, so pin numbers are call arguments rather than greppable constants |
  | Encoder objects and callbacks | `encodersSemaphore`, `dialTimer`, `dialAppSwitchTimer` created near flash `0x16250`, whose literal pool holds callback addresses around `0x155xx`-`0x157xx` |

  Tracing init to callback to GPIO helper to pin constant is the remaining path. Not urgent:
  the pins are only needed for encoder support, which is late in the port.

- **The full pin map is not settled, and one attribution is doubtful.** Three functions write
  IOCON. One is the matrix init at flash `0x17210` (words 32-57 = `P1_0`-`P1_25`), which is
  solid. A second, near flash `0x11a9c`, configures `P0_2` as FUNC1, `P0_17` as plain GPIO,
  `P0_18` as GPIO with a pull-down, and `P0_23`-`P0_26` as FUNC6. A third, ending at flash
  `0x124a0`, writes words 36-41 — which is `P1_4`-`P1_9`, **overlapping the matrix sense
  lines**. That overlap is unexplained, so do not treat the second and third as identified
  subsystems; proximity to the SPI HAL in flash is not evidence of purpose. Resolving the
  LPC546xx FUNC numbers needs the pin table from UM10912.

- **CTIMER4 probably drives the scan.** Its base `0x40049000` is referenced five times inside
  the matrix code between flash `0x171fc` and `0x17468`, which fits the 4000 Hz scan rate
  needing a hardware timer. Suggestive from locality rather than decoded.

## USB: the right controller, and the one piece still missing

**The K100 is wired to `USBHSD`, the high-speed device controller at `0x40094000`
— not `USB0`, the full-speed one at `0x40084000`.** Three independent reasons:

- the stock application references `0x40094000` and never mentions `0x40084000`;
- the keyboard negotiates High Speed, which the kernel logs on every connect;
- its application's vendor endpoints are **1024 bytes**, and full-speed interrupt
  endpoints stop at 64.

A port that brings up `USB0` on this board initialises a controller whose pins go
nowhere. It succeeds, reports nothing wrong, and never enumerates — which is
exactly what this project spent a flash cycle not understanding.

The two are the same NXP IP, so the register offsets are identical for everything
the shared ChibiOS driver touches. What genuinely differs:

| | `USB0` | `USBHSD` |
| --- | --- | --- |
| `EPLISTSTART` | whole address, 256-aligned | bits 8–19 programmable, **20–31 fixed** |
| `DATABUFSTART` | high bits only (one 4 MB region) | whole 32-bit address |
| Endpoint list may live in | any reachable SRAM | **only the 1 MB window at `0x40100000`** |
| Clock gates | `USB0D`, `AHBCLKCTRL1` bit 25 | `USB1D`+`USB1RAM`, **`AHBCLKCTRL2`** bits 5, 6 |
| PHY power | `PDEN_USB0_PHY`, bit 21 | `PDEN_USB1_PHY` bit 0, `PDEN_USB1_PLL` bit 1 |
| Interrupt | `USB0_IRQn` 28 → `VectorB0` | `USB1_IRQn` 47 → `VectorFC` |
| Endpoints | 5 | 6 |

The fixed field is the one that forces a design change rather than a constant: the
hardware supplies the top twelve address bits, so an endpoint list in main SRAM is
unreachable however the register is written. It has to be in the dedicated 8 KB
USB SRAM, which is where the stock firmware puts its own, at `0x40100A00`.

The `AHBCLKCTRL` register numbers are worth care. NXP's header names these bits
without saying which of the three registers they belong to, and bits 5 and 6 of
`AHBCLKCTRL0` are `SRAM3` and nothing at all. Their clock driver settles it:
`kCLOCK_Usbd1 = CLK_GATE_DEFINE(AHB_CLK_CTRL2, 5)`.

### Still missing: the PHY's voltage domain

With all of the above corrected, the firmware runs and **still does not enumerate
— and the host sees no attempt at all.** The kernel logs nothing between the
disconnect and the bootloader returning: no descriptor read, no "device not
accepting address", no error. So the device never pulls up D+ and never connects.
That rules out the endpoint list, the descriptors and the control endpoint, all of
which would produce a visible, failing enumeration instead.

What remains is the high-speed PHY itself. NXP's own sequence,
`CLOCK_EnableUsbhs0DeviceClock`, begins with `POWER_SetUsbPhy()` — "power on the
VD5 for USB PHY" — and **that function ships only inside their binary power
library.** There is no source for it in the SDK and no register documentation for
it in the device header; `fsl_power.c` for this part contains nothing but
brown-out detection.

This port currently does everything around it: the gates in NXP's order, the
transceiver and PLL power-down bits cleared, `USBCLKADJ` set, and the settling
delay. Powering the transceiver *down* is a deliberate no-op, because bringing it
back is the part that cannot be done.

The route to the rest is the stock firmware, which links that same binary library,
so the sequence is present in the image that this project already disassembles.
Finding it is the next piece of work.
