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

### Which group is ours

Scan-key counts from ckb-next's own geometry, per layout family:

| Family | ckb-next scan keys |
| --- | --- |
| ANSI (`us`, `pl`) | 122 |
| **ISO** (`de`, `gb`, `fr`, `it`, `es`, `se`, `dk`, `no`) | **123** |
| JIS (`jp`) | 126 |

**`0x0002FD24` has popcount 123, matching ISO exactly** — and the ISO count is identical
across all eight ISO country layouts, so it is a property of the physical layout rather than a
coincidence of German. That group is almost certainly the mask for this keyboard:

```
strobe 0: 0x3ffff  111111111111111111  18
strobe 1: 0x3ffff  111111111111111111  18
strobe 2: 0x3ffff  111111111111111111  18
strobe 3: 0x07fff  000111111111111111  15
strobe 4: 0x17ffb  010111111111111011  15
strobe 5: 0x3fc9f  111111110010011111  14
strobe 6: 0x1ffff  011111111111111111  17
strobe 7: 0x1e00f  011110000000001111   8
```

Caveat, stated plainly: the match is on popcount, and the selector's jump table does not map
cleanly onto ckb-next's layout numbering (the firmware dispatches on a value of 0–3, with one
of those splitting into two masks by a secondary check). So "123 means ISO" is a strong
inference, not a proof. Confirming it means reading the actual pressed positions off a real
keyboard.

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

- **Which `(strobe, sense)` pair is which key.** We now know *which* 123 of the 144 positions
  are populated, from the ISO mask above, but not which key sits at each. The table mapping a
  matrix position to a key index has not been found; it is presumably also copied into SRAM at
  init, alongside the second 8-entry table at `0x20001328` whose purpose is still
  unidentified. Finding it would let the generated logical matrix in `qmk-layout.md` be
  replaced by the real electrical one.

  **Best lead:** the image has a full symbol-name string table, and it includes
  `loadKeyAssignmentsSemaphore`. Chasing references to that string should reach the key
  assignment loader. Other promising names seen in passing: `ActionProcessTimer`,
  `processSemaphore`, `dialStopSemaphore` (the iCUE wheel is called the "dial", which may also
  settle whether it is an encoder).
- **RGB driver bus and pins.** Not yet analysed. Reference counts above the SDK
  lookup-table baseline point at **FLEXCOMM3** (5 refs, versus 3 for every other Flexcomm),
  plus **CTIMER4** (7), **GPIO** (6), **CRC** (5), **EEPROM** (4) and **MRT0** (4).
  Counting alone proves nothing — the all-instances-equal pattern is just an SDK array — but
  FLEXCOMM3 is the obvious place to start.
