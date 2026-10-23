#!/usr/bin/env node
// Checks the generated VIA definition with VIA's own reader.
//
// The definition is written by tools/generate-qmk-layout.py and has to be
// loaded into the application by hand once. Whether the application accepts it
// is therefore found out at the worst possible moment -- in front of an empty
// Design tab, with nothing saying why. @the-via/reader is the package the
// application reads definitions with, so the same answer is available here.
//
//     npm install --prefix tools/via-definition-check
//     node tools/via-definition-check/check.mjs
//
// Three gates, in the order the application applies them: the schema guard for
// the file, the transform, which is where the layout, the key bounds and the
// menus are checked and where a bad definition throws, and the guard for the
// transformed form, which is what the application stores and reads back.
//
// The dropdown numbers in the output are worth reading rather than skimming.
// QMK numbers only the effects a build compiles, the definition carries those
// numbers as literals, and nothing in either file can catch a disagreement --
// only the firmware's own _Static_assert can, and only for the one effect it
// guards. See docs/qmk-layout.md.
//
// ajv prints strict-mode warnings about VIA's schema on the way past. They are
// about that schema, not about this definition, and go to stderr.

import { readFile } from 'node:fs/promises';

const DEFINITION = new URL('../../via/k100-iso-de.json', import.meta.url);

// The schema is a tree of anyOf branches, so one wrong property is reported
// once per branch it fails: a dropdown with its options removed comes back as
// two hundred lines. Two things make that readable. "must match a schema in
// anyOf" is dropped, because it only says that a branch failed and never which
// property was wrong, and ajv's own order is kept, because it reports the
// innermost mismatch before the branches it brings down with it -- so the lines
// that name the mistake arrive first.
const SHOWN = 12;

function report(errors = []) {
  const complaints = [...new Set(errors
    .filter(({ message }) => !message.includes('match a schema in anyOf'))
    .map(({ instancePath, message }) => `${instancePath || '/'} ${message}`))];

  for (const complaint of complaints.slice(0, SHOWN)) {
    console.log(`  ${complaint}`);
  }
  if (complaints.length > SHOWN) {
    console.log(`  ... and ${complaints.length - SHOWN} more, the same mistake `
      + 'seen through other branches of the schema');
  }
}

const reader = await import('@the-via/reader').catch((error) => {
  if (error.code !== 'ERR_MODULE_NOT_FOUND') throw error;
  console.error('@the-via/reader is not installed. It is pinned in the '
    + 'package.json beside this script:\n\n'
    + '    npm install --prefix tools/via-definition-check\n');
  process.exit(2);
});

const path = process.argv[2] ? new URL(process.argv[2], `file://${process.cwd()}/`) : DEFINITION;
const definition = JSON.parse(await readFile(path, 'utf8'));

console.log(`checking ${path.pathname}`);

if (!reader.isKeyboardDefinitionV3(definition)) {
  console.log('the schema guard rejected it:');
  report(reader.isKeyboardDefinitionV3.errors);
  process.exit(1);
}
console.log('  the schema guard accepts it as a v3 keyboard definition');

let via;
try {
  via = reader.keyboardDefinitionV3ToVIADefinitionV3(definition);
} catch (error) {
  console.log(`the transform rejected it: ${error.message}`);
  process.exit(1);
}
console.log('  the transform accepts the layout, the key bounds and the menus');

if (!reader.isVIADefinitionV3(via)) {
  console.log('the transformed form was rejected, so the application could not '
    + 'store and reload it:');
  report(reader.isVIADefinitionV3.errors);
  process.exit(1);
}
console.log('  the transformed form validates, so it survives being stored');

console.log(`
name             ${via.name}
stored under     ${via.vendorProductId} (0x${via.vendorProductId.toString(16)})
matrix           ${via.matrix.rows} rows x ${via.matrix.cols} cols
keys drawn       ${via.layouts.keys.length}`);

// What the application reads to decide whether this keyboard has per-key colours, and then
// which index it paints. It survives the transform or it does not, and a definition that
// lost it looks exactly like one that never asked for it.
const painted = via.layouts.keys.filter((key) => key.li !== undefined).map((key) => key.li);
console.log(`keys with an LED  ${painted.length} of ${via.layouts.keys.length}`
  + (painted.length
    ? `, indices ${Math.min(...painted)} to ${Math.max(...painted)}`
      + `, ${new Set(painted).size} of them distinct`
    : ''));

console.log('\nkeycodes this keyboard adds, at the numbers VIA sends:');
(via.customKeycodes ?? []).forEach(({ name, title, shortName }, index) =>
  console.log(`  CUSTOM(${index})  ${name}  "${title}"  short "${shortName ?? ''}"`));

for (const menu of via.menus ?? []) {
  if (typeof menu === 'string') {
    console.log(`\nbuilt-in menu: ${menu}`);
    continue;
  }
  console.log(`\nmenu ${menu.label}`);
  for (const group of menu.content) {
    console.log(`  ${group.label}`);
    for (const { label, type, content, options, showIf } of group.content) {
      const [value, channel, number] = content;
      console.log(`    ${label} (${type}) channel ${channel} value ${number}  ${value}`
        + `${options ? `  ${JSON.stringify(options)}` : ''}`
        + `${showIf ? `  only when ${showIf}` : ''}`);
    }
  }
}
