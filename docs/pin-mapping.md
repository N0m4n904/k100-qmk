# Key matrix pin mapping, from firmware static analysis

Recovered by disassembling Corsair's official K100 OPX firmware
(`Heracles_Application_Firmware_v0.35.288`, PID `0x1b7c`). **No hardware was touched and the
case was never opened.**

Peripheral addresses are from NXP's own CMSIS header for the exact part
(`mcux-sdk/devices/LPC54605/LPC54605.h`), not from memory.

## Establishing the load address

Needed first, or no disassembly means anything. The image has no vector table at its start,
so the base was found statistically: under the correct base, internal function pointers
should land on Thumb function prologues.

| Candidate flash base | Pointers landing on a prologue |
| --- | --- |
| **`0x00000000`** | **29.3%** |
| `0x00002000` | 2.7% |
| `0x00004000` | 0.4% |
| `0x00008000` | 0.4% |
| `0x00010000` | 0.4% |
| `0x00018000` | 3.0% |
| `0x00020000` | 2.1% |

A sweep of the payload offset peaked just as sharply at `0x2c` (29.3% against 2.4% for the
next best). So:

```
application is linked at flash 0x00000000
flash address A  <->  file offset A + 0x2c
```

The 44-byte container header is stripped before the image is written.

## The matrix

**8 strobe outputs and 18 sense inputs, all on GPIO port 1.**

| Role | Pins | Count |
| --- | --- | --- |
| Strobe / drive | **P1_18 .. P1_25** | 8 |
| Sense / read | **P1_0 .. P1_17** | 18 |

8 × 18 = 144 positions for 123 keys, the remainder masked off.

### Initialisation, at flash `0x7210`

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

### The scan loop, at flash `0x70b8`

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

- **Which `(strobe, sense)` pair is which key.** The mask and lookup tables are built at
  runtime in SRAM, so they are not directly readable from the image; their flash source has
  not been located yet. Finding the table that maps a matrix position to a Bragi key index
  would complete the mapping and let the generated logical matrix in `docs/qmk-layout.md` be
  replaced by the real electrical one.
- **RGB driver bus and pins.** Not yet analysed. Reference counts above the SDK
  lookup-table baseline point at **FLEXCOMM3** (5 refs, versus 3 for every other Flexcomm),
  plus **CTIMER4** (7), **GPIO** (6), **CRC** (5), **EEPROM** (4) and **MRT0** (4).
  Counting alone proves nothing — the all-instances-equal pattern is just an SDK array — but
  FLEXCOMM3 is the obvious place to start.
