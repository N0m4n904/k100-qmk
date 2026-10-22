# QMK layout and LED definition (K100 OPX, ISO DE)

Generated, not hand-written. Regenerate with:

```sh
nix develop .#keymap
make -C tools/extract-ckb-keymap CKB=/path/to/ckb-next
tools/extract-ckb-keymap/extract-ckb-keymap k100 de > tools/extract-ckb-keymap/k100-de.json
python3 tools/fetch-firmware.py --out /tmp/k100-fw
python3 tools/generate-qmk-layout.py --ckb /path/to/ckb-next \
    --firmware /tmp/k100-fw/Heracles_Application_Firmware_v0.35.288.bin
```

Outputs:

| File | Contents |
| --- | --- |
| `qmk/keyboards/corsair/k100/keyboard.json` | `LAYOUT_k100_iso_de`, matrix size, `rgb_matrix.layout` |
| `qmk/keyboards/corsair/k100/keymaps/default/keymap.c` | Default keymap plus an empty Fn layer |
| `via/k100-iso-de.json` | VIA v3 definition: layout, the labels for the keycodes this keyboard adds, and the lighting menu |

## Where the data comes from

Four sources, joined:

- **The electrical matrix**, read from an official firmware image by `tools/k100_firmware.py`,
  which both this generator and `extract-matrix-map.py` share.

- **Geometry** from ckb-next, via `tools/extract-ckb-keymap`. The K100 map is produced by a
  chain of C++ transformations off the K95 Platinum (`KeyMap::getMap`), so rather than
  reimplement that chain the extractor **links against ckb-next's own implementation** and
  asks it for the finished map. No transcription, no drift.
- **LED ordering** from ckb-next's `keymap_bragi` array in `src/daemon/keymap.c`, where the
  array index *is* the Bragi LED index. `N_KEYS_BRAGI_PATCH` is 194, matching the 193 LEDs
  that `led_bragi.c` reports for the K100.
- **The keycodes this keyboard adds**, from `enum k100_keycodes` in the keyboard's own
  `k100.h`, read for the order it declares them in. VIA's `customKeycodes` is positional --
  its first entry labels the first `QK_KB` keycode -- so the enum is what fixes that order,
  and taking it from the header is what keeps the two from drifting apart. A keycode declared
  there with no label in the generator stops it, rather than reaching the app as a bare hex
  code nobody can pick from a list.

The generator refuses to emit anything if a key lacks a QMK keycode or an LED index, so
silent gaps are not possible.

## The electrical matrix

QMK's layout needs `matrix: [row, col]` for every key, and these are the keyboard's **real**
strobe and sense lines, read out of its own firmware:

- **rows are strobes**, `P1_18` to `P1_25`, open-drain outputs
- **columns are senses**, `P1_0` to `P1_17`, inputs with optional pull-ups
- **119 populated positions** for ISO, of which 104 carry a HID usage code

So `matrix_scan_custom()` has nothing to translate: it reports positions in the same
coordinates the layout declares. How the tables were found, and the full decoded matrix, are
in `docs/pin-mapping.md`; `tools/extract-matrix-map.py` prints it.

An earlier version of this document described a **logical** 7x22 matrix invented here, which
was the right call while the electrical one was unknown. It is gone.

### Two encoders, not keys

Four keys that ckb-next lists as scanning are absent from the matrix: `ctrlwheelccw`,
`ctrlwheelcw`, `voldn` and `volup`. Those are the rotations of the **iCUE dial** and the
**volume roller**, so both are encoders. The wheel's *button* is a matrix key, at strobe 7
sense 1; only the rotations are not.

The generator asserts this rather than trusting it: if the set of scanning keys missing from
the matrix is ever anything other than exactly those four rotations, it refuses to emit. That
is what catches a silently dropped key.

**Their pins are not yet known**, so no `encoder` block is emitted. They are recorded as
comments in the generated keymap, and QMK's `encoder_map` can be filled in once the pins are
traced.

