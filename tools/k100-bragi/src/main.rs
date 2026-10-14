//! Verifies the K100's firmware recovery path before any custom firmware is written.
//!
//! The ordering here is deliberate. `info` is read-only and establishes the framing the
//! device actually uses; the bootloader transitions are reversible; flashing comes last and
//! only once the framing is confirmed on real hardware. See `docs/recovery.md`.

mod container;
mod device;
mod roundtrip;
mod transport;

/// The full Bragi command set is defined here even where it is not wired up yet, so the
/// protocol stays documented in one place rather than being discovered twice.
#[allow(dead_code)]
mod protocol;

use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};

use device::Device;
use protocol::OperatingMode;

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

    /// Re-enumerate the keyboard, to revive a wedged command endpoint without
    /// reaching behind the desk. Changes no firmware and no setting.
    Reset,

    /// Put the keyboard into bootloader mode. Reversible with `exit-bootloader`.
    EnterBootloader {
        /// The product id to act on, and the only device that will be opened.
        #[arg(long, value_name = "PID", default_value = DEFAULT_TARGET,
              value_parser = parse_pid)]
        device: u16,
    },

    /// Return to normal operation via the vendor's documented transition path.
    ExitBootloader,

    /// Write a firmware image. Reports what it would do unless given --commit.
    Flash {
        /// A container built by tools/pack-firmware.py, or an official image.
        path: std::path::PathBuf,

        /// Actually write. Without this nothing is sent that changes the device.
        #[arg(long)]
        commit: bool,

        /// The product id to write to, and the only device that will be opened.
        /// Defaults to the OPX K100. In bootloader mode the keyboard may report
        /// something else; `info` says what, and that id goes here.
        #[arg(long, value_name = "PID", default_value = DEFAULT_TARGET,
              value_parser = parse_pid)]
        device: u16,
    },
}

/// The product id both write-side commands target unless told otherwise. A string
/// so `--help` prints it the way this project writes product ids everywhere else.
const DEFAULT_TARGET: &str = "0x1b7c";

/// Accepts `0x1b7c` and `1b7c` alike, since both forms appear in this project's notes.
fn parse_pid(text: &str) -> Result<u16, std::num::ParseIntError> {
    match text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        Some(hex) => u16::from_str_radix(hex, 16),
        None => u16::from_str_radix(text, 16),
    }
}

fn main() -> Result<()> {
    match Cli::parse().command {
        Action::Info => info(),
        Action::VerifyImage { path } => verify_image(&path),
        Action::Files => files(),
        Action::Reset => reset(),
        Action::EnterBootloader { device } => enter_bootloader(device),
        Action::ExitBootloader => exit_bootloader(),
        Action::Flash { path, commit, device } => flash(&path, commit, device),
    }
}

/// Writes a firmware image, having refused every reason not to.
///
/// The checks come first and in this order on purpose: the image is validated
/// before the keyboard is touched, the device is named rather than searched for,
/// the image is matched against what that device reports, and the mode is
/// confirmed before anything is written. Each one removes a way of writing the
/// wrong thing to the wrong device.
fn flash(path: &std::path::Path, commit: bool, product: u16) -> Result<()> {
    let image = std::fs::read(path)
        .with_context(|| format!("reading {}", path.display()))?;

    let header = container::Container::parse(&image)?;
    println!("image  {}", path.display());
    println!("       {}", header.describe());

    // Writing opens one named product id and nothing else, so no other device on
    // the bus can be reached from here even if it is also Corsair's.
    let device = Device::open_exact(product)?;
    println!("device {}", device.description());
    println!("       product id {:#06x}, the only id this command opened", device.product);

    // An image for another model, of the wrong type, or with a broken CRC is
    // refused here rather than by the keyboard.
    header.validate(&image, device.product)?;
    println!("       image matches this keyboard and its container is sound");

    let mode = device.operating_mode()?;
    println!("       operating mode {mode:?}");
    if mode != OperatingMode::Bootloader {
        bail!(
            "the keyboard is in {mode:?}, not Bootloader. Writing firmware from \
             here is not the documented path; run `enter-bootloader` first."
        );
    }

    let plan = roundtrip::flash(&device, &image, commit)?;

    if !commit {
        println!();
        println!("dry run: nothing was sent that changes the keyboard.");
        println!("  would write {} bytes to file id {}, as {} chunks of {} bytes in \
                  {}-byte reports, then apply",
                 plan.total, protocol::FILE_ID_FIRMWARE, plan.chunks, plan.chunk, plan.report);
        println!("  re-run with --commit to do it");
        return Ok(());
    }

    println!();
    println!("wrote {} bytes in {} chunks and applied", plan.total, plan.chunks);
    println!("the keyboard should re-enumerate; `info` will say what it is running");
    Ok(())
}

