//! Bragi device logic, on top of the usbdevfs transport.
//!
//! This replaced a HID-based implementation that could not reach the keyboard at all. The
//! header-layout probing it carried is also gone: the framing is settled for this device.

use anyhow::{anyhow, bail, Context, Result};

use crate::protocol::{
    HeaderLayout, Layout, Model, OperatingMode, Property, Request, Response, CORSAIR_VID,
};
use crate::transport::{self, UsbDevice};

/// Bragi command endpoints, from ckb-next's `bragi_fill_input_eps()` for this product ID,
/// corroborated by the device's own descriptors.
const BRAGI_EP_OUT: u8 = 0x01;
const BRAGI_EP_IN: u8 = 0x82;

/// "Jumbo" report size, which every K100 uses.
const REPORT_SIZE: usize = 1024;

/// Interfaces to claim when the device presents none that sysfs can see, which should not
/// happen. ckb-next claims all of them rather than only the vendor one, and anything less was
/// observed not to work; claiming any at all stops the keyboard typing until the handle is
/// dropped.
const FALLBACK_INTERFACES: [u8; 3] = [0, 1, 2];

/// ckb-next's transfer timeout.
const TIMEOUT_MS: u32 = 5000;

pub struct Device {
    usb: UsbDevice,
    /// `None` when the product id is not one this tool knows, which is the expected state in
    /// bootloader mode -- the bootloader is a different program and need not keep the
    /// application's identity.
    pub model: Option<Model>,
    /// The product id as reported, known even when the model is not.
    pub product: u16,
    pub layout: HeaderLayout,
}

impl Device {
    /// Finds a K100 and takes its interfaces from the kernel.
    ///
    /// A known product id is preferred, but any Corsair device will do if none is present.
    /// That fallback is the point: a keyboard in bootloader mode may report a product id this
    /// tool has never seen, and refusing to open it then would strand it -- recovery is
    /// exactly when being reachable matters. Which interfaces to claim comes from the device
    /// too, for the same reason.
    pub fn open() -> Result<Self> {
        let pids: Vec<u16> = Model::ALL.iter().map(|m| m.pid()).collect();

        let known = transport::find(CORSAIR_VID, &pids)?;
        let found = match known.into_iter().next() {
            Some(found) => found,
            None => transport::find_any(CORSAIR_VID)?.into_iter().next().ok_or_else(|| {
                anyhow!(
                    "no Corsair device found. Expected VID {CORSAIR_VID:#06x}, ideally with one \
                     of {}; is the keyboard connected, and does this user have access to its \
                     /dev/bus/usb node? See udev/README.md",
                    pids.iter().map(|p| format!("{p:#06x}")).collect::<Vec<_>>().join(", ")
                )
            })?,
        };

        Self::claim(found)
    }

