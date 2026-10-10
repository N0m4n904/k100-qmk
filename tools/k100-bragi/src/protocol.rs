//! Bragi wire protocol: pure data, no I/O.
//!
//! Constants are taken from Corsair's own web firmware updater bundle, cross-checked
//! against ckb-next's independently reverse-engineered values. See
//! `docs/bragi-firmware-update.md` for provenance of each enum.

use anyhow::{bail, Result};

/// First byte of every Bragi report body. ckb-next calls it the magic byte.
///
/// It is *not* a HID report ID: the K100's vendor interface declares no Report ID item, so
/// its reports are unnumbered and this is ordinary payload. Verified by reading the device's
/// report descriptor — see the Linux transport section of `docs/bragi-firmware-update.md`.
pub const BRAGI_MAGIC: u8 = 0x08;

/// What HID APIs expect in byte 0 when a device uses unnumbered reports.
pub const HID_REPORT_ID: u8 = 0x00;

pub const CORSAIR_VID: u16 = 0x1b1c;

/// The three K100 RGB SKUs. Only `OpticalOpx` has been researched in depth.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Model {
    OpticalOpx,
    Mechanical,
    OpticalVariant,
}

impl Model {
    pub const fn pid(self) -> u16 {
        match self {
            Self::OpticalOpx => 0x1b7c,
            Self::Mechanical => 0x1b7d,
            Self::OpticalVariant => 0x1bc5,
        }
    }

    pub const ALL: [Self; 3] = [Self::OpticalOpx, Self::Mechanical, Self::OpticalVariant];

    pub fn from_pid(pid: u16) -> Option<Self> {
        Self::ALL.into_iter().find(|m| m.pid() == pid)
    }

