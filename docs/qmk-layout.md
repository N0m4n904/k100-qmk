# QMK layout and LED definition (K100 OPX, ISO DE)

Generated, not hand-written. Regenerate with:

```sh
nix develop .#keymap
make -C tools/extract-ckb-keymap CKB=/path/to/ckb-next
tools/extract-ckb-keymap/extract-ckb-keymap k100 de > tools/extract-ckb-keymap/k100-de.json
python3 tools/generate-qmk-layout.py --ckb /path/to/ckb-next
```

Outputs:

| File | Contents |
| --- | --- |
| `qmk/keyboards/corsair/k100/keyboard.json` | `LAYOUT_k100_iso_de`, matrix size, `rgb_matrix.layout` |
| `qmk/keyboards/corsair/k100/keymaps/default/keymap.c` | Default keymap plus an empty Fn layer |
| `via/k100-iso-de.json` | VIA v3 definition |

## Where the data comes from

Two sources, joined:

- **Geometry** from ckb-next, via `tools/extract-ckb-keymap`. The K100 map is produced by a
  chain of C++ transformations off the K95 Platinum (`KeyMap::getMap`), so rather than
  reimplement that chain the extractor **links against ckb-next's own implementation** and
  asks it for the finished map. No transcription, no drift.
- **LED ordering** from ckb-next's `keymap_bragi` array in `src/daemon/keymap.c`, where the
  array index *is* the Bragi LED index. `N_KEYS_BRAGI_PATCH` is 194, matching the 193 LEDs
  that `led_bragi.c` reports for the K100.

The generator refuses to emit anything if a key lacks a QMK keycode or an LED index, so
silent gaps are not possible.

## The logical matrix — the decision that unblocks this work

QMK's layout needs `matrix: [row, col]` for every key, and we do **not** know the K100's
electrical matrix. That does not block anything, because we are writing the matrix driver
ourselves: the positions here are a **logical** matrix of our own choosing, and mapping the
optical scan onto it is the job of `matrix_scan_custom()`.

Rows are assigned by clustering keys on their **vertical centre**, not their top edge. That
detail matters: it is what puts the tall ISO Enter in the home row and the numpad's `+` and
Enter in their conventional rows, rather than a row above.

Result: **7 rows × 22 columns, 123 keys.**

```
row 0   profswitch wheel(ccw/btn/cw) lock mute volup voldn
row 1   g1 esc f1..f12 prtscn scroll pause stop prev play next
row 2   g2 grave 1..0 minus equal bspace ins home pgup numlock num/ num* num-
row 3   g3 tab q..p lbrace rbrace del end pgdn num7 num8 num9
row 4   g4 caps a..l colon quote hash enter num4 num5 num6 num+
row 5   g5 lshift bslash_iso z..m comma dot slash rshift up num1 num2 num3
row 6   g6 lctrl lwin lalt space ralt fn rmenu rctrl left down right num0 num. numenter
```

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

### Two caveats

- `led_bragi.c` reports **193** LEDs for the K100 but only **174** have positions in the GUI
  map. The remaining 19 are unaccounted for — possibly undrawn zones or padding. Worth
  resolving before the RGB driver is finalised.
- The LED **order** here follows Corsair's own Bragi indexing. That is a well-founded
  starting assumption, but the index to hardware mapping depends on how the LED driver ICs
  are actually wired, which is still unknown. Expect to revisit once the RGB bus is traced.

## Keycode choices worth knowing

- **G1-G6** map to `KC_F13`-`KC_F18`. QMK has no macro-key standard; this is the conventional
  stand-in and is trivially remapped in VIA.
- **The iCUE wheel** is modelled as three matrix positions (CCW, button, CW) defaulting to
  volume down/mute/up, because that is how ckb-next sees it reported. If the hardware turns
  out to expose a quadrature encoder, these should move to QMK's `encoder_map` instead.
- **`profswitch` and `lock`** are `KC_NO`; they have no QMK equivalent and need custom
  keycodes once their purpose is decided.
- **`fn`** is `MO(1)`, and layer 1 is deliberately all `KC_TRNS` until the hardware is
  understood.

## Validation

The generated files were checked structurally: 123 layout entries with unique in-bounds
matrix positions, 174 RGB entries with coordinates inside QMK's space and every `matrix`
reference resolving to a real key, and the VIA definition carrying the identical position set
and matrix dimensions as `keyboard.json`.
