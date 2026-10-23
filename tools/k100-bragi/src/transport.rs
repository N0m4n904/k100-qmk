//! usbdevfs transport: the only access path that works for this keyboard.
//!
//! Neither hidraw nor hidapi's libusb backend can drive the K100's Bragi interface; writes
//! are rejected. ckb-next reaches it through usbdevfs ioctls on `/dev/bus/usb/...`, detaching
//! the kernel driver and claiming the interfaces first, and that is what this does. See the
//! Linux transport section of `docs/bragi-firmware-update.md`.
//!
//! No elevated privileges are required: the usbfs node carries an ACL granting the
//! logged-in user access, as OpenRGB's and ckb-next's udev rules arrange.

use std::collections::BTreeMap;
use std::ffi::{c_int, c_uint, c_void};
use std::fs::{File, OpenOptions};
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};

use anyhow::{anyhow, bail, Context, Result};

/// ioctl request encoding, from `asm-generic/ioctl.h`.
mod ioc {
    const NRBITS: u32 = 8;
    const TYPEBITS: u32 = 8;
    const SIZEBITS: u32 = 14;

    pub const NONE: u32 = 0;
    pub const WRITE: u32 = 1;
    pub const READ: u32 = 2;

    pub const fn encode(direction: u32, kind: u8, number: u32, size: usize) -> u32 {
        (direction << (NRBITS + TYPEBITS + SIZEBITS))
            | ((size as u32) << (NRBITS + TYPEBITS))
            | ((kind as u32) << NRBITS)
            | number
    }
}

/// `struct usbdevfs_bulktransfer`. Serves interrupt endpoints too; the kernel routes by
/// endpoint type, and ckb-next drives this keyboard's interrupt endpoints the same way.
#[repr(C)]
struct BulkTransfer {
    endpoint: c_uint,
    length: c_uint,
    timeout: c_uint,
    data: *mut c_void,
}

/// `struct usbdevfs_ioctl`, the envelope that carries DISCONNECT and CONNECT per interface.
#[repr(C)]
struct DriverIoctl {
    interface: c_int,
    code: c_int,
    data: *mut c_void,
}

/// `struct usbdevfs_getdriver`.
#[repr(C)]
struct GetDriver {
    interface: c_uint,
    name: [u8; 256],
}

const USBDEVFS_BULK: u32 =
    ioc::encode(ioc::READ | ioc::WRITE, b'U', 2, size_of::<BulkTransfer>());
const USBDEVFS_GETDRIVER: u32 = ioc::encode(ioc::WRITE, b'U', 8, size_of::<GetDriver>());
const USBDEVFS_CLAIMINTERFACE: u32 = ioc::encode(ioc::READ, b'U', 15, size_of::<c_uint>());
const USBDEVFS_RELEASEINTERFACE: u32 = ioc::encode(ioc::READ, b'U', 16, size_of::<c_uint>());
const USBDEVFS_IOCTL: u32 = ioc::encode(ioc::READ | ioc::WRITE, b'U', 18, size_of::<DriverIoctl>());
const USBDEVFS_RESET: u32 = ioc::encode(ioc::NONE, b'U', 20, 0);

/// Passed as the `code` inside a [`DriverIoctl`] rather than issued directly.
const USBDEVFS_DISCONNECT: c_int = ioc::encode(ioc::NONE, b'U', 22, 0) as c_int;
const USBDEVFS_CONNECT: c_int = ioc::encode(ioc::NONE, b'U', 23, 0) as c_int;

