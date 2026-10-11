//! Verifies the K100's firmware recovery path before any custom firmware is written.
//!
//! The ordering here is deliberate. `info` is read-only and establishes the framing the
//! device actually uses; the bootloader transitions are reversible; flashing comes last and
//! only once the framing is confirmed on real hardware. See `docs/recovery.md`.

mod device;
mod roundtrip;
mod transport;

/// The full Bragi command set is defined here even where it is not wired up yet, so the
/// protocol stays documented in one place rather than being discovered twice.
#[allow(dead_code)]
mod protocol;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};

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

    /// Report which Bragi files this keyboard exposes. Opens and closes each; never
    /// touches firmware.
    Files,

    /// Put the keyboard into bootloader mode. Reversible with `exit-bootloader`.
    EnterBootloader,

    /// Return to normal operation via the vendor's documented transition path.
    ExitBootloader,
}

fn main() -> Result<()> {
    match Cli::parse().command {
        Action::Info => info(),
        Action::VerifyImage { path } => verify_image(&path),
        Action::Files => files(),
        Action::EnterBootloader => enter_bootloader(),
        Action::ExitBootloader => exit_bootloader(),
    }
}

fn open() -> Result<Device> {
    Device::open()
}

fn info() -> Result<()> {
    let device = open()?;

    println!("{}", device.description());
    println!("  product id        {:#06x}", device.product);
    println!("  header layout     {:?}", device.layout);

    match device.usb_pid() {
        Ok(pid) => println!("  reported pid      {pid:#06x}"),
        Err(error) => println!("  reported pid      unavailable ({error})"),
    }

    print_version("firmware", device.firmware_version());
    print_version("bootloader", device.bootloader_version());

    match device.hw_layout() {
        Ok(layout) => println!("  physical layout   {layout:?}"),
        Err(error) => println!("  physical layout   unavailable ({error:#})"),
    }

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

fn print_version(label: &str, version: Result<String>) {
    match version {
        Ok(text) => println!("  {label:<17} {text}"),
        Err(error) => println!("  {label:<17} unavailable ({error:#})"),
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

/// Probes each known file ID, skipping firmware.
///
/// Needs host control: these commands are refused with `InvalidState` while the keyboard is
/// self-operated. The previous mode is restored afterwards.
fn files() -> Result<()> {
    let device = open()?;
    const HANDLE: u8 = 1;

    println!("{}", device.description());
    println!("  probing {} file ids in host-controlled mode\n",
             protocol::FILE_IDS.len() - 1);

    device.with_host_control(|| {
        for &(id, name) in protocol::FILE_IDS {
            if id == protocol::FILE_ID_FIRMWARE {
                println!("  {id:>3} {name:<26} skipped deliberately (see docs/recovery.md)");
                continue;
            }

            let opened = device.open_file(HANDLE, id)?;
            let status = opened.status();

            // Unbind unconditionally: a handle left open makes every later open fail with
            // InvalidState, which looks exactly like a protocol error.
            let released = device.unbind_buffers(HANDLE)?.status();

            let note = if released.is_success() { String::new() }
                       else { format!("  (unbind {released:?})") };
            println!("  {id:>3} {name:<26} {status:?}{note}");
        }
        Ok(())
    })
}

fn enter_bootloader() -> Result<()> {
    let device = open()?;
    roundtrip::enter_bootloader(&device)
}

fn exit_bootloader() -> Result<()> {
    let device = open()?;
    roundtrip::exit_bootloader(&device)
}