    pub const fn description(self) -> &'static str {
        match self {
            Self::OpticalOpx => "K100 RGB (OPX optical)",
            Self::Mechanical => "K100 RGB (Cherry MX Speed)",
            Self::OpticalVariant => "K100 RGB (optical, later variant)",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Command {
    Set = 1,
    Get = 2,
    UnbindBuffers = 5,
    WriteBufferBegin = 6,
    WriteBuffer = 7,
    ReadBuffer = 8,
    DescribeBuffer = 9,
    OpenFile = 13,
    ResetToDefaults = 15,
    ApplyFirmware = 16,
    ApplyFirmwareExtended = 22,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Property {
    Brightness = 2,
    OperatingMode = 3,
    UsbVid = 17,
    UsbPid = 18,
    FirmwareVersion = 19,
    BootloaderVersion = 20,
    /// Physical layout. Returns ckb-next's `LAYOUT_*` enum directly, not zero-based:
    /// an ISO board reports 2. Verified on hardware.
    HwLayout = 0x41,
}

/// Physical layouts, matching ckb-next's `structures.h`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Layout {
    Ansi = 1,
    Iso = 2,
    Abnt = 3,
    Jis = 4,
    Dubeolsik = 5,
}

impl Layout {
    pub fn from_raw(value: u8) -> Option<Self> {
        Some(match value {
            1 => Self::Ansi,
            2 => Self::Iso,
            3 => Self::Abnt,
            4 => Self::Jis,
            5 => Self::Dubeolsik,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum OperatingMode {
    Reserved = 0,
    SelfOperated = 1,
    HostControlled = 2,
    Bootloader = 3,
    Sleep = 4,
}

impl OperatingMode {
    pub fn from_raw(value: u8) -> Option<Self> {
        Some(match value {
            0 => Self::Reserved,
            1 => Self::SelfOperated,
            2 => Self::HostControlled,
            3 => Self::Bootloader,
            4 => Self::Sleep,
            _ => return None,
        })
    }
}

/// Firmware image kinds the protocol can write.
///
/// Only [`ImageType::Firmware`] is ever safe for this project: the bootloader is the
/// recovery path, so replacing it removes the only thing that makes a bad write
/// survivable. Corsair's own updater refuses bootloader updates too, logging
/// "Chip needs isp update which is not supported for now". See `docs/recovery.md`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ImageType {
    Firmware = 1,
    Bootloader = 2,
    FirmwareWithBootloader = 3,
    SecondaryChipFirmware = 4,
}

impl ImageType {
    /// Rule 1 from `docs/recovery.md`, enforced in the type system rather than by comment.
    pub fn ensure_recoverable(self) -> Result<Self> {
        match self {
            Self::Firmware | Self::SecondaryChipFirmware => Ok(self),
            Self::Bootloader | Self::FirmwareWithBootloader => bail!(
                "refusing to write image type {self:?}: this replaces the bootloader, \
                 which is the recovery path (see docs/recovery.md)"
            ),
        }
    }
}

/// Device status codes returned in responses.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Success,
    InvalidArgumentValue,
    InsufficientBufferSize,
    InvalidState,
    CommandNotSupported,
    PropertyNotSupported,
    InvalidDeviceAddress,
    HardwareError,
    InvalidOperation,
    InvalidFirmwareFile,
    MalformedReport,
    Unknown(u8),
}

impl Status {
    pub fn from_raw(value: u8) -> Self {
        match value {
            0 => Self::Success,
            1 => Self::InvalidArgumentValue,
            2 => Self::InsufficientBufferSize,
            3 => Self::InvalidState,
            4 => Self::CommandNotSupported,
            5 => Self::PropertyNotSupported,
            6 => Self::InvalidDeviceAddress,
            7 => Self::HardwareError,
            9 => Self::InvalidOperation,
            10 => Self::InvalidFirmwareFile,
            15 => Self::MalformedReport,
            other => Self::Unknown(other),
        }
    }

    pub fn is_success(self) -> bool {
        self == Self::Success
    }
}

/// How many bytes sit between the property byte and the payload.
///
/// The updater picks this per device: `getHeaderLayout()` returns `FOUR_BYTE` when the
/// device reports Bragi protocol version 1.5, and `TWO_BYTE` otherwise. The version comes
/// from the device at runtime, so this cannot be known statically — [`crate::device`]
/// determines it empirically.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HeaderLayout {
    TwoByte,
    FourByte,
}

impl HeaderLayout {
    /// Index of the first payload byte in a response, counting the report ID at index 0.
    pub const fn payload_offset(self) -> usize {
        match self {
            Self::TwoByte => 3,
            Self::FourByte => 5,
        }
    }
}

/// A request, before any report-ID byte is prepended by the transport.
///
/// A command's arguments begin at **body byte 2**, immediately after the magic and the
/// command. There is no separate "property" slot: a property ID is simply the first argument,
/// as a 16-bit little-endian value. Modelling it that way is not cosmetic — treating the
/// property as one byte and the payload as separate put a SET's value one byte too early,
/// which the keyboard rejects with `INVALID_STATE`. Verified on hardware; see the request
/// layouts in `docs/bragi-firmware-update.md`.
#[derive(Debug, Clone)]
pub struct Request {
    pub command: Command,
    pub args: Vec<u8>,
}

impl Request {
    pub fn new(command: Command, args: impl Into<Vec<u8>>) -> Self {
        Self { command, args: args.into() }
    }

    /// `08 02 <prop16>`
    pub fn get(property: Property) -> Self {
        Self::new(Command::Get, (property as u16).to_le_bytes().to_vec())
    }

    /// `08 01 <prop16> <value…>`
    pub fn set(property: Property, value: impl AsRef<[u8]>) -> Self {
        let mut args = (property as u16).to_le_bytes().to_vec();
        args.extend_from_slice(value.as_ref());
        Self::new(Command::Set, args)
    }

    pub fn set_operating_mode(mode: OperatingMode) -> Self {
        Self::set(Property::OperatingMode, [mode as u8])
    }

    /// `08 0D <handle> <fileId>`
    pub fn open_file(handle: u8, file_id: u16) -> Self {
        let [lo, hi] = file_id.to_le_bytes();
        Self::new(Command::OpenFile, vec![handle, lo, hi])
    }

    /// `08 09 <handle>`
    pub fn describe_buffer(handle: u8) -> Self {
        Self::new(Command::DescribeBuffer, vec![handle])
    }

    /// `08 05 <count> <handle>`
    pub fn unbind_buffers(handle: u8) -> Self {
        Self::new(Command::UnbindBuffers, vec![1, handle])
    }

    /// Serialises the request body, optionally behind a leading report-ID byte.
    ///
    /// usbdevfs writes the body directly; a HID API consumes a leading report id, which is
    /// `0x00` here because this device's reports are unnumbered.
    pub fn encode(&self, report_length: usize, report_id: bool) -> Result<Vec<u8>> {
        let used = 2 + self.args.len();
        if used > report_length {
            bail!("{} argument bytes overflow a {report_length}-byte report", self.args.len());
        }

        let offset = usize::from(report_id);
        let mut report = vec![0u8; report_length + offset];
        report[offset] = BRAGI_MAGIC;
        report[offset + 1] = self.command as u8;
        report[offset + 2..offset + used].copy_from_slice(&self.args);
        Ok(report)
    }
}

/// A decoded response.
#[derive(Debug, Clone)]
pub struct Response {
    pub raw: Vec<u8>,
    layout: HeaderLayout,
}

impl Response {
    pub fn new(raw: Vec<u8>, layout: HeaderLayout) -> Self {
        Self { raw, layout }
    }

    /// The status byte sits immediately after the command echo.
    pub fn status(&self) -> Status {
        self.raw.get(2).copied().map_or(Status::Unknown(0xff), Status::from_raw)
    }

    pub fn payload(&self) -> &[u8] {
        self.raw.get(self.layout.payload_offset()..).unwrap_or(&[])
    }

    /// Reads a little-endian u16 from the start of the payload. Firmware versions are
    /// encoded this way.
    pub fn u16_payload(&self) -> Option<u16> {
        let p = self.payload();
        Some(u16::from_le_bytes([*p.first()?, *p.get(1)?]))
    }

    /// Plausibility check used to pick the header layout: a Get of a known property should
    /// answer with the command echoed and a success status.
    ///
    /// A reply does **not** begin with the Bragi magic the way a request does. Observed on
    /// hardware, byte 0 is `0x00`, byte 1 echoes the command and byte 2 carries the status;
    /// see the response layout in `docs/bragi-firmware-update.md`. Byte 0 is deliberately
    /// not checked, since nothing documents what it means.
    pub fn looks_valid(&self, expected: Command) -> bool {
        self.raw.get(1) == Some(&(expected as u8)) && self.status().is_success()
    }
}

/// CRC-32/MPEG-2: poly 0x04C11DB7, init all ones, no reflection, no final XOR.
///
/// The only checksum algorithm Bragi defines (`CRC32_MPEG2 = 1`), and the one the K100
/// manifest names. Verified against both official images in `docs/firmware-format.md`.
pub fn crc32_mpeg2(data: &[u8]) -> u32 {
    data.iter().fold(u32::MAX, |crc, &byte| {
        (0..8).fold(crc ^ ((byte as u32) << 24), |acc, _| {
            if acc & 0x8000_0000 != 0 {
                (acc << 1) ^ 0x04C1_1DB7
            } else {
                acc << 1
            }
        })
    })
}

/// The 512-byte-aligned container Corsair ships firmware in.
pub struct Image<'a> {
    pub bytes: &'a [u8],
}

impl<'a> Image<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self> {
        if bytes.len() < 0x2c {
            bail!("firmware image is too short to contain a header");
        }
        let image = Self { bytes };

        if image.declared_length() as usize != bytes.len() {
            bail!(
                "header declares {} bytes but the file is {}",
                image.declared_length(),
                bytes.len()
            );
        }
        let actual = crc32_mpeg2(&bytes[4..]);
        if actual != image.declared_crc() {
            bail!("CRC mismatch: header says {:#010x}, computed {actual:#010x}", image.declared_crc());
        }
        Ok(image)
    }

    pub fn declared_crc(&self) -> u32 {
        u32::from_le_bytes(self.bytes[0..4].try_into().expect("checked length"))
    }

    pub fn declared_length(&self) -> u32 {
        u32::from_le_bytes(self.bytes[4..8].try_into().expect("checked length"))
    }

    /// ASCII build date and time the vendor stamps into the header.
    pub fn build_stamp(&self) -> String {
        String::from_utf8_lossy(&self.bytes[0x0e..0x23]).replace('\0', " ").trim().to_string()
    }
}