## ISO DE specifics

Confirmed by diffing the DE map against US rather than assumed:

| Evidence | DE | US |
| --- | --- | --- |
| `bslash_iso` (key beside Left Shift) | present | absent |
| `hash` (key beside Enter) | present | absent |
| `bslash` (ANSI, above Enter) | absent | present |
| Enter | `18x24` — tall | `26x12` — wide |
| Left Shift | `14x12` — short | `26x12` |
| `y` / `z` labels | Z / Y (QWERTZ) | Y / Z |

**ckb-next names keys by US position, which is also QMK's convention.** So the key named `z`
is the one where US has Z — labelled Y on a German board — and maps to `KC_Z`. The German
labelling is a host-side concern; firmware sees positions. Only two things are genuinely
physical about ISO here: the two extra keys (`KC_NUBS`, `KC_NUHS`) and the key shapes.

## RGB matrix

**174 LEDs**: 119 under keys (`LED_FLAG_KEYLIGHT`), 55 in the three-sided LightEdge and
lightbars (`LED_FLAG_UNDERGLOW`). Positions are scaled from ckb-next's 16ths-of-an-inch grid
onto QMK's 224x64 space, preserving the bounding box so the negative coordinates of the left
bar and top bar land correctly.

Four keys carry a scancode but no LED, which is why 123 keys yield only 119 key LEDs.

### The 174 versus 193 question, resolved

`led_bragi.c` reports **193** LEDs while our map positions **174**. Cross-checking ckb-next's
`keymap_bragi` against OpenRGB master's `corsair_k100_layout` accounts for all 19, and the
count 193 is confirmed from the other side too: OpenRGB's LED indices run to a maximum of
**192**, so 0–192 inclusive.

| LED indices | Count | What they are |
| --- | --- | --- |
| 0–3 | 4 | Reserved. The first four `keymap_bragi` entries are `{0, -1, KEY_NONE}` padding. |
| 49 | 1 | `bslash`, the ANSI backslash. **Not present on an ISO board**, so correctly absent. |
| 103, 104, 112, 113, 115, 116, 127, 130 | 8 | `volup`, `voldn`, `rwin`, `light`, `ro`, `ctrlwheelcw`, `mr`, `ctrlwheelccw` |
| 117–121, 129 | 6 | Unnamed in both ckb-next and OpenRGB. Genuinely unknown. |

**So 174 is correct for this board, not deficient.** Four are padding, one is ANSI-only, and
six are unknown to every public source.

The middle group needs a second look though. Those eight carry LED indices in
`keymap_bragi` but are flagged `hasLed == false` in ckb-next's GUI geometry — the two tables
in the same project disagree. `volup`, `voldn` and the two wheel-rotation entries are real
controls on this keyboard, so they may well be lit in hardware and simply missing from the
GUI's drawing data. If so, our `rgb_matrix` layout omits up to four real LEDs. Worth checking
once anything can actually drive the LEDs.

Useful corroboration from OpenRGB for later: it names indices our map already covers,
including `Lock` at 114, the media keys at 123–126, `Profile` at 128, `iCue` at 137,
`Wheel 1`–`8` at 182–189 and `Logo L` at 190. The iCUE wheel therefore has **eight LEDs of
its own**, which is worth knowing when the LightEdge and wheel zones are wired up.
- The LED **order** here follows Corsair's own Bragi indexing. That is a well-founded
  starting assumption, but the index to hardware mapping depends on how the LED driver ICs
  are actually wired, which is still unknown. Expect to revisit once the RGB bus is traced.

## Keycode choices worth knowing

- **G1-G6** map to `KC_F13`-`KC_F18`. QMK has no macro-key standard; this is the conventional
  stand-in and is trivially remapped in VIA.
- **The two rotations are encoders**, not matrix positions. ckb-next reports them as keys, but
  the stock firmware attaches `P1_28`-`P1_31` to pin interrupts and nothing in the matrix sees
  a turn. The iCUE wheel sets the lighting's brightness and the volume roller the volume;
  which pair is which, and which way round, is not established. See `docs/pin-mapping.md`.
