//! The recovery round trip from `docs/recovery.md` rule 2.
//!
//! Each transition makes the device re-enumerate, so a handle does not survive one. These
//! functions send the command, then wait for the keyboard to come back and confirm the mode
//! it landed in, rather than assuming the write took effect.

use std::thread::sleep;
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};

use crate::device::Device;
use crate::protocol::OperatingMode;

/// The updater allows 20 s for a device to come back after a firmware apply.
const RECONNECT_TIMEOUT: Duration = Duration::from_secs(20);

/// It also settles for a second between transitions.
const SETTLE: Duration = Duration::from_secs(1);

const POLL_INTERVAL: Duration = Duration::from_millis(250);

pub fn enter_bootloader(device: &Device) -> Result<()> {
    let mode = device.operating_mode()?;
    if mode == OperatingMode::Bootloader {
        println!("Already in bootloader mode.");
        return Ok(());
    }

    println!("Current mode: {mode:?}");
    println!("Requesting bootloader mode (SET OPERATING_MODE 3)...");
    device.set_operating_mode(OperatingMode::Bootloader)?;

    let device = reconnect().context("keyboard did not come back after the bootloader request")?;
    let landed = device.operating_mode()?;

    if landed != OperatingMode::Bootloader {
        bail!("keyboard came back in {landed:?} rather than Bootloader");
    }

    println!("In bootloader mode. Return with `exit-bootloader`.");
    Ok(())
}

/// The vendor path out is `Bootloader -> SelfOperated -> HostControlled`, not a direct jump;
/// the updater logs "Transitioning from bootloader to host controlled via self-operated mode".
pub fn exit_bootloader(device: &Device) -> Result<()> {
    let mode = device.operating_mode()?;
    if mode != OperatingMode::Bootloader {
        println!("Not in bootloader mode (currently {mode:?}); nothing to do.");
        return Ok(());
    }

    println!("Leaving bootloader via self-operated mode...");
    device.set_operating_mode(OperatingMode::SelfOperated)?;
    sleep(SETTLE);

    let device = reconnect().context("keyboard did not come back after leaving bootloader")?;
    device.set_operating_mode(OperatingMode::HostControlled)?;
    sleep(SETTLE);

    let landed = device.operating_mode()?;
    println!("Operating mode is now {landed:?}.");

    if landed == OperatingMode::Bootloader {
        bail!("keyboard is still in bootloader mode");
    }
    Ok(())
}

/// Polls for the keyboard to reappear on the bus after a mode change.
fn reconnect() -> Result<Device> {
    let deadline = Instant::now() + RECONNECT_TIMEOUT;
    let mut last_error = None;

    while Instant::now() < deadline {
        sleep(POLL_INTERVAL);

        // Re-opens from scratch: the bus address changes across re-enumeration.
        match Device::open() {
            Ok(device) => return Ok(device),
            Err(error) => last_error = Some(error),
        }
    }

    Err(last_error.unwrap_or_else(|| anyhow::anyhow!("keyboard did not reappear")))
        .context(format!("waited {RECONNECT_TIMEOUT:?}"))
}

/// Writing an image is deliberately not implemented yet.
///
/// The command set is known — `OPEN_FILE`, then `WRITE_BUFFER_BEGIN` carrying a 4-byte
/// little-endian total length plus the first chunk, then `WRITE_BUFFER` for the remainder,
/// then `APPLY_FIRMWARE_EXTENDED` (22) as the K100 manifest's `apply-extended` — but two
/// details are still unresolved from static reading alone:
///
///   * the file ID that selects the firmware buffer, and
///   * whether this device frames those commands with a two- or four-byte header.
///
/// `info` determines the header layout empirically, so run it first. Guessing either value
/// and writing anyway is the one mistake that could cost the keyboard, so this stays a stub
/// until both are confirmed against the device.
#[allow(dead_code)]
pub fn flash(_device: &Device, _image: &[u8]) -> Result<()> {
    bail!(
        "flashing is not implemented yet: the firmware file ID and this device's header \
         layout must be confirmed first (run `info`), see docs/bragi-firmware-update.md"
    )
}
