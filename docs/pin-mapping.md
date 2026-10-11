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
str   r1, [r2, #0x2204]     ; drive them high (open-drain: released)
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
| `0x300` | FUNC0, digital, **open-drain**, no pull — the strobes |
| `0x100` | FUNC0, digital, no pull — the senses |
| `0x120` | FUNC0, digital, **pull-up** — the senses, alternate mode |

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
strb.w r4, [r1, #32]        ; GPIO->B[1][pin] = 1, release the open-drain strobe
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
firmware drives open-drain strobes and reads pulled-up senses — textbook switch matrix. The
optical detection happens *inside* the switch, which outputs a plain digital signal. So no IR
emitter/receiver front-end is needed in firmware, and QMK's conventional matrix model applies
almost directly.

That removes what `ARCHITECTURE.md` called "the unknown that matters most" and shrinks
stream C substantially. It also means a controller transplant is more viable than previously
assessed, should that ever be revisited.

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
- **RGB driver bus and pins.** Not yet analysed. Reference counts above the SDK
  lookup-table baseline point at **FLEXCOMM3** (5 refs, versus 3 for every other Flexcomm),
  plus **CTIMER4** (7), **GPIO** (6), **CRC** (5), **EEPROM** (4) and **MRT0** (4).
  Counting alone proves nothing — the all-instances-equal pattern is just an SDK array — but
  FLEXCOMM3 is the obvious place to start.
