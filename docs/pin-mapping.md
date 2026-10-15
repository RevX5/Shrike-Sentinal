# Pin mapping

The exact I/O planner labels depend on the Go Configure Software Hub project and Shrike Lite revision. Verify every assignment against the board documentation and the mapping used in the known-good bitstream.

## RP2040-side mapping

| Signal / role | RP2040 GPIO | Notes |
|---|---:|---|
| SPI0 MISO | GPIO0 | FPGA counter readback; FPGA configuration also uses this SPI bus |
| Manual SPI chip select | GPIO1 | Active-low, controlled by firmware as SIO |
| SPI0 SCK | GPIO2 | SPI clock |
| SPI0 MOSI | GPIO3 | Configuration and read commands |
| FPGA power control | GPIO12 | `PIN_PWR` in firmware |
| FPGA enable control | GPIO13 | `PIN_EN` in firmware |
| FPGA EVENT_OUT input | GPIO14 | Counted by GPIO IRQ and observed by PIO |
| FPGA START output | GPIO15 | Triggers the FPGA's finite event burst |

## FPGA I/O Planner signals

Use this table to recreate the mappings from the tested design; confirm against the board-specific mapping guide before generating a new bitstream.

| Verilog top-level signal | FPGA I/O planner mapping |
|---|---|
| `clk` | `OSC_CLK` |
| `clk_en` | `OSC_EN` |
| `LED` | `GPIO16_OUT` |
| `LED_en` | `GPIO16_OE` |
| `START` | `GPIO17_IN` |
| `EVENT_OUT` | `GPIO18_OUT` |
| `EVENT_OE` | `GPIO18_OE` |
| `spi_sck` | `GPIO03_IN` |
| `spi_ss_n` | `GPIO04_IN` |
| `spi_mosi` | `GPIO05_IN` |
| `spi_miso` | `GPIO06_OUT` |
| `spi_miso_en` | `GPIO06_OE` |

The SPI ports are separate from the RP2040 signal GPIOs because the board routes these signals through the FPGA I/O fabric. Don't rename a port or change its I/O assignment without regenerating and testing the bitstream.
