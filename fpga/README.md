# FPGA sources

`main.v` is the Shrike Sentinel top-level design. It generates a 10,000-pulse burst, blinks the on-board LED, and exposes an SPI target interface to report the pulse counter.

The top-level instantiates Vicharak's `spi_target` module. Fetch it and its hardware license notice by running:

```powershell
.\scripts\fetch_dependencies.ps1
```

The expected result is `fpga/spi_target.v`. Add both `main.v` and `spi_target.v` as source files in Go Configure Software Hub and set `shrike_sentinel_events` as the top-level module.

Current source parameter: `HALF_PERIOD_LIMIT = 314` (about 79.37 kHz with a nominal 50 MHz clock). Keep known-good backups before making changes.
