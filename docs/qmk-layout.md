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
- **LED ordering for the keys**, from the keyboard itself. Every key LED was lit on its own
  through the probe and the key that came up was written down, 120 slots of it, and that table
  is in the generator. It had to be measured: Corsair's order agrees with nothing else here.
  See "What the LED order turned out to be" below.
- **LED ordering for everything that is not a key**, still from ckb-next's `keymap_bragi`
  array in `src/daemon/keymap.c`, where the array index *is* the Bragi LED index -- and still
  unverified, which the probe can settle the same way.
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

Their pins are `P1_28`/`P1_29` and `P1_30`/`P1_31`, read out of the stock firmware's
`INPUTMUX` configuration, where they are attached to four pin interrupts and sit outside the
matrix. The generator emits an `encoder` block for both pairs.

**Which pair is which was settled by turning them**, which is the only way it could be: the
first pair is the iCUE wheel and the second the volume roller, as the order above guessed.
What each does is decided in `encoders.c`, where the wheel sets the lighting's brightness and
the roller the volume, so the rotations are deliberately not remappable and no `encoder_map`
is emitted.

The directions had to be measured the same way, and only one of them was right: rolling the
volume roller away from the user reports *anticlockwise*, so its two cases are crossed in
`encoders.c`. QMK's `ENCODER_DIRECTION_FLIP` is no use for that, because it would turn the
wheel round with it.

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

The enter is the one key ckb-next cannot describe. An ISO enter is two rectangles -- 1.5 units
wide in the row above, 1.25 in the row below, with the key beside it tucking under the
overhang -- and `18x24` is the box around the pair, which is why the application drew a plain
tall block. `hash` ends exactly 0.25 units inside that box, which is the overhang measured
rather than assumed, and the VIA definition carries it as KLE's second rectangle. QMK's own
layout has no such thing, and nothing in the firmware looks at a key's shape.

## RGB matrix

The table is **190 entries long, one per LED in the firmware's own `LedInfoMap`**. QMK will
not address an LED past the end of it, and this keyboard has LEDs that ckb-next's geometry
never places, so stopping at what has coordinates would make the length a number someone
chose. It is read out of the keyboard instead.

| Entries | Flag | What they are |
| --- | --- | --- |
| 123 | `LED_FLAG_KEYLIGHT` | under keys. 119 keys, four of which have two LEDs: the wheel button, the profile-switch key, the lock key and the mute key. |
| 55 | `LED_FLAG_UNDERGLOW` | the three bars of the LightEdge (22 along the top, 11 down each side), the ring of eight around the iCUE wheel, the three of the logo |
| 12 | none | the six status icons, and six slots with nothing fitted |

Positions are scaled from ckb-next's 16ths-of-an-inch grid onto QMK's 224x64 space,
preserving the bounding box so the negative coordinates of the left bar and top bar land
correctly. Four more keys carry a scancode and no LED at all -- the two wheels' rotations,
which are encoders rather than matrix positions.

### The twelve entries that carry no flag

A flag of zero keeps every effect away, and that is what these entries are for: they exist so
that the indices after them line up, not to be drawn on. It is also why the coordinate they
carry is harmless -- nothing reads it.

**Six are the status icons.** Single-channel LEDs, on or off with nothing in between, so a
colour would be meaningless and what they mean is decided by what the keyboard is reporting.
`indicators.c` drives them by index and is the only thing that does. The channel table in
`is31fl3743b_leds.c` marks which LEDs have one channel, so the generator finds them rather
than being told, and refuses to build if the measured table ever places one of them.

**Six are slots with nothing fitted** -- 14, 58, 101, 113, 115 and 130 -- found by lighting
each one and seeing nothing come up. 14 is the space between F12 and Print.

### The 174 versus 193 question, resolved

`led_bragi.c` reports **193** LEDs while ckb-next's geometry names **174**. Cross-checking ckb-next's
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

The counts also agree across the two projects, which is worth noting because they were read
from different places: ckb-next's array holds 194 entries of which the first four are
padding, leaving 190 real LEDs, and 190 is exactly what the firmware's own `LedInfoMap`
holds. The two sources agree on *how many* LEDs exist. Whether they agree **index for index**
was the open question, and the answer is no -- the four padding entries at ckb-next's head
were the first reason to doubt it, and the measurement below settled it.

The middle group raised a question that the measurement has since closed. Those eight carry
LED indices in `keymap_bragi` but are flagged `hasLed == false` in ckb-next's GUI geometry —
the two tables in the same project disagree — so `volup`, `voldn` and the two wheel-rotation
entries might have been lit in hardware and missing only from the drawing data. They are not:
all 190 slots have been lit one at a time, 178 of them turned out to be a key or a light, and
the twelve left over are the six status icons and six slots with nothing fitted. Nothing is
omitted.

