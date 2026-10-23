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
use crate::transport;

/// The updater allows 20 s for a device to come back after a firmware apply.
const RECONNECT_TIMEOUT: Duration = Duration::from_secs(20);

/// It also settles for a second between transitions.
const SETTLE: Duration = Duration::from_secs(1);

const POLL_INTERVAL: Duration = Duration::from_millis(250);

/// Pauses the updater takes during a write, kept because they are almost certainly there to
/// let the device erase flash rather than for comfort.
const AFTER_BEGIN: Duration = Duration::from_secs(1);
const AFTER_UNBIND: Duration = Duration::from_millis(500);

/// `apply-fw-timeout` from the K100's own manifest: how long the device may be away after an
/// apply before it is considered gone.
const APPLY_TIMEOUT: Duration = Duration::from_secs(8);

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

/// How many payload bytes each kind of write report carries.
///
/// **These sizes have to be exact, not conservative.** Nothing on the wire says how much of a
/// report is payload: every report goes out padded to its full length, and the device knows
/// only the total it was given up front. So it must take each report to be full, and a chunk
/// one byte smaller than the device assumes shifts the rest of the image by one byte.
///
/// Both figures therefore come from the request encoding itself. The first report also
/// carries the four-byte total, so it fits less:
///
/// ```text
/// write_buffer_begin   08 06 <handle> <total:4> <payload…>     report - 2 - 1 - 4
/// write_buffer         08 07 <handle> <payload…>               report - 2 - 1
/// ```
///
/// Corsair's `writeFile` computes the same two numbers by different arithmetic --
/// `reportLength - 4 - 1 - sizeWidth` and `reportLength - 1 - sizeWidth`, with `sizeWidth`
/// 2 for this protocol generation -- and lands on 57 and 61 for a 64-byte report, exactly
/// what the encoding above gives. Two independent routes to the same pair is the reason to
/// trust them.
fn chunk_sizes(report: usize) -> Result<(usize, usize)> {
    let capacity = |overhead: usize| {
        report.checked_sub(protocol::REQUEST_HEADER + overhead).filter(|&c| c > 0)
    };

    let first = capacity(1 + 4);
    let rest = capacity(1);

    match (first, rest) {
        (Some(first), Some(rest)) => Ok((first, rest)),
        _ => bail!("a {report}-byte report leaves no room for a firmware chunk"),
    }
}

/// What a flash would do, without doing it.
pub struct Plan {
    pub total: usize,
    pub chunks: usize,
    pub first_chunk: usize,
    pub chunk: usize,
    pub report: usize,
    /// CRC-32/MPEG-2 over the whole image, which the apply command carries and the device
    /// checks against what it received.
    pub checksum: u32,
}

impl Plan {
    fn new(image: &[u8], report: usize) -> Result<Self> {
        let (first_chunk, chunk) = chunk_sizes(report)?;
        let remainder = image.len().saturating_sub(first_chunk);

        Ok(Self {
            total: image.len(),
            chunks: 1 + remainder.div_ceil(chunk),
            first_chunk,
            chunk,
            report,
            checksum: protocol::crc32_mpeg2(image),
        })
    }
}

/// Writes a firmware image, or reports what it would write.
///
/// The sequence is `writeFile` followed by the apply step, both from the updater bundle:
/// open the firmware file, declare the total length with the first chunk, send the
/// remainder, unbind, then apply with the image's checksum. A non-zero status at any step
/// aborts rather than continuing, because a partial image applied is worse than no image
/// written.
///
/// With `commit` false nothing is sent that changes the device: the file is not opened and no
/// buffer is written. That is the default everywhere above this function.
pub fn flash(device: &Device, image: &[u8], commit: bool) -> Result<Plan> {
    let plan = Plan::new(image, device.report_size)?;

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
    sleep(AFTER_UNBIND);

    apply(device, &plan)?;
    Ok(plan)
}

/// What was found running after an apply.
pub enum Running {
    /// It speaks Corsair's protocol and reports this firmware version.
    Bragi(String),
    /// It came back without Corsair's protocol, declaring this device version.
    ///
    /// Not a failure, and not an edge case either: an image that does not implement
    /// Bragi -- this project's own firmware -- can look like nothing else. There is
    /// no version to ask it for, so the one it puts in its USB descriptor is what
    /// there is, and that is the number to compare against.
    Foreign(String),
}

