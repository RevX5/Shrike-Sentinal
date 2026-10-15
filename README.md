# Shrike Sentinel

**Automated embedded-firmware stress testing with the Vicharak Shrike Lite (RP2040 + FPGA).**

Shrike Sentinel exercises the FPGA-to-MCU event path with a repeatable 10,000-pulse burst. It compares three independent measurements:

1. **FPGA internal counter** — the number of generated rising edges.
2. **RP2040 PIO counter** — an independent hardware-assisted count of input edges on GPIO14.
3. **RP2040 GPIO IRQ counter** — a software interrupt-based count of rising edges on GPIO14.

The RP2040 configures the FPGA from an embedded MCU-compatible bitstream at boot, reads the FPGA counter over SPI, and reports the comparison over USB serial.

> **Status:** Prototype validated on the author's Shrike Lite. The repository contains the uploaded source files and build/setup documentation. The generated FPGA bitstream and Pico SDK import file are not included in this archive; follow the setup steps below before building. A successful local SDK build is not claimed here.

## Practical use case: diagnose missed GPIO interrupts

Imagine an embedded engineer has changed an interrupt handler in a motor-control, encoder, or sensor-processing project. The application still appears to run, but the engineer suspects that occasional input events are being missed under load. Counting only in the interrupt handler cannot distinguish a faulty pulse generator from a problem in the MCU capture path.

Shrike Sentinel provides a controlled experiment without requiring an external pulse generator:

1. The FPGA generates a deterministic burst of **10,000 rising edges** and keeps its own count.
2. The RP2040 PIO state machine counts the incoming edges on GPIO14 independently of the C interrupt handler.
3. The RP2040 GPIO interrupt handler counts the same input edges.
4. Firmware reads the FPGA's internal count over SPI and prints all three counts and the comparisons over USB serial.

### How to interpret a result

| FPGA count | PIO count | IRQ count | What it suggests |
|---:|---:|---:|---|
| 10,000 | 10,000 | 10,000 | All three counting paths agreed for this burst. |
| 10,000 | 10,000 | 9,998 | The FPGA generated the expected burst and PIO observed it, while the IRQ count was short. Investigate the IRQ handler, interrupt configuration, and counter bookkeeping. |
| 10,000 | 9,998 | 9,998 | The IRQ result agrees with PIO, but PIO does not agree with the FPGA. Investigate the signal path, pin mapping, and capture setup before blaming the IRQ handler. |
| SPI read fails | — | — | The run cannot validate the generator count until SPI readback is fixed. |

These comparisons localize a discrepancy; they do not by themselves prove the exact root cause. Follow-up tests should change one variable at a time and retain the serial logs.

### Example engineering workflow

A firmware engineer records a baseline, makes one change to the GPIO interrupt path, rebuilds the firmware, and reruns the same burst test. If a mismatch occurs, the FPGA and PIO counts provide two references for deciding which part of the event path needs investigation. The engineer then compares repeated runs before and after the change rather than relying on a single successful burst.

**Current limitation:** the repository implements a manually triggered, fixed 10,000-pulse burst. It is a focused diagnostic prototype, not a general-purpose production test framework; it does not currently automate multi-run testing or certify a maximum interrupt rate.

## Features

- RP2040 firmware written in C using the Raspberry Pi Pico SDK.
- FPGA pulse-burst generator written in Verilog.
- 10,000 rising edges per burst (FPGA internal count).
- Three-way comparison: FPGA counter, PIO count, and GPIO IRQ count.
- SPI counter readback (SPI mode 0, 8-bit, MSB first).
- USB CDC command console, watchdog, uptime, and in-RAM event log.
- PIO-based independent capture reference.
- Manual `EVENT BURST` workflow (automated multi-burst testing is intentionally not included).

## Repository layout

```text
shrike-sentinel/
├── CMakeLists.txt
├── hello_usb.c
├── sentinel_pulse_counter.pio
├── bitstream/
│   └── README.md
├── fpga/
│   ├── main.v
│   └── README.md
├── docs/
│   ├── architecture.md
│   ├── build-and-flash.md
│   ├── pin-mapping.md
│   ├── test-results.md
│   └── licensing.md
├── scripts/
│   └── fetch_dependencies.ps1
├── THIRD_PARTY_NOTICES.md
├── LICENSE-MIT
└── LICENSE-CERN-OHL-1.2
└── .gitignore
```