    /// Opens exactly one device of exactly this product id, or nothing.
    ///
    /// Everything [`open`](Self::open) does to stay reachable is a hazard once writing is
    /// involved: the vendor-wide fallback would happily open some other Corsair device, and
    /// taking the first of several matches would pick a keyboard by scan order. Writing names
    /// its target instead, and refuses an ambiguous bus rather than guessing on it.
    pub fn open_exact(product: u16) -> Result<Self> {
        let mut found = transport::find(CORSAIR_VID, &[product])?;

        if found.len() > 1 {
            bail!(
                "{} devices report {CORSAIR_VID:#06x}:{product:#06x} ({}). Refusing to pick \
                 one; disconnect the others.",
                found.len(),
                found
                    .iter()
                    .map(|f| f.node.display().to_string())
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }

        let found = found.pop().ok_or_else(|| {
            anyhow!(
                "no {CORSAIR_VID:#06x}:{product:#06x} on this bus. Nothing else will be \
                 written to. If the keyboard is in bootloader mode it may report a different \
                 product id: run `info` to see what it reports, then pass that id."
            )
        })?;

        Self::claim(found)
    }

    /// Takes a found device's interfaces from the kernel.
    fn claim(found: transport::Found) -> Result<Self> {
        let model = Model::from_pid(found.product);
        let interfaces = if found.interfaces.is_empty() {
            FALLBACK_INTERFACES.to_vec()
        } else {
            found.interfaces.clone()
        };

        let usb = UsbDevice::open(&found.node, &interfaces)
            .with_context(|| format!("taking interfaces of {}", found.node.display()))?;

        // Two-byte framing is settled for the application firmware rather than probed: USB_VID
        // and USB_PID read back correctly at that offset. See docs/bragi-firmware-update.md.
        // Whether the bootloader frames the same way is unverified, which is why `info`
        // exists.
        Ok(Self { usb, model, product: found.product, layout: HeaderLayout::TwoByte })
    }

    /// A human-readable name, which is not always a known model.
    pub fn description(&self) -> String {
        match self.model {
            Some(model) => model.description().to_string(),
            None => format!(
                "unrecognised Corsair device {:#06x} -- possibly a K100 in bootloader mode",
                self.product
            ),
        }
    }

    /// Sends a request and reads the reply.
    pub fn exchange(&self, request: &Request) -> Result<Response> {
        let body = request.encode(REPORT_SIZE, false)?;
        self.usb
            .transfer(BRAGI_EP_OUT, Some(&body), REPORT_SIZE, TIMEOUT_MS)
            .with_context(|| format!("sending {:?}", request.command))?;

        let reply = self
            .usb
            .transfer(BRAGI_EP_IN, None, REPORT_SIZE, TIMEOUT_MS)
            .with_context(|| format!("reading the reply to {:?}", request.command))?;

        Ok(Response::new(reply, self.layout))
    }

    /// Issues a request and fails unless the device reported success.
    fn expect_ok(&self, request: &Request) -> Result<Response> {
        let response = self.exchange(request)?;
        if !response.status().is_success() {
            bail!("{:?} returned {:?}", request.command, response.status());
        }
        Ok(response)
    }

    pub fn firmware_version(&self) -> Result<String> {
        self.read_version(Property::FirmwareVersion)
    }

    pub fn bootloader_version(&self) -> Result<String> {
        self.read_version(Property::BootloaderVersion)
    }

    /// Versions are three payload bytes: major, minor, patch. Corroborated by ckb-next
    /// recording a K100 at "fw 0.32.6, bld 0.10.45", which matches a real unit exactly.
    fn read_version(&self, property: Property) -> Result<String> {
        let response = self.expect_ok(&Request::get(property))?;
        let payload = response.payload();
        if payload.len() < 3 {
            bail!("{property:?} payload was only {} bytes", payload.len());
        }
        Ok(format!("{}.{}.{}", payload[0], payload[1], payload[2]))
    }

    pub fn usb_pid(&self) -> Result<u16> {
        let response = self.expect_ok(&Request::get(Property::UsbPid))?;
        response.u16_payload().ok_or_else(|| anyhow!("USB_PID payload was too short"))
    }

    pub fn operating_mode(&self) -> Result<OperatingMode> {
        let response = self.expect_ok(&Request::get(Property::OperatingMode))?;
        let raw = *response
            .payload()
            .first()
            .ok_or_else(|| anyhow!("operating mode payload was empty"))?;

        OperatingMode::from_raw(raw).ok_or_else(|| anyhow!("unknown operating mode {raw:#04x}"))
    }

    /// Physical layout. The device reports ckb-next's enum directly, not zero-based.
    pub fn hw_layout(&self) -> Result<Layout> {
        let response = self.expect_ok(&Request::get(Property::HwLayout))?;
        let raw = *response
            .payload()
            .first()
            .ok_or_else(|| anyhow!("layout payload was empty"))?;

        Layout::from_raw(raw).ok_or_else(|| anyhow!("unknown physical layout {raw}"))
    }

    /// Changes operating mode and confirms it took effect.
    ///
    /// File and buffer commands are refused with `InvalidState` while the keyboard is
    /// `SelfOperated`, its power-on state, so host control is a prerequisite for most work.
    /// `Bootloader` is fire-and-forget because the device re-enumerates instead of replying.
    pub fn set_operating_mode(&self, mode: OperatingMode) -> Result<()> {
        let request = Request::set_operating_mode(mode);

        if mode == OperatingMode::Bootloader {
            let body = request.encode(REPORT_SIZE, false)?;
            self.usb
                .transfer(BRAGI_EP_OUT, Some(&body), REPORT_SIZE, TIMEOUT_MS)
                .context("requesting bootloader mode")?;
            return Ok(());
        }

        self.expect_ok(&request)?;
        let landed = self.operating_mode()?;
        if landed != mode {
            bail!("asked for {mode:?} but the keyboard reports {landed:?}");
        }
        Ok(())
    }

    /// Opens a file into a buffer handle, returning the device's status unjudged: callers
    /// legitimately probe for files that may not exist on a given model.
    pub fn open_file(&self, handle: u8, file_id: u16) -> Result<Response> {
        self.exchange(&Request::open_file(handle, file_id))
    }

    pub fn unbind_buffers(&self, handle: u8) -> Result<Response> {
        self.exchange(&Request::unbind_buffers(handle))
    }

    /// Runs `body` with the keyboard in host-controlled mode, restoring the previous mode
    /// afterwards whether or not it succeeded.
    pub fn with_host_control<T>(&self, body: impl FnOnce() -> Result<T>) -> Result<T> {
        let entry = self.operating_mode()?;
        if entry == OperatingMode::HostControlled {
            return body();
        }

        self.set_operating_mode(OperatingMode::HostControlled)
            .context("taking host control")?;

        let outcome = body();

        if let Err(error) = self.set_operating_mode(entry) {
            // Reported, but it must not mask the original failure.
            eprintln!("warning: could not restore {entry:?}: {error:#}");
        }
        outcome
    }
}
