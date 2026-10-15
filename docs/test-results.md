# Test results

These results are transcribed from the serial-console logs supplied by the project author. They represent observed tests on one Shrike Lite setup.

## Current validated configuration

**FPGA parameter:** `HALF_PERIOD_LIMIT = 314`  
**Assumed clock:** 50 MHz  
**Calculated pulse rate:** approximately 79.37 kHz  
**Burst size:** 10,000 rising edges

| Metric | Reported outcome |
|---|---:|
| Bursts in latest 80 kHz-class test series | 20 |
| Passing bursts | 20 / 20 |
| Generated pulses across the series | 200,000 |
| IRQ misses in that series | 0 |
| FPGA internal count per burst | 10,000 |
| PIO count per burst | 10,000 |
| GPIO IRQ count per burst | 10,000 |
| Reported elapsed time | 174–175 ms |

## Prior diagnostic history

Earlier firmware configurations exhibited intermittent IRQ discrepancies while the FPGA internal count and PIO reference count continued to report 10,000. The project author then:
- moved the direct GPIO IRQ handler to SRAM;
- registered a direct exclusive handler for `IO_IRQ_BANK0`;
- set GPIO bank interrupt priority to `0x00`;
- set USB controller interrupt priority to `0xC0`.

Following those changes, the author reported 20 consecutive passing bursts in the 70 kHz-class test series and 20 consecutive passing bursts in the 80 kHz-class series.

Historical results should not be combined with the current final series because the firmware and interrupt configuration changed between test groups.

## Interpretation

- The matched FPGA / PIO / IRQ counters show that all three measurement paths agreed during the reported passing runs.
- Twenty passing runs is useful evidence, but not proof of a hard real-time guarantee or zero failure probability in all conditions.
- The current project does not implement an automated multi-burst stress command; tests were run manually using `EVENT BURST`.
- If the test is repeated, preserve console logs and note the firmware build, bitstream hash, FPGA parameter, and date so that future results can be compared meaningfully.
