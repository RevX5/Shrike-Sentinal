# Licensing notes — review before public release

This prepared repository does **not** silently assign a single license to every original Shrike Sentinel file. Before publishing, decide and document the license for your own firmware, FPGA top-level design, and project documentation.

The upstream Shrike repository states that its software is GPL-2.0 and that its hardware-design documentation / HDL / RTL is covered by CERN Open Hardware Licence v1.2. The `spi_target.v` file used by this project is fetched from Vicharak's public Shrike repository; its upstream location and license notice are recorded in `THIRD_PARTY_NOTICES.md`.

When distributing modified CERN-OHL-licensed hardware documentation, retain applicable notices and include the full license as required by its terms. The helper script fetches the license file but does not automatically choose a license for the rest of this project.

This file is informational, not legal advice. Do not describe the project as MIT-licensed or GPL-licensed as a whole unless you have selected that license and checked compatibility for all included components.