The Vicharak SPI-target HDL is an upstream dependency. The PowerShell helper downloads that file and its license notice, along with the Pico SDK import helper, from their upstream repositories. See [Third-party notices](THIRD_PARTY_NOTICES.md).

## Hardware and software

- Board: **Vicharak Shrike Lite** (RP2040 + FPGA)
- MCU SDK configuration in the supplied CMake file: Pico SDK **2.3.1**
- Current FPGA parameter: `HALF_PERIOD_LIMIT = 314`
- Nominal FPGA clock assumed by the source: 50 MHz
- Calculated pulse rate: `50,000,000 / (2 × (314 + 1)) ≈ 79.37 kHz`
- Expected burst length: `10,000` rising edges
- Console: USB CDC, typically opened at 115200 baud by the serial terminal

## Measured result reported by the author

At the configuration with `HALF_PERIOD_LIMIT = 314`, the author reported **20 consecutive passing bursts**. Each reported burst showed:

- FPGA internal count: `10000`
- RP2040 PIO observed: `10000`
- RP2040 IRQ observed: `10000`
- All comparison checks: `PASS`
- Elapsed time: approximately `174–175 ms`, including the firmware's quiet-period detection

This is an observed validation result from one test series, not a guarantee of zero missed events in all operating conditions. Earlier runs at other firmware configurations had intermittent IRQ discrepancies; see [test results](docs/test-results.md).

## Quick start

1. Clone or download this repository.
2. Open PowerShell in the repository root and run:

   ```powershell
   .\scripts\fetch_dependencies.ps1
   ```

   This fetches `pico_sdk_import.cmake` and Vicharak's upstream `spi_target.v` and hardware license notice. Network access is required for this step.

3. Generate the MCU-compatible FPGA bitstream in Go Configure Software Hub from `fpga/main.v` together with `fpga/spi_target.v`. Use the I/O mapping documented in [docs/pin-mapping.md](docs/pin-mapping.md).
4. Put the generated binary at:

   ```text
   bitstream/FPGA_bitstream_MCU.bin
   ```

5. Install/configure the Raspberry Pi Pico VS Code extension and Pico SDK, then configure and build the project as described in [docs/build-and-flash.md](docs/build-and-flash.md).
6. Flash `build/hello_usb.uf2` to Shrike Lite using your usual BOOT/RPI-RP2 procedure.
7. Open the board's USB serial port and enter `HELP`, then `EVENT BURST`.

## Console commands

The current firmware includes these commands:

- `HELP`
- `STATUS`
- `UPTIME`
- `LOG`
- `CLEARLOG`
- `FAULT SIMULATE`
- `FAULT CLEAR`
- `FAULT STATUS`
- `WATCHDOG STATUS`
- `WATCHDOG TEST` — intentionally stops servicing the watchdog; use only when a reset is acceptable.
- `EVENT COUNT`
- `EVENT TEST`
- `EVENT BURST`
- `FPGA COUNT`

`EVENT TEST` is legacy diagnostic code expecting a continuous waveform; the current FPGA design is configured for triggered finite bursts. Use `EVENT BURST` for the current main test.

## Documentation

- [Architecture](docs/architecture.md)
- [Pin mapping](docs/pin-mapping.md)
- [Build and flash instructions](docs/build-and-flash.md)
- [Measured test results](docs/test-results.md)
- [Licensing notes](docs/licensing.md)

## Limitations

- The generated bitstream is board- and design-specific; it is not checked into this archive.
- The FPGA project is synthesized and implemented in Go Configure Software Hub, separately from the Pico SDK CMake build.
- The PIO counter and IRQ counter share GPIO14 as their input, while the FPGA counter is read back over SPI.
- The event log is held in RAM and is lost on reset.
- The current test harness uses a quiet-period timeout to decide that the burst has finished; elapsed time includes this period.
- Tested rates are experimental observations, not a certified maximum-rate specification.
