# Build and flash

This guide is based on the current Windows project and its CMake configuration.

## Prerequisites

- Vicharak Shrike Lite board.
- Go Configure Software Hub configured for the on-board FPGA.
- VS Code with the Raspberry Pi Pico extension.
- Raspberry Pi Pico SDK **2.3.1**, with its required compiler, CMake, and build tools installed.
- USB data cable and a serial terminal (for example, PuTTY).
- Generated MCU-compatible FPGA bitstream.

### Pico SDK setup

1. Install the Raspberry Pi Pico extension in VS Code and follow its setup instructions to install or locate Pico SDK 2.3.1 and the required toolchain.
2. Ensure the SDK path is configured as required by the extension and the project's `pico_sdk_import.cmake`.
3. Confirm that the SDK and toolchain are available before configuring the project.

The repository's `scripts/fetch_dependencies.ps1` downloads the Pico SDK import helper; **it does not download the complete Pico SDK or install the toolchain**.

## 1. Fetch required source helpers

From the repository root, run PowerShell:

```powershell
.\scripts\fetch_dependencies.ps1
```

The script downloads:
- `pico_sdk_import.cmake` from Raspberry Pi's Pico SDK 2.3.1.
- `fpga/spi_target.v` from Vicharak's Shrike example.
- `third_party/vicharak/LICENSE_HW.md` for the upstream HDL license notice.

Review the notices before redistributing the fetched file.

## 2. Generate the FPGA bitstream

1. Open Go Configure Software Hub.
2. Create/open the SLG47910V (Rev BB) FPGA project used for Shrike Lite.
3. Add `fpga/main.v` and `fpga/spi_target.v` to the design sources; ensure `shrike_sentinel_events` is the top-level module.
4. Apply the pin mappings documented in `pin-mapping.md`.
5. Synthesize and check the result; the SPI module must be part of the hierarchy.
6. Generate the MCU-compatible FPGA configuration binary.
7. Copy the generated binary to:

   ```text
   bitstream/FPGA_bitstream_MCU.bin
   ```

The current CMake file intentionally stops with a fatal error if the binary is missing. Don't substitute a blank file or a bitstream from a different FPGA configuration.

## 3. Configure and build the RP2040 firmware

1. Open this repository folder in VS Code.
2. Ensure `pico_sdk_import.cmake` is present in the repository root.
3. Ensure `bitstream/FPGA_bitstream_MCU.bin` exists.
4. Run **Raspberry Pi Pico: Configure Project** from the Command Palette.
5. Run **Raspberry Pi Pico: Build Project**.
6. Confirm that `build/hello_usb.uf2` is generated.

This source archive has not been compiled in the current environment because the local Pico SDK, Go Configure toolchain, and generated bitstream were not provided. Treat the local build as the verification step.

## 4. Flash

1. Back up the current working UF2 and FPGA binary.
2. Put Shrike Lite in BOOT mode and connect USB; wait for the `RPI-RP2` drive.
3. Copy `build/hello_usb.uf2` to that drive.
4. Wait for the board to reboot.
5. Open the board's USB CDC serial COM port at 115200 baud.

The firmware configures the FPGA on startup using the embedded bitstream. A successful startup should report successful bitstream transmission, FPGA SPI counter readback enabled, the GPIO IRQ counter enabled on GPIO14, and `PIO reference capture: READY`.

## 5. Run a burst

At the serial prompt:

```text
STATUS
EVENT BURST
```

A passing 80 kHz-class burst should report 10,000 for all three counters, all checks passing, and elapsed time around 174–175 ms. This is the observed result for the tested configuration, not a guarantee that every future run will pass.

## Recovery notes

- Keep a copy of the last known-good `hello_usb.uf2` and `FPGA_bitstream_MCU.bin`.
- If synthesis fails, inspect the first error and check the `spi_target` source is added to the Go Configure project.
- If the CMake configure step reports the bitstream is missing, generate the correct MCU binary and place it in the required path.
- If the serial terminal prints `PIO reference capture: FAILED`, check that `sentinel_pulse_counter.pio` is at the project root and the CMake PIO header generation step is enabled.