/// A USB device found in sysfs.
#[derive(Debug, Clone)]
pub struct Found {
    pub node: PathBuf,
    pub product: u16,
    /// `bcdDevice`, the version the device declares in its own descriptor.
    ///
    /// The one field that says which program is answering, because all three --
    /// Corsair's application, Corsair's bootloader and this project's firmware --
    /// share a vendor and product id. It is also the only thing left to read when
    /// what came back does not speak Corsair's protocol at all.
    pub version: Option<u16>,
    /// Interface numbers the device actually presents, read from sysfs rather than assumed.
    ///
    /// The application firmware shows three, but the bootloader is a different program and
    /// need not show the same ones. Claiming an interface that does not exist fails, and
    /// leaving one unclaimed was observed to stop the device responding, so the set has to
    /// come from the device.
    pub interfaces: Vec<u8>,
    /// Maximum packet size of each endpoint, by address, read from the descriptors.
    ///
    /// This is not a detail. The application's vendor endpoints are 1024 bytes and the
    /// **bootloader's are 64**, so a report size assumed from one is silently wrong in the
    /// other: every request goes out as sixteen packets, the device never sees a whole
    /// report, and it answers nothing at all. Observed on hardware, and it is why this comes
    /// from the device rather than from a constant.
    pub endpoints: BTreeMap<u8, usize>,
}

impl Found {
    /// The report size to use on an endpoint: one packet, as the device declares it.
    pub fn report_size(&self, endpoint: u8) -> Result<usize> {
        self.endpoints.get(&endpoint).copied().ok_or_else(|| {
            anyhow!(
                "{} presents no endpoint {endpoint:#04x}; it has {}",
                self.node.display(),
                self.endpoints
                    .keys()
                    .map(|e| format!("{e:#04x}"))
                    .collect::<Vec<_>>()
                    .join(", ")
            )
        })
    }
}

/// Interface numbers of a sysfs USB device.
///
/// Its interfaces appear as child directories named `<device>:<config>.<interface>`, each
/// carrying a `bInterfaceNumber`. That number is authoritative; the directory suffix is not,
/// since it is the descriptor index.
fn interfaces_of(path: &Path) -> Vec<u8> {
    let Some(prefix) = path.file_name().and_then(|n| n.to_str()) else {
        return Vec::new();
    };

    let mut interfaces: Vec<u8> = std::fs::read_dir(path)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|child| {
            child.file_name().to_str().is_some_and(|n| n.starts_with(&format!("{prefix}:")))
        })
        .filter_map(|child| {
            let text = std::fs::read_to_string(child.path().join("bInterfaceNumber")).ok()?;
            u8::from_str_radix(text.trim(), 16).ok()
        })
        .collect();

    interfaces.sort_unstable();
    interfaces.dedup();
    interfaces
}

/// Maximum packet size of every endpoint the device presents, by address.
///
/// Each interface directory holds one `ep_<hex address>` child per endpoint, whose
/// `wMaxPacketSize` is four hex digits with no prefix.
fn endpoints_of(path: &Path) -> BTreeMap<u8, usize> {
    let Some(prefix) = path.file_name().and_then(|n| n.to_str()) else {
        return BTreeMap::new();
    };

    std::fs::read_dir(path)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|child| {
            child.file_name().to_str().is_some_and(|n| n.starts_with(&format!("{prefix}:")))
        })
        .flat_map(|interface| std::fs::read_dir(interface.path()).into_iter().flatten().flatten())
        .filter_map(|child| {
            let name = child.file_name().to_str()?.strip_prefix("ep_")?.to_owned();
            let address = u8::from_str_radix(&name, 16).ok()?;
            let text = std::fs::read_to_string(child.path().join("wMaxPacketSize")).ok()?;
            Some((address, usize::from_str_radix(text.trim(), 16).ok()?))
        })
        .collect()
}

/// Scans sysfs for devices matching a vendor and any of several products.
pub fn find(vendor: u16, products: &[u16]) -> Result<Vec<Found>> {
    scan(vendor, |pid| products.contains(&pid))
}

/// Scans sysfs for every device of a vendor, whatever product it reports.
///
/// Needed because a device in bootloader mode may not report a product id this tool knows,
/// and that is exactly when reaching it matters most.
pub fn find_any(vendor: u16) -> Result<Vec<Found>> {
    scan(vendor, |_| true)
}