/// Waits out the reboot an apply triggers and reports what the keyboard is running.
///
/// This is the only check that means anything: every status byte up to here says a report was
/// accepted, and none of them says the image boots. What answers afterwards does.
pub fn confirm_after_apply(product: u16) -> Result<Running> {
    println!("  waiting up to {}s for the keyboard to reboot", APPLY_TIMEOUT.as_secs());
    sleep(APPLY_TIMEOUT);

    match reconnect() {
        Ok(device) => {
            println!("  back on the bus, operating mode {:?}", device.operating_mode()?);
            device.firmware_version().map(Running::Bragi)
        }
        Err(silent) => {
            // Nothing answered, so the question is whether anything came back at all.
            let version = transport::find(protocol::CORSAIR_VID, &[product])?
                .into_iter()
                .next()
                .and_then(|found| found.version)
                .ok_or(silent)
                .context("the keyboard did not come back after the apply")?;

            println!("  back on the bus, and not answering Corsair's protocol");
            Ok(Running::Foreign(describe_version(version)))
        }
    }
}

/// `bcdDevice` in the form a version is written in: 0x0010 is 0.1.0.
///
/// The descriptor packs it as binary-coded decimal, two digits for the leading number
/// and one each for the other two, which is also how QMK generates it from a
/// keyboard's `device_version`.
fn describe_version(version: u16) -> String {
    let major = (version >> 12) * 10 + ((version >> 8) & 0xf);
    format!("{}.{}.{}", major, (version >> 4) & 0xf, version & 0xf)
}

/// Commits the written image and accepts that the device may never answer.
///
/// The K100's manifest sets `reboot: true`, so the device restarts on a successful apply and
/// a reply can be lost to the re-enumeration. Corsair's own updater treats that as success --
/// "Apply extended finished without response" -- and only a reply that *arrives* and carries
/// a non-zero status is a failure. Reading a timeout as failure here would report a successful
/// flash as a broken one, and tempt a second write over a good image.
fn apply(device: &Device, plan: &Plan) -> Result<()> {
    let request = Request::apply_firmware_extended(
        protocol::FILE_ID_FIRMWARE,
        protocol::ImageType::Firmware,
        plan.checksum,
    );

    match device.exchange(&request) {
        Ok(response) if response.status().is_success() => {
            println!("  apply acknowledged");
            Ok(())
        }
        Ok(response) => bail!("applying the firmware returned {:?}", response.status()),
        Err(_) => {
            println!("  apply sent, no reply -- expected, the keyboard reboots on success");
            Ok(())
        }
    }
}

/// Streams the image in, first chunk and total length together, then the rest.
///
/// The two chunk sizes differ because the first report also carries the total, so the image
/// is split at `first_chunk` rather than chunked uniformly. Getting that split wrong shifts
/// every byte after it; see [`chunk_sizes`].
fn write_image(device: &Device, image: &[u8], plan: &Plan) -> Result<()> {
    let split = plan.first_chunk.min(image.len());
    let (first, rest) = image.split_at(split);

    let begun = device.exchange(&Request::write_buffer_begin(
        FIRMWARE_HANDLE,
        u32::try_from(image.len()).context("image is larger than 4 GiB")?,
        first,
    ))?;
    if begun.status() != protocol::Status::Success {
        bail!("beginning the firmware write returned {:?}", begun.status());
    }

    // The updater waits a second here. The device has the total now and has to make room for
    // it, which on this chip means erasing flash sectors before the next report arrives.
    sleep(AFTER_BEGIN);

    for (index, chunk) in rest.chunks(plan.chunk).enumerate() {
        let written = device.exchange(&Request::write_buffer(FIRMWARE_HANDLE, chunk))?;
        if written.status() != protocol::Status::Success {
            bail!(
                "writing chunk {} of {} returned {:?}",
                index + 2,
                plan.chunks,
                written.status()
            );
        }

        if (index + 2) % 250 == 0 {
            println!("  {} of {} chunks", index + 2, plan.chunks);
        }
    }

    Ok(())
}

