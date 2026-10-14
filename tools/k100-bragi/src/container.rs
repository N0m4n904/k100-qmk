//! Corsair's firmware container, enough of it to refuse a bad image.
//!
//! `tools/pack-firmware.py` builds these and proves the layout by rebuilding both
//! published images byte for byte. This is the reading side, in the program that
//! does the writing, because a check in the tool that produced a file does not
//! protect a file that reaches the device some other way, and the keyboard is
//! what is at stake.
//!
//! Every field and offset is from `docs/firmware-format.md`, where they were
//! established from those two images rather than guessed.

use anyhow::{bail, Context, Result};

use crate::protocol::{crc32_mpeg2, ImageType};

/// Bytes of header ahead of the payload.
pub const HEADER_SIZE: usize = 0x2C;

/// Where the application lives. The first 64 KB is Corsair's bootloader.
pub const APPLICATION_BASE: u32 = 0x0001_0000;

/// Flash available to the application, past the bootloader.
pub const APPLICATION_SIZE: u32 = 448 * 1024;

/// A parsed container header.
#[derive(Debug, Clone, Copy)]
pub struct Container {
    pub crc: u32,
    pub length: u32,
    pub entry: u32,
    pub format_version: u16,
    pub product_id: u16,
    pub image_type: ImageType,
    pub version_minor: u8,
    pub version_patch: u16,
}

impl Container {
    pub fn parse(image: &[u8]) -> Result<Self> {
        if image.len() < HEADER_SIZE {
            bail!("{} bytes is too short to be a firmware container", image.len());
        }
        let u32_at = |o: usize| u32::from_le_bytes(image[o..o + 4].try_into().unwrap());
        let u16_at = |o: usize| u16::from_le_bytes(image[o..o + 2].try_into().unwrap());

        let raw_type = u16_at(0x27);
        let image_type = ImageType::from_raw(raw_type)
            .with_context(|| format!("image declares unknown type {raw_type}"))?;

        Ok(Self {
            crc: u32_at(0x00),
            length: u32_at(0x04),
            entry: u32_at(0x08),
            format_version: u16_at(0x0C),
            product_id: u16_at(0x25),
            image_type,
            version_minor: image[0x29],
            version_patch: u16_at(0x2A),
        })
    }

    /// The build date and time the vendor stamps into the header.
    pub fn build_stamp(image: &[u8]) -> String {
        String::from_utf8_lossy(&image[0x0E..0x23]).replace('\0', " ").trim().to_string()
    }

    /// Everything wrong with an image that can be seen without a device.
    ///
    /// Separate from [`validate`](Self::validate) so the same gate runs offline,
    /// in `verify-image`, rather than only on the path that writes.
    pub fn check(&self, image: &[u8]) -> Result<()> {
        self.image_type.ensure_recoverable()?;
        if self.image_type != ImageType::Firmware {
            bail!(
                "image declares type {:?}, and only {:?} may be written to this \
                 keyboard's firmware file",
                self.image_type,
                ImageType::Firmware
            );
        }

        if self.length as usize != image.len() {
            bail!("image declares {} bytes and is {}", self.length, image.len());
        }

        let computed = crc32_mpeg2(&image[4..]);
        if computed != self.crc {
            bail!(
                "CRC mismatch: the header says {:#010x} and the contents give {:#010x}",
                self.crc,
                computed
            );
        }

        if self.entry.is_multiple_of(2) {
            bail!(
                "entry {:#010x} is even, so not a Thumb address; the bootloader \
                 branches to it and would fault",
                self.entry
            );
        }

        let low = APPLICATION_BASE + HEADER_SIZE as u32;
        let high = APPLICATION_BASE + APPLICATION_SIZE;
        if !(low..high).contains(&(self.entry & !1)) {
            bail!(
                "entry {:#010x} is outside the application region {low:#07x}..{high:#07x}",
                self.entry
            );
        }

        if image.len() as u32 > APPLICATION_SIZE {
            bail!(
                "{} bytes does not fit the {} KB application region",
                image.len(),
                APPLICATION_SIZE / 1024
            );
        }

        Ok(())
    }

    /// [`check`](Self::check), plus the one thing that needs the keyboard: that
    /// the image was built for the model on the other end of the cable.
    pub fn validate(&self, image: &[u8], product: u16) -> Result<()> {
        self.check(image)?;

        if self.product_id != product {
            bail!(
                "image is built for product {:#06x} and this keyboard is {:#06x}",
                self.product_id,
                product
            );
        }

        Ok(())
    }

    /// The version this image declares, formatted the way the device reports its own.
    ///
    /// The header carries only the minor and patch; the major is the `0` every published
    /// K100 image and every reading off the device has shown.
    pub fn version(&self) -> String {
        format!("0.{}.{}", self.version_minor, self.version_patch)
    }

    pub fn describe(&self) -> String {
        format!(
            "0.{}.{}, entry {:#010x}, product {:#06x}, type {:?}, {} bytes",
            self.version_minor,
            self.version_patch,
            self.entry,
            self.product_id,
            self.image_type,
            self.length
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A minimal well-formed container, so each test can break exactly one thing.
    fn sound(product: u16, image_type: u16, entry: u32) -> Vec<u8> {
        let mut image = vec![0u8; HEADER_SIZE + 0x400];
        let length = image.len() as u32;
        image[0x04..0x08].copy_from_slice(&length.to_le_bytes());
        image[0x08..0x0C].copy_from_slice(&entry.to_le_bytes());
        image[0x0C..0x0E].copy_from_slice(&1u16.to_le_bytes());
        image[0x25..0x27].copy_from_slice(&product.to_le_bytes());
        image[0x27..0x29].copy_from_slice(&image_type.to_le_bytes());

        let crc = crc32_mpeg2(&image[4..]);
        image[0x00..0x04].copy_from_slice(&crc.to_le_bytes());
        image
    }

    /// Pins the checksum to the value both official images are stamped with, computed
    /// over their own contents. A wrong variant of CRC-32 would still look like a
    /// checksum and would reject every image the keyboard accepts.
    #[test]
    fn crc_matches_the_published_algorithm() {
        assert_eq!(crc32_mpeg2(b""), 0xFFFF_FFFF);
        assert_eq!(crc32_mpeg2(b"123456789"), 0x0376_E6E7);
    }

    #[test]
    fn accepts_a_sound_firmware_image() {
        let image = sound(0x1b7c, 1, 0x0001_065D);
        Container::parse(&image).unwrap().validate(&image, 0x1b7c).unwrap();
    }

    /// The product match is the only check that needs the keyboard, so it is the only
    /// one `verify-image` cannot exercise. It is therefore the one worth a test: an
    /// image for the i.MX RT variant is a valid container and ruinous on this chip.
    #[test]
    fn refuses_an_image_built_for_another_product() {
        let image = sound(0x1bc5, 1, 0x0001_065D);
        let header = Container::parse(&image).unwrap();

        header.check(&image).expect("a container for another model is still well formed");

        let error = header.validate(&image, 0x1b7c).unwrap_err().to_string();
        assert!(error.contains("0x1bc5"), "{error}");
        assert!(error.contains("0x1b7c"), "{error}");
    }

    #[test]
    fn refuses_a_bootloader_image() {
        let image = sound(0x1b7c, 2, 0x0001_065D);
        let header = Container::parse(&image).unwrap();
        assert!(header.validate(&image, 0x1b7c).is_err());
    }
}
