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

pub fn enter_bootloader(device: Device) -> Result<()> {
    let mode = device.operating_mode()?;
    if mode == OperatingMode::Bootloader {
        println!("Already in bootloader mode.");
        return Ok(());
    }

    println!("Current mode: {mode:?}");
    println!("Requesting bootloader mode (SET OPERATING_MODE 3)...");
    device.request_operating_mode(OperatingMode::Bootloader)?;
    drop(device);

    let device = reconnect().context("keyboard did not come back after the bootloader request")?;
    let landed = device.operating_mode()?;

    if landed != OperatingMode::Bootloader {
        bail!("keyboard came back in {landed:?} rather than Bootloader");
    }

    println!("In bootloader mode, {}-byte reports. Return with `exit-bootloader`.",
             device.report_size);
    Ok(())
}

/// Leaves the bootloader by asking for self-operated mode.
///
/// The vendor path is `Bootloader -> SelfOperated -> HostControlled`, not a direct jump; the
/// updater logs "Transitioning from bootloader to host controlled via self-operated mode".
/// Only the first leg belongs here. Host control is what the updater wants next for its own
/// sake, and leaving a keyboard host-controlled with nothing driving it is worse than leaving
/// it in the state it powers on into.
pub fn exit_bootloader(device: Device) -> Result<()> {
    let mode = device.operating_mode()?;
    if mode != OperatingMode::Bootloader {
        println!("Not in bootloader mode (currently {mode:?}); nothing to do.");
        return Ok(());
    }

    println!("Leaving bootloader for self-operated mode...");
    device.request_operating_mode(OperatingMode::SelfOperated)?;
    drop(device);
    sleep(SETTLE);

    let device = reconnect().context("keyboard did not come back after leaving bootloader")?;
    let landed = device.operating_mode()?;

    if landed == OperatingMode::Bootloader {
        bail!("keyboard is still in bootloader mode");
    }

    println!("Operating mode is now {landed:?}, {}-byte reports.", device.report_size);
    match device.firmware_version() {
        Ok(version) => println!("Running firmware {version}."),
        Err(error) => println!("Could not read the firmware version: {error:#}"),
    }
    Ok(())
}

/// Waits for the keyboard to come back and answer after a mode change.
///
/// Appearing in sysfs is not the same as being ready: the device is listed before its
/// endpoints will carry a transfer, and opening it at that moment gets a handle that times
/// out on the first request. So the condition polled here is a successful command, not a
/// successful open, which is the thing the caller actually needs.
///
/// Opening from scratch each time is deliberate too. The bus address changes across
/// re-enumeration, and so can the report size -- the bootloader's endpoints are a sixteenth
/// of the application's -- so a handle from before the transition describes the wrong device.
fn reconnect() -> Result<Device> {
    let deadline = Instant::now() + RECONNECT_TIMEOUT;
    let mut last_error = None;

    while Instant::now() < deadline {
        sleep(POLL_INTERVAL);

        match Device::open().and_then(|device| device.operating_mode().map(|_| device)) {
            Ok(device) => return Ok(device),
            Err(error) => last_error = Some(error),
        }
    }

    Err(last_error.unwrap_or_else(|| anyhow::anyhow!("keyboard did not reappear")))
        .context(format!("waited {RECONNECT_TIMEOUT:?} for the keyboard to answer again"))
}

/// Buffer handle used for the firmware file.
///
/// Any free handle would do; one is what the probe uses and what the updater
/// uses, and a handle left bound makes every later open fail with
/// `INVALID_STATE`, so there is no reason to spread them around.
const FIRMWARE_HANDLE: u8 = 1;

/// Payload bytes that fit in one report, from the updater's own arithmetic.
///
/// It computes `reportLength - 4 - 1 - 4`: a four-byte Bragi header, the report
/// id, and the four-byte total length that rides in the first chunk. A chunk
/// shorter than the device can take always works, while one longer than it
/// expects does not, so the conservative reading is the one used.
///
/// `report` has to come from the device. The application declares 1024-byte
/// endpoints and **the bootloader declares 64**, so a figure derived from the
/// application is sixteen times too large for the program that actually does
/// the writing.
fn chunk_size(report: usize) -> Result<usize> {
    report.checked_sub(4 + 1 + 4).filter(|&c| c > 0).ok_or_else(|| {
        anyhow::anyhow!("a {report}-byte report leaves no room for a firmware chunk")
    })
}

/// What a flash would do, without doing it.
pub struct Plan {
    pub total: usize,
    pub chunks: usize,
    pub chunk: usize,
    pub report: usize,
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
    let chunk = chunk_size(device.report_size)?;
    let plan = Plan {
        total: image.len(),
        chunks: image.len().div_ceil(chunk),
        chunk,
        report: device.report_size,
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
    let mut chunks = image.chunks(plan.chunk);

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
