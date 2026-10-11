# udev rules

`99-corsair-k100.rules` grants a normal user access to the keyboard, so `k100-bragi` does not
need root.

```sh
sudo cp udev/99-corsair-k100.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
# then replug the keyboard
```

On NixOS, which has no writable `/etc/udev/rules.d`:

```nix
services.udev.extraRules = builtins.readFile ./udev/99-corsair-k100.rules;
```

The rules assume a `users` group, matching the `TAG+="uaccess"` and `TAG+="udev-acl"`
convention; adjust `GROUP=` if your system uses something else.

## Verified against the keyboard

With these rules in place, `k100-bragi info` reads the device over usbdevfs:

```
K100 RGB (OPX optical)
  product id        0x1b7c
  reported pid      0x1b7c
  firmware          0.32.6
  bootloader        0.10.45
  physical layout   Iso
  operating mode    SelfOperated
```

**Replug the keyboard first.** The vendor command endpoint can be left unresponsive by a failed
HID client, and every transfer then times out even though opening and claiming succeed. A power
cycle clears it; see `docs/bragi-firmware-update.md`.

## The `usb` rule is the one that matters

`k100-bragi` talks to `/dev/bus/usb/...` through usbdevfs, not to `/dev/hidraw*`. That is not a
preference — **hidraw cannot write to this keyboard's Bragi interface at all**, and the usbdevfs
path is the only one that works. `docs/bragi-firmware-update.md` has the measurements.

It matches on **vendor only**, with no product id. That is deliberate: a keyboard in bootloader
mode may report a product id nothing here recognises, and that is precisely the moment access has
to work. `Device::open()` is built the same way, preferring a known product and falling back to
any Corsair device.

## What these rules do not do

**They do not make Corsair's web updater work on Linux.** This has now been tested twice, and the
second test is the conclusive one:

1. First test: the hidraw nodes were *already* `crw-rw-rw-` plus an ACL — world writable — and
   every write to the Bragi interface failed with `ETIMEDOUT` (errno 110) at every report length.
   Chromium's WebHID, which uses hidraw `write()` for output reports on Linux, failed with
   `NotAllowedError: Failed to write the report.`
2. Second test, with the `hidraw` rule above actually installed: **identical failure.** The
   updater cannot get past its first command, `GET(OPERATING_MODE)` — `{id: 1, cmd: 2, propId: 3}`
   in its own log.

So the obvious reading of `NotAllowedError` is wrong. It is Chromium's generic wrapper around a
write that failed, not a permission denial, and no amount of permission fixes it. The write
reaches the same wall hidraw does.

(The `iCUE WebSocket connection failed` lines in that log are unrelated: the page probes for a
local iCUE installation, which does not exist on Linux.)

Granting permission was worth ruling out, because it was the one cheap explanation that would have
made firmware recovery possible from this machine. It is ruled out. **Recovery through Corsair's
own updater needs Windows**, and that is a fact to plan around rather than retest.
