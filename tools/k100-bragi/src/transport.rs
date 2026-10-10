//! usbdevfs transport: the only access path that works for this keyboard.
//!
//! Neither hidraw nor hidapi's libusb backend can drive the K100's Bragi interface; writes
//! are rejected. ckb-next reaches it through usbdevfs ioctls on `/dev/bus/usb/...`, detaching
//! the kernel driver and claiming the interfaces first, and that is what this does. See the
//! Linux transport section of `docs/bragi-firmware-update.md`.
//!
//! No elevated privileges are required: the usbfs node carries an ACL granting the
//! logged-in user access, as OpenRGB's and ckb-next's udev rules arrange.

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

/// Passed as the `code` inside a [`DriverIoctl`] rather than issued directly.
const USBDEVFS_DISCONNECT: c_int = ioc::encode(ioc::NONE, b'U', 22, 0) as c_int;
const USBDEVFS_CONNECT: c_int = ioc::encode(ioc::NONE, b'U', 23, 0) as c_int;

/// A USB device found in sysfs.
#[derive(Debug, Clone)]
pub struct Found {
    pub node: PathBuf,
    pub product: u16,
}

/// Scans sysfs for devices matching a vendor and any of several products.
pub fn find(vendor: u16, products: &[u16]) -> Result<Vec<Found>> {
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
        if vid != vendor || !products.contains(&pid) {
            continue;
        }
        let (Some(bus), Some(device)) = (decimal("busnum"), decimal("devnum")) else { continue };

        found.push(Found {
            node: PathBuf::from(format!("/dev/bus/usb/{bus:03}/{device:03}")),
            product: pid,
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
        for &interface in self.claimed.iter().rev() {
            let number = c_uint::from(interface);
            unsafe {
                libc::ioctl(self.file.as_raw_fd(), USBDEVFS_RELEASEINTERFACE as _, &number);
            }
        }
        for &interface in self.detached.clone().iter().rev() {
            let _ = self.driver_ioctl(interface, USBDEVFS_CONNECT);
        }
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