OpenRGB's numbering does **not** line up with the firmware's map, which is worth knowing
before borrowing anything from it: it puts the wheel's ring at 182–189 where the keyboard has
it at 120–127. What it corroborates is the hardware rather than the order — the iCUE wheel
has eight LEDs around it, and the measurement found exactly eight.
**Which driver and channels light LED *n* is known**, because `is31fl3743b_leds.c` is read
out of the firmware's own table.

### What the LED order turned out to be

Whether entry *n*'s **coordinate** belongs to LED *n* was an open question for as long as the
answer was derived rather than looked at. Three derivations were tried and all three were
wrong: ckb-next's order, which is alphabetical by key name; the key matrix, strobe by strobe;
and the physical rows. Each matched at index 0 and diverged within twenty.

What settled it was the probe in VIA's Diagnostics menu, one slot at a time:

| Slots | What is there |
| --- | --- |
| 0–119 | the keys, in **six rows of 22**, each row starting with a macro key and running across the main block before taking that row's share of the navigation and number pads. Five slots in them have nothing fitted and still take their numbers. |
| 120–127 | the ring around the iCUE wheel, clockwise from the top. |
| 128–129 | the wheel button, which has two LEDs. |
| 130 | nothing fitted. |
| 131–152 | the top light bar, left to right -- except its 12th to 15th segments, which are at 150, 149, 152 and 151, in that order, between the 11th at 141 and the 16th at 142. |
| 153–156 | the profile-switch key and the lock key, two LEDs each. |
| 157–167 | the left light bar. |
| 168–169 | the mute key, two LEDs. |
| 170–172 | the logo: left, centre, right. |
| 173–178 | the six single-colour status icons, in the order the printed icons run. |
| 179–189 | the right light bar, which nothing reached until it was found here. |

Three things are worth knowing, because none of them is guessable. The **media keys sit in
the top row**, where they are printed, rather than with the other keys of their strobe. The
**number pad's rows are not the ones they look like**: 1, 2, 3 and its enter key come before
4, 5, 6, all in the row that carries A to Ä. And four keys -- the wheel button, the
profile-switch key, the lock key and the mute key -- have **two LEDs each**, which is why 119
keys take 123 slots.

One thing is still open: **in which order the eleven segments of each side bar run.** The
ring is not among them -- ckb-next names its segments by angle, 22.5 degrees through 337.5
clockwise from the top, and the probe found 120 at the top with 127 to the left of it, which
is the same way round -- and neither is the top bar, whose order was walked segment by segment.

It is also the one open question that no effect here can answer. Every segment of a side bar
carries the same x, so a sweep across the keyboard lights the whole bar at once whichever end
157 and 179 are; only an effect that reads y would show a bar running backwards. Settling it
means lighting all 22 with the probe and writing down what comes up, as the rest was settled.

The generator refuses to emit if the measured table and the keys the matrix scans are not the
same set, so a renamed or added key stops the build instead of shifting every LED after it.

## Keycode choices worth knowing

- **G1-G6** are `MC_0`-`MC_5`, the first six of VIA's sixteen macros -- M0 to M5 in the
  application, where they are recorded and stored on the keyboard. They sent `KC_F13`-`KC_F18`
  before, the conventional stand-in for a macro key, which stood in for nothing: no host does
  anything with F13 unless something was bound to it. A macro that has not been recorded is
  empty and the key does nothing, which is the one thing to know before reporting G3 as dead.
- **The two rotations are encoders**, not matrix positions. ckb-next reports them as keys, but
  the stock firmware attaches `P1_28`-`P1_31` to pin interrupts and nothing in the matrix sees
  a turn. The iCUE wheel sets the lighting's brightness and the volume roller the volume,
  both confirmed by turning them; the roller's directions are crossed because it reports the
  opposite way round from the wheel.
- **The wheel's button** is `K100_RGB_EFFECT`, a keycode this keyboard adds, and steps
  through the effects this firmware is built with, skipping the one that paints black because
  the key beside it turns the lighting off. It sets no colour: that one piece of state belongs
  to whoever is configuring the keyboard, and a key that wrote it fought the application for
  it. It used to be `KC_MUTE`, which the mute key beside it already was.
- **`lock`** is `GU_TOGG`: it suppresses the GUI keys, which is what the Windows Lock icon
  above it reports. **`profswitch`**, left of the wheel, is `K100_LIGHTS_TOGGLE`: the profiles
  it switched belong to the stock firmware, and turning the lighting off and on is the nearest
  thing this one has, beside the wheel that sets the brightness. It was `KC_NO`, which VIA
  draws as a blank key.
