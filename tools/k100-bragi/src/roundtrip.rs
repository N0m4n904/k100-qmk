//! The recovery round trip from `docs/recovery.md` rule 2.
//!
//! Each transition makes the device re-enumerate, so a handle does not survive one. These
//! functions send the command, then wait for the keyboard to come back and confirm the mode
//! it landed in, rather than assuming the write took effect.

use std::thread::sleep;
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};

use crate::device::Device;
use crate::protocol::{self, OperatingMode, Request};

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

/// Buffer handle used for the firmware file.
///
/// Any free handle would do; one is what the probe uses and what the updater
/// uses, and a handle left bound makes every later open fail with
/// `INVALID_STATE`, so there is no reason to spread them around.
const FIRMWARE_HANDLE: u8 = 1;

/// Payload bytes per report.
///
/// The report is 1024 bytes and this device frames a request in two, so 1017
/// would fit in the first chunk. The updater computes `reportLength - 4 - 1 -
/// 4`, which assumes a four-byte header, and that is the figure used here: a
/// chunk shorter than the device can take always works, while one longer than
/// it expects does not, and the cost is a few more transfers over 23 KB.
const CHUNK: usize = 1024 - 4 - 1 - 4;

/// What a flash would do, without doing it.
pub struct Plan {
    pub total: usize,
    pub chunks: usize,
}

/// Writes a firmware image, or reports what it would write.
///
/// The sequence is `writeFile` from the updater bundle: open the firmware file,
/// declare the total length with the first chunk, send the remainder, then
/// apply. A non-zero status at any step aborts rather than continuing, because
/// a partial image applied is worse than no image written.
///
/// With `commit` false nothing is sent that changes the device: the file is not
/// opened and no buffer is written. That is the default everywhere above this
/// function.
pub fn flash(device: &Device, image: &[u8], commit: bool) -> Result<Plan> {
    let plan = Plan {
        total: image.len(),
        chunks: image.len().div_ceil(CHUNK),
    };

    if !commit {
        return Ok(plan);
    }

    // Opening the firmware file is the first step that implies write intent, so
    // it is also the first step gated behind `commit`.
    let opened = device.open_file(FIRMWARE_HANDLE, protocol::FILE_ID_FIRMWARE)?;
    if opened.status() != protocol::Status::Success {
        bail!("opening the firmware file returned {:?}", opened.status());
    }

    // Unbind whatever happens, successful or not: a handle left bound makes
    // every later open fail, which would leave the keyboard unwritable without
    // a power cycle.
    let result = write_image(device, image, &plan);
    let unbound = device.unbind_buffers(FIRMWARE_HANDLE);
    result?;
    unbound.context("unbinding the firmware buffer after writing")?;

    let applied = device.exchange(&Request::apply_firmware_extended())?;
    if applied.status() != protocol::Status::Success {
        bail!("applying the firmware returned {:?}", applied.status());
    }

    Ok(plan)
}

fn write_image(device: &Device, image: &[u8], plan: &Plan) -> Result<()> {
    let mut chunks = image.chunks(CHUNK);

    let first = chunks.next().unwrap_or(&[]);
    let begun = device.exchange(&Request::write_buffer_begin(
        FIRMWARE_HANDLE,
        u32::try_from(image.len()).context("image is larger than 4 GiB")?,
        first,
    ))?;
    if begun.status() != protocol::Status::Success {
        bail!("beginning the firmware write returned {:?}", begun.status());
    }

    for (index, chunk) in chunks.enumerate() {
        let written = device.exchange(&Request::write_buffer(FIRMWARE_HANDLE, chunk))?;
        if written.status() != protocol::Status::Success {
            bail!(
                "writing chunk {} of {} returned {:?}",
                index + 2,
                plan.chunks,
                written.status()
            );
        }
    }

    Ok(())
}