fn open() -> Result<Device> {
    Device::open()
}

fn info() -> Result<()> {
    let device = open()?;

    println!("{}", device.description());
    println!("  product id        {:#06x}", device.product);
    println!("  header layout     {:?}", device.layout);

    // The one number that distinguishes the two programs on this chip before any command is
    // sent, and the one that silently breaks everything if it is assumed. 1024 is the
    // application, 64 is the bootloader.
    println!("  report size       {} bytes, from the endpoint descriptor", device.report_size);

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

/// Runs the write path's own gate on a file, with no device involved.
///
/// Deliberately the same [`container::Container::check`] that `flash` calls, so
/// an image that passes here fails later only on the one thing this cannot know:
/// which keyboard is plugged in.
fn verify_image(path: &std::path::Path) -> Result<()> {
    let bytes = std::fs::read(path).with_context(|| format!("reading {}", path.display()))?;
    let header = container::Container::parse(&bytes)?;

    println!("{}", path.display());
    println!("  container         {}", header.describe());
    println!("  format version    {}", header.format_version);
    println!("  built             {}", container::Container::build_stamp(&bytes));

    header.check(&bytes)?;
    println!("  checks            pass: length, CRC, type and entry point are all sound");
    println!();
    println!("This says the container is well formed. It says nothing about whether the");
    println!("code inside it runs.");
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

/// Re-enumerates the keyboard and reports whether that revived it.
///
/// Deliberately does its own read afterwards: a reset that returns success only
/// means the ioctl was accepted, and the thing worth knowing is whether the
/// command endpoint answers again, which only a command can establish.
fn reset() -> Result<()> {
    let device = open()?;
    println!("{}", device.description());

    device.reset()?;
    println!("  port reset; waiting for the keyboard to come back");

    std::thread::sleep(std::time::Duration::from_secs(2));

    let device = open().context("the keyboard did not reappear after the reset")?;
    match device.operating_mode() {
        Ok(mode) => {
            println!("  back on the bus, operating mode {mode:?}");
            println!();
            println!("The command endpoint answers again.");
            Ok(())
        }
        Err(error) => {
            println!("  back on the bus, but the command endpoint still does not answer");
            println!();
            println!("A port reset does not drop VBUS. Unplug the keyboard, wait a few");
            println!("seconds and plug it back in; that is what docs/recovery.md step 0 asks");
            println!("for and it is known to work.");
            Err(error)
        }
    }
}

/// Entering the bootloader names its target, for the same reason flashing does:
/// it is a deliberate change to a working keyboard, and it is only ever entered
/// from a known application whose product id is therefore known.
fn enter_bootloader(product: u16) -> Result<()> {
    roundtrip::enter_bootloader(Device::open_exact(product)?)
}

/// Leaving it does not, and must not. This is the direction that rescues a
/// keyboard, and a bootloader may report a product id this tool has never seen;
/// refusing to open it then would strand the very device this exists to recover.
fn exit_bootloader() -> Result<()> {
    roundtrip::exit_bootloader(open()?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use protocol::Model;

    /// `DEFAULT_TARGET` is a string so `--help` can print a hex product id, which
    /// means nothing stops it drifting from the model it is meant to name. This is
    /// what stops it: the default write target is the OPX K100 and nothing else.
    #[test]
    fn the_default_target_is_the_opx_k100() {
        assert_eq!(parse_pid(DEFAULT_TARGET).unwrap(), Model::OpticalOpx.pid());
    }

    #[test]
    fn product_ids_parse_with_or_without_the_prefix() {
        assert_eq!(parse_pid("0x1b7c").unwrap(), 0x1b7c);
        assert_eq!(parse_pid("1b7c").unwrap(), 0x1b7c);
        assert!(parse_pid("nonsense").is_err());
    }

    #[test]
    fn the_cli_is_well_formed() {
        use clap::CommandFactory;
        Cli::command().debug_assert();
    }
}