fn scan(vendor: u16, accept: impl Fn(u16) -> bool) -> Result<Vec<Found>> {
    let mut found = Vec::new();

    for entry in std::fs::read_dir("/sys/bus/usb/devices")
        .context("listing /sys/bus/usb/devices")?
        .flatten()
    {
        let path = entry.path();
        let hex = |name: &str| -> Option<u16> {
            let text = std::fs::read_to_string(path.join(name)).ok()?;
            u16::from_str_radix(text.trim(), 16).ok()
        };
        let decimal = |name: &str| -> Option<u32> {
            std::fs::read_to_string(path.join(name)).ok()?.trim().parse().ok()
        };

        let (Some(vid), Some(pid)) = (hex("idVendor"), hex("idProduct")) else { continue };
        if vid != vendor || !accept(pid) {
            continue;
        }
        let (Some(bus), Some(device)) = (decimal("busnum"), decimal("devnum")) else { continue };

        found.push(Found {
            node: PathBuf::from(format!("/dev/bus/usb/{bus:03}/{device:03}")),
            product: pid,
            version: hex("bcdDevice"),
            interfaces: interfaces_of(&path),
            endpoints: endpoints_of(&path),
        });
    }
    Ok(found)
}

/// An open usbfs handle owning a set of interfaces.
///
/// ckb-next claims *every* interface rather than only the vendor one, and this matches that.
/// Interfaces are released and the kernel driver reattached on drop, so a dropped handle
/// always leaves the keyboard usable.
pub struct UsbDevice {
    file: File,
    detached: Vec<u8>,
    claimed: Vec<u8>,
}

impl UsbDevice {
    /// Opens the node and takes the given interfaces from the kernel.
    pub fn open(node: &Path, interfaces: &[u8]) -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(node)
            .with_context(|| format!("opening {}", node.display()))?;

        let mut device = Self { file, detached: Vec::new(), claimed: Vec::new() };