- **`fn`** is `MO(1)`, and layer 1 passes everything through except five keys. Two are there
  because the keyboard would otherwise have no way to do them at all: **Fn+Esc** is `QK_BOOT`,
  the only route back to Corsair's bootloader a user can take deliberately, and
  **Fn+Backspace** is `EE_CLR`, the only way back to the compiled defaults once something has
  been stored over them. **Fn+N** is `NK_TOGG`: this firmware reports all keys at once, and a
  host that cannot read that reads nothing -- a BIOS or a boot menu, where there is no
  application to put it right with. **Fn+Left** and **Fn+Right** are `RM_SPDD` and `RM_SPDU`,
  the effect speed, which is the one lighting value with no control of its own on the board.
  Layers 2 and 3 exist in the stored keymap, are transparent throughout, and nothing selects
  them.

## The lighting menu is declared, not VIA's own

VIA ships a `qmk_rgb_matrix` menu that a definition can ask for by name, and it is the
wrong menu for this keyboard. Its effect dropdown lists all 45 effects QMK has and sends
the position in that list, while QMK numbers only the effects that are **compiled**. This
firmware compiles four, so 2 is a rainbow here and "Breathing" there. Picking breathing
would start the rainbow and nothing would say so.

The menu is therefore emitted from the same `ANIMATIONS` table the firmware's animation
list comes from. The dropdown holds exactly the effects that exist, at the numbers they
answer to, and the colour and speed controls carry a `showIf` so that each appears only
for the effects that read it.

### Why the rainbows are the keyboard's own

Three of the four effects are, and the two rainbows are there for a reason worth writing
down: **QMK's rainbows read the stored colour.** `CYCLE_ALL` and `CYCLE_LEFT_RIGHT` replace
the hue -- with the clock and with the LED's position -- and keep the saturation they are
handed, which is the saturation the *plain colour* is set to. A keyboard set to white
therefore has white rainbows. The effect runs, every LED is painted, and nothing about it
looks like a rainbow; it is indistinguishable from the solid colour, and the control that
would have put it right was hidden by the `showIf` that said rainbows read no colour.

`K100_RAINBOW` and `K100_RAINBOW_SWEEP` in `rgb_matrix_kb.inc` take the hue the same way and
paint it fully saturated. The colour then belongs to the one effect that is a colour, which
is what the `showIf` says, and one knob no longer reaches across three effects.

### Why "Off" is effect 4 and not zero

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
keyboard's own effects after every built-in, which makes it 4 here, and a `_Static_assert`
in that file holds that number -- and the rainbows' 2 and 3 -- against what the generated
definition sends.

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

### Telling the application what the keyboard did

VIA reads these values when it connects and afterwards knows only what it wrote itself, so
the wheel button stepping the effect and the wheel turning the brightness happen behind its
back: the dropdown goes on naming the effect that was running when the menu loaded, and the
next change made there lands on top of a state the application no longer knows.

VIA has a way out that the keyboard drives. It passes every report that arrives on its own
through a handler looking for a **UI sync request** -- report `0x16`, version 1, then which
channel and value to read again -- and recognising one consumes the report, so it never
reaches the code that matches replies to commands. `via_ui_sync.c` sends one after each of
the three things that change the lighting without the application asking.

The request is not in QMK: its `via_command_id` stops at `0x15`, and `parseUISyncRequest` in
VIA's own bundle is what the layout follows. An application that does not know it loses
nothing by dropping it, unless it is waiting for a reply at that moment, in which case it
would take this for the reply -- which cannot be avoided from the keyboard's side, since a
keyboard cannot ask which version is listening. `tools/k100_via.py` skips these reports for
the same reason.

### A colour per LED, painted from the application

VIA can paint keys: a palette appears beside the keyboard and dragging across it sends one
colour per key, which the keyboard stores. Three things had to be true for that, and the
third is why it could not have been done before the LED order was measured.

**The definition has to name an LED for each key.** The index goes in the key's *legend*,
not in a property on it -- `l128` on the second line, where VIA's reader looks for it after
running the labels through its own alignment table. A property called `li` is what the
transformed definition carries, and a definition that writes one is rejected by the schema.
The application asks for per-key colours only if some key names an LED, and then for every
index from zero to the highest named, so this is also the switch that turns the feature on.

**The protocol is VIA's, on a number the keyboard does not choose.** Painting is
`id_custom_set_value` on channel 0, value 1, with the LED index and the colour behind it, and
reading is the same with `id_custom_get_value`. Channel 0 is the channel VIA leaves to the
keyboard, so this keyboard's own values -- the version, the LED probe, its index -- start at
**2**. The version used to be value 1, which would have answered the painter with a string.
Like the UI sync request, this is read out of VIA's bundle: `getPerKeyRGBMatrix` and
`setPerKeyRGBMatrix` in the application, with `PER_KEY_RGB_CHANNEL_COMMAND = [0, 1]`.