- **The wheel's button** is `K100_RGB_PROFILE`, a keycode this keyboard adds, and steps the
  lighting through white, red and rainbow. It used to be `KC_MUTE`, which the mute key beside
  it already was.
- **`lock`** is `GU_TOGG`: it suppresses the GUI keys, which is what the Windows Lock icon
  above it reports. **`profswitch`** is `KC_NO` -- the profiles it switched belong to the
  stock firmware, and this one has nothing equivalent to switch.
- **`fn`** is `MO(1)`. Layer 1 passes everything through except Fn+Esc, which is `QK_BOOT`,
  and Fn+Backspace, which is `EE_CLR`.

## The lighting menu is declared, not VIA's own

VIA ships a `qmk_rgb_matrix` menu that a definition can ask for by name, and it is the
wrong menu for this keyboard. Its effect dropdown lists all 45 effects QMK has and sends
the position in that list, while QMK numbers only the effects that are **compiled**. This
firmware compiles two, so 1 is the solid colour and 2 the rainbow. In VIA's list, 2 is
"Breathing". Picking breathing would start the rainbow and nothing would say so.

The menu is therefore emitted from the same `ANIMATIONS` table the firmware's animation
list comes from. The dropdown holds exactly the effects that exist, at the numbers they
answer to, and the colour and speed controls carry a `showIf` so that each appears only
for the effects that read it.

### Why "Off" is effect 3 and not zero

VIA's own menu offers "All Off" as value zero, and zero is not an effect. It disables the
matrix, and QMK draws the status icons from inside the lighting task:

```c
rgb_task_render(effect);
if (effect) {
    rgb_matrix_indicators();
    rgb_matrix_indicators_advanced(&rgb_effect_params);
}
```

With no effect running there is no call, and the task then stops sending frames at all --
*"we only need to flush once if we are `RGB_MATRIX_NONE`"* -- so no other hook can paint
them either. Turning the key lighting off from the app would take Num Lock with it.

So the menu does not offer zero. Its "Off" selects `K100_LIGHTS_OFF` in
`rgb_matrix_kb.inc`, an effect that paints black: the task keeps running, the indicator
hook keeps being called, and the keyboard goes dark with its icons still lit. QMK numbers a
keyboard's own effects after every built-in, which makes it 3 here, and a `_Static_assert`
in that file holds the number against what the generated definition sends.

Measured on the keyboard, reading the LED output buffer over SWD while the firmware ran:
with "Off" selected the key channels hold `0x00` and the lit indicator's channel still holds
`0xff`, where before the switch both held `0xff`.

The same asymmetry works in the firmware's favour one level down. An effect's colour goes
through the brightness, so brightness zero is dark keys; the icons are written with
`rgb_matrix_set_color()`, which hands its three bytes to the driver unscaled, so they are
lit either way.

The four controls are QMK's `id_qmk_rgb_matrix_*` values on channel 3.
`tools/via-lighting.py` drives the same four, which is how the menu is checked against the
firmware without the application:

```sh
python3 tools/via-lighting.py                 # read all four
python3 tools/via-lighting.py --effect 2 --speed 200
```

Measured on the keyboard: all four are written and read back, and the four it reports
agree with the same values read out of `rgb_matrix_config` over SWD.

## Validation

The generated files are checked structurally, not eyeballed:

- 119 layout entries with unique matrix positions, all inside 8 x 18
- 190 RGB entries with coordinates inside QMK's 224 x 64 space, every `matrix` reference
  resolving to a real key
- the VIA definition carrying the identical position set and matrix dimensions as
  `keyboard.json`

The generator also refuses to emit at all if a key lacks a QMK keycode or an LED index, if a
HID code matches more or fewer than one geometry key, if a key would land at two matrix
positions, or if the scanning keys missing from the matrix are anything other than the two
encoders' rotations.