        for &interface in interfaces {
            if device.driver(interface)?.is_some() {
                device
                    .driver_ioctl(interface, USBDEVFS_DISCONNECT)
                    .with_context(|| format!("detaching kernel driver from interface {interface}"))?;
                device.detached.push(interface);
            }
            device
                .claim(interface)
                .with_context(|| format!("claiming interface {interface}"))?;
            device.claimed.push(interface);
        }
        Ok(device)
    }

    /// The kernel driver currently bound to an interface, if any.
    pub fn driver(&self, interface: u8) -> Result<Option<String>> {
        let mut query = GetDriver { interface: interface.into(), name: [0; 256] };
        if unsafe { libc::ioctl(self.file.as_raw_fd(), USBDEVFS_GETDRIVER as _, &mut query) } < 0 {
            // ENODATA simply means nothing is bound.
            return Ok(None);
        }
        let end = query.name.iter().position(|&b| b == 0).unwrap_or(query.name.len());
        Ok(Some(String::from_utf8_lossy(&query.name[..end]).into_owned()))
    }

    fn driver_ioctl(&self, interface: u8, code: c_int) -> Result<()> {
        let mut envelope = DriverIoctl {
            interface: interface.into(),
            code,
            data: std::ptr::null_mut(),
        };
        self.check(unsafe {
            libc::ioctl(self.file.as_raw_fd(), USBDEVFS_IOCTL as _, &mut envelope)
        })
        .map(|_| ())
    }

    fn claim(&self, interface: u8) -> Result<()> {
        let number = c_uint::from(interface);
        self.check(unsafe {
            libc::ioctl(self.file.as_raw_fd(), USBDEVFS_CLAIMINTERFACE as _, &number)
        })
        .map(|_| ())
    }

    /// One bulk or interrupt transfer. `data` is sent for an OUT endpoint; for an IN
    /// endpoint pass `None` and the reply is returned, truncated to what arrived.
    pub fn transfer(
        &self,
        endpoint: u8,
        data: Option<&[u8]>,
        length: usize,
        timeout_ms: u32,
    ) -> Result<Vec<u8>> {
        let mut buffer = vec![0u8; length];
        if let Some(payload) = data {
            if payload.len() > length {
                bail!("payload of {} bytes exceeds the {length}-byte buffer", payload.len());
            }
            buffer[..payload.len()].copy_from_slice(payload);
        }

        let mut request = BulkTransfer {
            endpoint: endpoint.into(),
            length: length as c_uint,
            timeout: timeout_ms,
            data: buffer.as_mut_ptr().cast(),
        };

        let transferred = self
            .check(unsafe { libc::ioctl(self.file.as_raw_fd(), USBDEVFS_BULK as _, &mut request) })
            .with_context(|| format!("transfer on endpoint {endpoint:#04x}"))?;

        buffer.truncate(transferred as usize);
        Ok(buffer)
    }

    /// Resets the port, which is as close to unplugging the device as software gets.
    ///
    /// `docs/recovery.md` opens its procedure with "replug the keyboard": the vendor command
    /// endpoint can be left refusing every transfer with `ETIMEDOUT` by a client that failed
    /// mid-exchange, and only a fresh start clears it. A USB port reset is the kernel's own
    /// version of that -- the device re-enumerates and its endpoints are re-established --
    /// and it costs no physical access, which matters when the keyboard is not on the machine
    /// doing the work.
    ///
    /// It is not identical to a power cycle: the device is not de-powered, so firmware state
    /// that only `VBUS` dropping would clear survives. If a reset does not revive the
    /// endpoint, an actual replug is the next thing to try, not a second reset.
    ///
    /// The device re-enumerates at a new address, so this consumes the handle.
    pub fn reset(mut self) -> Result<()> {
        // The interfaces go back to the kernel first. Resetting while this process still
        // holds them leaves the kernel unable to re-bind its keyboard driver, so the device
        // would come back enumerated but not typing.
        self.release();

        self.check(unsafe { libc::ioctl(self.file.as_raw_fd(), USBDEVFS_RESET as _, 0) })
            .context("resetting the USB port")?;
        Ok(())
    }

    /// Gives every claimed interface back and reattaches every driver that was detached.
    ///
    /// Taken out of [`Drop`] so [`reset`](Self::reset) can do it early and explicitly.
    /// Draining the lists makes it idempotent, which is what lets `Drop` run afterwards
    /// without undoing anything twice.
    fn release(&mut self) {
        for interface in std::mem::take(&mut self.claimed).into_iter().rev() {
            let number = c_uint::from(interface);
            unsafe {
                libc::ioctl(self.file.as_raw_fd(), USBDEVFS_RELEASEINTERFACE as _, &number);
            }
        }
        for interface in std::mem::take(&mut self.detached).into_iter().rev() {
            let _ = self.driver_ioctl(interface, USBDEVFS_CONNECT);
        }
    }

    /// Maps a negative ioctl return into the underlying errno, which is the whole point of
    /// using this transport: a rejected transfer must be distinguishable from a silent one.
    fn check(&self, result: c_int) -> Result<c_int> {
        if result < 0 {
            let error = std::io::Error::last_os_error();
            return Err(anyhow!("{error}"));
        }
        Ok(result)
    }
}

impl Drop for UsbDevice {
    fn drop(&mut self) {
        self.release();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Pins the computed request numbers to the values the kernel actually defines on
    /// x86_64. The arithmetic is easy to get subtly wrong and a wrong number would be
    /// indistinguishable from a device fault at runtime.
    #[test]
    fn ioctl_numbers_match_the_kernel() {
        assert_eq!(USBDEVFS_BULK, 0xC018_5502);
        assert_eq!(USBDEVFS_GETDRIVER, 0x4104_5508);
        assert_eq!(USBDEVFS_CLAIMINTERFACE, 0x8004_550F);
        assert_eq!(USBDEVFS_RELEASEINTERFACE, 0x8004_5510);
        assert_eq!(USBDEVFS_IOCTL, 0xC010_5512);
        assert_eq!(USBDEVFS_RESET, 0x5514);
        assert_eq!(USBDEVFS_DISCONNECT, 0x5516);
        assert_eq!(USBDEVFS_CONNECT, 0x5517);
    }

    #[test]
    fn struct_sizes_match_the_kernel() {
        assert_eq!(size_of::<BulkTransfer>(), 24);
        assert_eq!(size_of::<DriverIoctl>(), 16);
        assert_eq!(size_of::<GetDriver>(), 260);
    }
}
