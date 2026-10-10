//! HID transport for Bragi, plus the runtime probing the protocol requires.
//!
//! Two things genuinely cannot be known without talking to the device: which of its HID
//! interfaces carries Bragi, and which header layout it uses. Both are discovered here by
//! trying and checking rather than assumed, because a wrong frame on a firmware command is
//! exactly the mistake worth engineering out.

use std::time::Duration;

use anyhow::{anyhow, bail, Context, Result};
use hidapi::{HidApi, HidDevice};

use crate::protocol::{
    Command, HeaderLayout, Model, OperatingMode, Property, Request, Response, CORSAIR_VID,
};

/// Bragi "jumbo" report size, which every K100 SKU uses (`USES_BRAGI_JUMBO` in ckb-next).
pub const JUMBO_REPORT_LENGTH: usize = 1024;

/// Standard Bragi report size, for devices that are not jumbo.
pub const STANDARD_REPORT_LENGTH: usize = 64;

const READ_TIMEOUT: Duration = Duration::from_millis(1000);

fn hex(data: &[u8], count: usize) -> String {
    data.iter().take(count).map(|b| format!("{b:02x}")).collect::<Vec<_>>().join(" ")
}

pub struct Device {
    hid: HidDevice,
    pub model: Model,
    pub report_length: usize,
    pub layout: HeaderLayout,
}

impl Device {
    /// Finds a K100, then establishes which interface and header layout actually work.
    pub fn open(api: &HidApi) -> Result<Self> {
        let candidates: Vec<_> = api
            .device_list()
            .filter(|info| info.vendor_id() == CORSAIR_VID)
            .filter_map(|info| Model::from_pid(info.product_id()).map(|model| (info, model)))
            .collect();

        if candidates.is_empty() {
            bail!(
                "no K100 found. Expected Corsair VID {CORSAIR_VID:#06x} with one of the \
                 K100 product IDs; is the keyboard connected and does this user have \
                 hidraw access?"
            );
        }

        let mut failures = Vec::new();
        for (info, model) in candidates {
            let path = info.path().to_string_lossy().into_owned();
            match info.open_device(api) {
                Ok(hid) => match Self::establish(hid, model) {
                    Ok(device) => return Ok(device),
                    Err(error) => failures.push(format!("{path}: {error:#}")),
                },
                Err(error) => failures.push(format!("{path}: cannot open ({error})")),
            }
        }

        Err(anyhow!(
            "found a K100 but no interface answered Bragi:\n  {}",
            failures.join("\n  ")
        ))
    }

    /// Probes one interface: ask for the firmware version under each header layout and keep
    /// whichever answers coherently.
    fn establish(mut hid: HidDevice, model: Model) -> Result<Self> {
        // A rejected write and a silent device are different faults and must not look alike,
        // so every attempt's outcome is kept and reported if none succeeds.
        let mut attempts = Vec::new();

        for report_length in [JUMBO_REPORT_LENGTH, STANDARD_REPORT_LENGTH] {
            for layout in [HeaderLayout::TwoByte, HeaderLayout::FourByte] {
                let device = Self { hid, model, report_length, layout };

                match device.exchange(&Request::get(Property::FirmwareVersion)) {
                    Ok(response) if response.looks_valid(Command::Get) => return Ok(device),
                    Ok(response) => {
                        attempts.push(format!(
                            "{report_length}B/{layout:?}: replied but incoherent: {}",
                            hex(&response.raw, 12)
                        ));
                        hid = device.hid;
                    }
                    Err(error) => {
                        attempts.push(format!("{report_length}B/{layout:?}: {error:#}"));
                        hid = device.hid;
                    }
                }
            }
        }
        bail!("no framing worked:\n      {}", attempts.join("\n      "))
    }

    /// Sends a request and reads the reply.
    pub fn exchange(&self, request: &Request) -> Result<Response> {
        self.write(request)?;

        let mut buffer = vec![0u8; self.report_length];
        let read = self
            .hid
            .read_timeout(&mut buffer, READ_TIMEOUT.as_millis() as i32)
            .context("reading Bragi response")?;

        if read == 0 {
            bail!("device did not answer within {READ_TIMEOUT:?}");
        }
        buffer.truncate(read);
        Ok(Response::new(buffer, self.layout))
    }

    /// Sends a request without waiting for a reply. Used for the bootloader transition,
    /// where the device drops off the bus instead of answering — the updater passes
    /// `expectResponse: false` for exactly this.
    pub fn write(&self, request: &Request) -> Result<()> {
        let report = request.encode(self.report_length)?;
        self.hid.write(&report).with_context(|| format!("sending {:?}", request.command))?;
        Ok(())
    }

    pub fn firmware_version(&self) -> Result<u16> {
        self.read_u16(Property::FirmwareVersion)
    }

    pub fn bootloader_version(&self) -> Result<u16> {
        self.read_u16(Property::BootloaderVersion)
    }

    pub fn usb_pid(&self) -> Result<u16> {
        self.read_u16(Property::UsbPid)
    }

    pub fn operating_mode(&self) -> Result<OperatingMode> {
        let response = self.exchange(&Request::get(Property::OperatingMode))?;
        let raw = *response
            .payload()
            .first()
            .ok_or_else(|| anyhow!("operating mode response carried no payload"))?;

        OperatingMode::from_raw(raw).ok_or_else(|| anyhow!("unknown operating mode {raw:#04x}"))
    }

    fn read_u16(&self, property: Property) -> Result<u16> {
        let response = self.exchange(&Request::get(property))?;
        if !response.status().is_success() {
            bail!("{property:?} returned {:?}", response.status());
        }
        response
            .u16_payload()
            .ok_or_else(|| anyhow!("{property:?} response was too short"))
    }

    /// Changes operating mode. `Bootloader` is sent fire-and-forget because the device
    /// re-enumerates rather than replying; see `docs/bragi-firmware-update.md`.
    pub fn set_operating_mode(&self, mode: OperatingMode) -> Result<()> {
        let request = Request::set_operating_mode(mode);

        if mode == OperatingMode::Bootloader {
            return self.write(&request);
        }

        let response = self.exchange(&request)?;
        if !response.status().is_success() {
            bail!("setting mode to {mode:?} returned {:?}", response.status());
        }
        Ok(())
    }
}
