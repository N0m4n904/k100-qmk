//! Verifies the K100's firmware recovery path before any custom firmware is written.
//!
//! The ordering here is deliberate. `info` is read-only and establishes the framing the
//! device actually uses; the bootloader transitions are reversible; flashing comes last and
//! only once the framing is confirmed on real hardware. See `docs/recovery.md`.

mod device;
mod roundtrip;

/// The full Bragi command set is defined here even where it is not wired up yet, so the
/// protocol stays documented in one place rather than being discovered twice.
#[allow(dead_code)]
mod protocol;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use hidapi::HidApi;

use device::Device;
use protocol::{Image, OperatingMode};

#[derive(Parser)]
#[command(about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Action,
}

#[derive(Subcommand)]
enum Action {
    /// Identify the keyboard and read its versions and operating mode. Read-only.
    Info,

    /// Check a firmware image's container without touching the keyboard. Offline.
    VerifyImage {
        /// Path to a `.bin` extracted by tools/fetch-firmware.py
        path: std::path::PathBuf,
    },

    /// Put the keyboard into bootloader mode. Reversible with `exit-bootloader`.
    EnterBootloader,

    /// Return to normal operation via the vendor's documented transition path.
    ExitBootloader,
}

fn main() -> Result<()> {
    match Cli::parse().command {
        Action::Info => info(),
        Action::VerifyImage { path } => verify_image(&path),
        Action::EnterBootloader => enter_bootloader(),
        Action::ExitBootloader => exit_bootloader(),
    }
}

fn open() -> Result<Device> {
    let api = HidApi::new().context("initialising hidapi")?;
    Device::open(&api)
}

fn info() -> Result<()> {
    let device = open()?;

    println!("{}", device.model.description());
    println!("  product id        {:#06x}", device.model.pid());
    println!("  report length     {} bytes", device.report_length);
    println!("  header layout     {:?}", device.layout);

    match device.usb_pid() {
        Ok(pid) => println!("  reported pid      {pid:#06x}"),
        Err(error) => println!("  reported pid      unavailable ({error})"),
    }

    print_version("firmware", device.firmware_version());
    print_version("bootloader", device.bootloader_version());

    match device.operating_mode() {
        Ok(mode) => {
            println!("  operating mode    {mode:?}");
            if mode == OperatingMode::Bootloader {
                println!();
                println!("This keyboard is sitting in bootloader mode. That is a recoverable");
                println!("state, not a brick: flash a verified image, then `exit-bootloader`.");
            }
        }
        Err(error) => println!("  operating mode    unavailable ({error})"),
    }

    Ok(())
}

fn print_version(label: &str, version: Result<u16>) {
    match version {
        Ok(raw) => println!("  {label:<17} {}.{} (raw {raw:#06x})", raw >> 8, raw & 0xff),
        Err(error) => println!("  {label:<17} unavailable ({error})"),
    }
}

fn verify_image(path: &std::path::Path) -> Result<()> {
    let bytes = std::fs::read(path).with_context(|| format!("reading {}", path.display()))?;
    let image = Image::parse(&bytes)?;

    println!("{}", path.display());
    println!("  length            {} bytes", bytes.len());
    println!("  built             {}", image.build_stamp());
    println!("  crc32-mpeg2       {:#010x} (verified)", image.declared_crc());
    Ok(())
}

fn enter_bootloader() -> Result<()> {
    let device = open()?;
    roundtrip::enter_bootloader(&device)
}

fn exit_bootloader() -> Result<()> {
    let device = open()?;
    roundtrip::exit_bootloader(&device)
}