/// Reads the firmware file back out of the keyboard and compares it.
///
/// Read-only, and the cheapest way to answer a question that otherwise needs
/// guesswork: is the image in flash the image that was sent? Every status byte
/// during a write says a report was *accepted*, and none of them says the bytes
/// landed where they should. If the chunking is wrong the CRC in flash will not
/// match the header, the bootloader's own validation fails, and it refuses to
/// start an image that this tool reported writing successfully.
///
/// `DESCRIBE_BUFFER` gives the length, then `READ_BUFFER` walks the buffer the
/// same way the write side walks it, with no offset in the request.
pub fn read_firmware(device: &Device, expected: &[u8]) -> Result<()> {
    let opened = device.open_file(FIRMWARE_HANDLE, protocol::FILE_ID_FIRMWARE)?;
    if opened.status() != protocol::Status::Success {
        bail!("opening the firmware file returned {:?}", opened.status());
    }

    let result = read_back(device, expected);
    let unbound = device.unbind_buffers(FIRMWARE_HANDLE);
    let outcome = result;
    unbound.context("unbinding the firmware buffer after reading")?;
    outcome
}

fn read_back(device: &Device, expected: &[u8]) -> Result<()> {
    let described = device.exchange(&Request::describe_buffer(FIRMWARE_HANDLE))?;
    println!("  describe-buffer status {:?}, payload {:02x?}",
             described.status(), described.payload().get(..8).unwrap_or(described.payload()));

    let mut read = Vec::with_capacity(expected.len());
    while read.len() < expected.len() {
        let response = device.exchange(&Request::read_buffer(FIRMWARE_HANDLE))?;
        if response.status() != protocol::Status::Success {
            bail!("read-buffer returned {:?} after {} bytes",
                  response.status(), read.len());
        }

        let chunk = response.payload();
        if chunk.is_empty() {
            bail!("read-buffer returned no data after {} bytes", read.len());
        }

        let wanted = (expected.len() - read.len()).min(chunk.len());
        read.extend_from_slice(&chunk[..wanted]);
    }

    match read.iter().zip(expected).position(|(a, b)| a != b) {
        None => {
            println!("  {} bytes read back identical to the image that was sent",
                     read.len());
            Ok(())
        }
        Some(at) => bail!(
            "flash differs from the image sent, first at offset {at:#x}: \
             flash has {:#04x}, the image has {:#04x}",
            read[at], expected[at]
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The pair that must be exact rather than safe.
    ///
    /// Pinned to the figures Corsair's own `writeFile` computes, by its arithmetic rather
    /// than ours: `reportLength - 4 - 1 - sizeWidth` for the first report and
    /// `reportLength - 1 - sizeWidth` for the rest, with `sizeWidth` 2 on this protocol
    /// generation. Our encoding reaches the same numbers from the wire layout, and the point
    /// of the test is that it keeps doing so.
    #[test]
    fn chunk_sizes_match_the_vendor_arithmetic() {
        let vendor = |report: usize, size_width: usize| {
            (report - 4 - 1 - size_width, report - 1 - size_width)
        };

        assert_eq!(chunk_sizes(64).unwrap(), (57, 61));
        assert_eq!(chunk_sizes(64).unwrap(), vendor(64, 2));

        assert_eq!(chunk_sizes(1024).unwrap(), (1017, 1021));
        assert_eq!(chunk_sizes(1024).unwrap(), vendor(1024, 2));
    }

    /// A report too small to carry anything is refused rather than producing a zero-byte
    /// chunk, which would loop forever.
    #[test]
    fn refuses_a_report_with_no_room() {
        assert!(chunk_sizes(7).is_err());
        assert!(chunk_sizes(0).is_err());
    }

    /// The chunk count has to match how the image is actually split, or progress reporting
    /// lies and an off-by-one in the split goes unnoticed.
    #[test]
    fn the_plan_counts_the_chunks_it_will_send() {
        for total in [1usize, 57, 58, 117, 118, 23524, 150908] {
            let image = vec![0u8; total];
            let plan = Plan::new(&image, 64).unwrap();

            let sent = 1 + image[plan.first_chunk.min(total)..].chunks(plan.chunk).count();
            assert_eq!(plan.chunks, sent, "total {total}");
        }
    }
}