**The colours have to go somewhere.** `VIA_EEPROM_CUSTOM_CONFIG_SIZE` is what QMK gives a
keyboard for its own stored state, and declaring a size is what creates the region. 190 LEDs
at three bytes -- a hue, a saturation and a brightness each. The region comes out of what the
dynamic keymap leaves, so the macro buffer shrinks by the same 570 bytes, from 855.

### Turning one key off, and the brush that does it

The painter sends a hue and a saturation, and neither can say *off*: zero saturation is white.
So each stored colour carries a **brightness** of its own, and the one a painted key gets is a
property of the brush rather than of the stroke -- `K100_VIA_PAINT_VALUE`, a dropdown beside
the palette with `Full`, `Bright`, `Half`, `Dim` and `Off`. Pick one, paint, and every key
painted afterwards takes it. A dropdown and not a range for the same reason the LED probe has
one: VIA draws a range with no number beside it, and "how bright is the brush" is a question a
control has to be able to answer.

The brush is not stored. It is not a property of the keyboard, and one that came back set to
off after a power cycle would make the next stroke look like a keyboard that had stopped
working.

In the effect, a stored brightness **scales** the one the wheel sets rather than replacing it:
the wheel still dims the whole keyboard, a key set to half stays half of whatever that is, and
a key set to zero is off at any setting. The application cannot see any of this -- it reads a
hue and a saturation -- so a key that is off still shows its colour in VIA's drawing.

One consequence worth writing down: the default is no longer what an erased region reads as.
Zero bytes mean *off everywhere* now, so the defaults are written rather than assumed, in both
places that reset them.

**What shows them is an effect of the keyboard's own**, `K100_PER_KEY`, which gives every LED
the colour stored for it at the brightness the wheel sets. It is the fifth entry in the
dropdown and the last of the four effects declared in `rgb_matrix_kb.inc`, because the numbers
VIA sends are the order they are declared in and appending is the one change that moves
nothing already in use. Painting without selecting it stores colours that nothing draws, which
is exactly what it looks like: a painter that does nothing.

**The colours are read before QMK decides to wipe the EEPROM, which cost a measurement to
find.** `via_init()` runs before `quantum_init()`, and `quantum_init()` is where QMK notices
that `EECONFIG_MAGIC_NUMBER` has moved and erases the whole EEPROM. So on the first boot of a
firmware whose stored shape changed, `via_init_kb()` has already read the region -- and what
it read was the *previous* firmware's bytes. Measured, with the table at a new address: LED 1
came back with saturation 41, which is `0x29`, which is the low byte of `KC_ESC` where the old
dynamic keymap's second keycode had been. `eeconfig_init_kb()` is the hook that runs from
inside that erase, and putting the RAM copy back in step there is the fix.

Two further details are worth knowing. **A key with two LEDs gets the colour on both**: the
application paints one index, and `per_key_colour.c` gives the same colour to every other LED
at the same position -- which is how it finds them, because two lights under one key carry one
position and the generator refuses to emit a layout where two LEDs share a position without
sharing a key. And **only keys can be painted**, because only keys are drawn: the LightEdge
bars, the logo and the wheel's ring have nothing to click on. The storage covers them anyway,
so a menu that sets them is an addition rather than another change to the shape of what is
stored -- and every such change costs every keyboard its stored keymap.

## Validation

The generated files are checked structurally, not eyeballed:

- 119 layout entries with unique matrix positions, all inside 8 x 18
- 190 RGB entries with coordinates inside QMK's 224 x 64 space, every `matrix` reference
  resolving to a real key
- the VIA definition carrying the identical position set and matrix dimensions as
  `keyboard.json`

### The definition is checked with VIA's own reader

Those checks are this project's. They say the definition agrees with the firmware; they do
not say the application will take it. `@the-via/reader` is the package the application reads
definitions with, so that question has an answer here rather than in front of an empty Design
tab:

```sh
npm ci --prefix tools/via-definition-check
node tools/via-definition-check/check.mjs
```

It applies the three gates the application applies -- the schema guard for the file, the
transform, where the layout, the key bounds and the menus are checked, and the guard for the
transformed form, which is what gets stored and read back -- and then prints the menu and the
custom keycodes as the application will see them, with the numbers each control sends. A
definition that is merely valid can still send the wrong number, so the numbers are the part
worth reading.

ajv's strict-mode warnings on the way past are about VIA's schema, not about this definition,
and go to stderr.

The generator also refuses to emit at all if a key lacks a QMK keycode or an LED index, if a
HID code matches more or fewer than one geometry key, if a key would land at two matrix
positions, or if the scanning keys missing from the matrix are anything other than the two
encoders' rotations.
