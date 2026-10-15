$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$fpgaDir = Join-Path $repoRoot "fpga"
$thirdPartyDir = Join-Path $repoRoot "third_party\vicharak"

New-Item -ItemType Directory -Force -Path $fpgaDir | Out-Null
New-Item -ItemType Directory -Force -Path $thirdPartyDir | Out-Null

$downloads = @(
    @{
        Url = "https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.3.1/external/pico_sdk_import.cmake"
        Path = (Join-Path $repoRoot "pico_sdk_import.cmake")
        Label = "Raspberry Pi Pico SDK import helper"
    },
    @{
        Url = "https://raw.githubusercontent.com/vicharak-in/shrike/main/examples/usb_to_spi/ffpga/src/spi_target.v"
        Path = (Join-Path $fpgaDir "spi_target.v")
        Label = "Vicharak Shrike SPI target HDL"
    },
    @{
        Url = "https://raw.githubusercontent.com/vicharak-in/shrike/main/LICENSE_HW.md"
        Path = (Join-Path $thirdPartyDir "LICENSE_HW.md")
        Label = "Vicharak hardware license notice"
    }
)

foreach ($item in $downloads) {
    Write-Host "Downloading $($item.Label)..."
    Invoke-WebRequest -Uri $item.Url -OutFile $item.Path
    if (!(Test-Path $item.Path) -or (Get-Item $item.Path).Length -eq 0) {
        throw "Download failed or produced an empty file: $($item.Path)"
    }
    Write-Host "Saved: $($item.Path)"
}

Write-Host ""
Write-Host "Dependencies fetched. Next, generate bitstream/FPGA_bitstream_MCU.bin in Go Configure Software Hub."
