    # Generated Cmake Pico project file

    cmake_minimum_required(VERSION 3.13)

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # Initialise pico_sdk from installed location
    # (note this can come from environment, CMake cache etc)

    # == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
    if(WIN32)
        set(USERHOME $ENV{USERPROFILE})
    else()
        set(USERHOME $ENV{HOME})
    endif()
    set(sdkVersion 2.3.1)
    set(toolchainVersion 15_2_Rel1)
    set(picotoolVersion 2.3.1)
    set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
    if (EXISTS ${picoVscode})
        include(${picoVscode})
    endif()
    # ====================================================================================
    set(PICO_BOARD pico CACHE STRING "Board type")

    # Pull in Raspberry Pi Pico SDK (must be before project)
    include(pico_sdk_import.cmake)

    project(hello_usb C CXX ASM)

    # Initialise the Raspberry Pi Pico SDK
    pico_sdk_init()

    # Add executable. Default name is the project name, version 0.1

    if (TARGET tinyusb_device)
        # Embed the FPGA bitstream into the firmware.
    set(BITSTREAM_FILE
        "${CMAKE_CURRENT_LIST_DIR}/bitstream/FPGA_bitstream_MCU.bin"
    )

    if(NOT EXISTS "${BITSTREAM_FILE}")
        message(FATAL_ERROR "FPGA bitstream not found: ${BITSTREAM_FILE}")
    endif()

    file(READ "${BITSTREAM_FILE}" BITS_HEX HEX)
    string(LENGTH "${BITS_HEX}" BITS_HEX_LEN)
    math(EXPR BITS_LEN "${BITS_HEX_LEN} / 2")
    string(REGEX REPLACE "(..)" "0x\\1," BITS_ARRAY "${BITS_HEX}")

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bitstream.c"
        "#include <stdint.h>\n"
        "const uint32_t fpga_bitstream_len = ${BITS_LEN};\n"
        "const uint8_t fpga_bitstream[${BITS_LEN}] = {${BITS_ARRAY}};\n"
    )

    add_executable(hello_usb
        hello_usb.c
        "${CMAKE_CURRENT_BINARY_DIR}/bitstream.c"
    )

        pico_generate_pio_header(hello_usb ${CMAKE_CURRENT_LIST_DIR}/sentinel_pulse_counter.pio)

        # pull in common dependencies
        target_link_libraries(hello_usb 
            pico_stdlib
            hardware_spi
            hardware_pio
            hardware_irq
        ) 

        # enable usb output, disable uart output
        pico_enable_stdio_usb(hello_usb 1)
        pico_enable_stdio_uart(hello_usb 0)

        target_link_libraries(hello_usb hardware_watchdog)

        # create map/bin/hex/uf2 file etc.
        pico_add_extra_outputs(hello_usb)

        # add url via pico_set_program_url
    elseif(PICO_ON_DEVICE)
        message("Skipping hello_usb because TinyUSB submodule is not initialized in the SDK")
    endif()

    # Reconfigure after updating the 80 kHz FPGA burst generator
    


    