# Third-party notices

## Vicharak Shrike — SPI target HDL

The design instantiates `spi_target`, maintained in Vicharak's public Shrike repository:

- Source: https://github.com/vicharak-in/shrike/blob/main/examples/usb_to_spi/ffpga/src/spi_target.v
- Upstream hardware license: https://github.com/vicharak-in/shrike/blob/main/LICENSE_HW.md
- Repository: https://github.com/vicharak-in/shrike

Vicharak's repository identifies its HDL / RTL and hardware design documentation as covered by CERN Open Hardware Licence v1.2. Keep the upstream notices and the downloaded `LICENSE_HW.md` when redistributing this dependency. `scripts/fetch_dependencies.ps1` fetches both into this repository.

## Raspberry Pi Pico SDK import helper

The setup script fetches `pico_sdk_import.cmake` from Raspberry Pi's Pico SDK tag `2.3.1`:

- Source: https://github.com/raspberrypi/pico-sdk/blob/2.3.1/external/pico_sdk_import.cmake
- Repository: https://github.com/raspberrypi/pico-sdk

The Pico SDK and its helper files remain subject to the license terms provided by the upstream SDK.
