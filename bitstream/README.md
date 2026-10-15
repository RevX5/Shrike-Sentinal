# FPGA bitstream

`CMakeLists.txt` expects the generated MCU-compatible bitstream at:

```text
bitstream/FPGA_bitstream_MCU.bin
```

This binary is intentionally not included in the repository archive because it was not provided with the source files. Generate it from the FPGA project that matches `fpga/main.v`, `fpga/spi_target.v`, and the documented I/O mapping. Do not use an older bitstream or a binary generated for different I/O assignments.

After generation, place your known-good binary here before configuring/building the Pico project. Decide whether to commit the binary to GitHub; if you do, ensure you have the necessary rights and that the file matches the source and documented settings.
