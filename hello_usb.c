#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "sentinel_pulse_counter.pio.h"
#include "pico/platform.h"
#include "hardware/irq.h"

#define COMMAND_BUFFER_SIZE 64
#define EVENT_LOG_CAPACITY  16
#define CFG_SPI       spi0
#define PIN_CFG_MISO  0
#define PIN_CFG_SS    1
#define PIN_CFG_SCK   2
#define PIN_CFG_MOSI  3
#define PIN_PWR       12
#define PIN_EN        13
#define FPGA_EVENT_GPIO 14
#define EVENT_TEST_DURATION_MS 30000
#define EVENT_NOMINAL_HZ       10000
#define FPGA_START_GPIO 15
#define BURST_EXPECTED_PULSES 10000
#define BURST_TIMEOUT_MS 2500
#define BURST_QUIET_MS 50
#define FPGA_SPI_BAUD_HZ 100000u
#define PIO_CAPTURE_IDLE_LOOPS 2500000u /* about 40 ms at 125 MHz */

#define FPGA_CMD_SNAPSHOT 0xA0u
#define FPGA_CMD_READ_B0   0xA1u
#define FPGA_CMD_READ_B1   0xA2u
#define FPGA_CMD_READ_B2   0xA3u

typedef enum {
    EVENT_BOOT = 1,
    EVENT_HELP,
    EVENT_STATUS,
    EVENT_UPTIME,
    EVENT_UNKNOWN_COMMAND,
    EVENT_INPUT_TOO_LONG,
    EVENT_FAULT_SIMULATED,
    EVENT_FAULT_CLEARED,
    EVENT_LOG_CLEARED,
    EVENT_LOG_READ,
    EVENT_WATCHDOG_TEST
} event_code_t;

typedef struct {
    uint32_t sequence;
    uint64_t uptime_ms;
    event_code_t code;
    int32_t value;
} event_t;

static char command_buffer[COMMAND_BUFFER_SIZE];
static size_t command_length = 0;
static bool command_overflow = false;
static bool previous_was_cr = false;

static event_t event_log[EVENT_LOG_CAPACITY];
static size_t log_head = 0;
static size_t log_count = 0;
static uint32_t next_sequence = 1;

static uint32_t commands_received = 0;
static bool simulated_fault = false;

static bool fpga_config_ok = false;
static bool fpga_spi_ready = false;
static bool pio_capture_ready = false;
static PIO capture_pio = pio0;
static const uint capture_sm = 0;
static uint capture_offset = 0;

extern const uint8_t fpga_bitstream[];
extern const uint32_t fpga_bitstream_len;

static volatile uint32_t fpga_event_count = 0;
static bool event_test_active = false;
static uint32_t event_test_start_count = 0;
static uint64_t event_test_start_ms = 0;

static bool burst_test_active = false;
static uint32_t burst_start_count = 0;
static uint32_t burst_last_count = 0;
static uint64_t burst_start_ms = 0;
static uint64_t burst_last_change_ms = 0;

/* SPI readback helpers are defined after FPGA configuration code. */
static bool fpga_read_pulse_count(uint32_t *count);
static bool fpga_pio_capture_init(void);

/* Return uptime in milliseconds. */
static uint64_t get_uptime_ms(void)
{
    return to_us_since_boot(get_absolute_time()) / 1000ULL;
}

/* Add an event to the circular RAM log. */
static void record_event(event_code_t code, int32_t value)
{
    event_t *event = &event_log[log_head];

    event->sequence = next_sequence++;
    event->uptime_ms = get_uptime_ms();
    event->code = code;
    event->value = value;

    log_head = (log_head + 1) % EVENT_LOG_CAPACITY;

    if (log_count < EVENT_LOG_CAPACITY) {
        log_count++;
    }
}

static const char *event_name(event_code_t code)
{
    switch (code) {
        case EVENT_BOOT:              return "BOOT";
        case EVENT_HELP:              return "HELP";
        case EVENT_STATUS:            return "STATUS";
        case EVENT_UPTIME:            return "UPTIME";
        case EVENT_UNKNOWN_COMMAND:   return "UNKNOWN_CMD";
        case EVENT_INPUT_TOO_LONG:    return "INPUT_TOO_LONG";
        case EVENT_FAULT_SIMULATED:   return "FAULT_SIMULATED";
        case EVENT_FAULT_CLEARED:     return "FAULT_CLEARED";
        case EVENT_LOG_CLEARED:       return "LOG_CLEARED";
        case EVENT_LOG_READ:          return "LOG_READ";
        case EVENT_WATCHDOG_TEST:     return "WATCHDOG_TEST";
        default:                      return "UNKNOWN_EVENT";
    }
}

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  HELP           Show commands\n");
    printf("  STATUS         Show firmware status\n");
    printf("  UPTIME         Show uptime\n");
    printf("  LOG            Show recent events\n");
    printf("  CLEARLOG       Clear the event log\n");
    printf("  FAULT SIMULATE Simulate a fault flag\n");
    printf("  FAULT CLEAR    Clear the simulated fault\n");
    printf("  FAULT STATUS   Show simulated fault state\n");
    printf("  WATCHDOG STATUS - Show watchdog status\n");
    printf("  WATCHDOG TEST   - Test watchdog reset\n");
    printf("  EVENT COUNT - Show FPGA event count\n");
    printf("  EVENT TEST  - Run a 5-second FPGA event test\n");
    printf("  EVENT BURST - Generate and count 10,000 FPGA pulses\n");
    printf("  FPGA COUNT  - Read the FPGA internal pulse counter over SPI\n");
}

static void print_status(void)
{
    printf("\n--- Shrike Sentinel Status ---\n");
    printf("Firmware: Shrike Sentinel v0.2\n");
    printf("MCU: RP2040\n");
    printf("Status: RUNNING\n");
    printf("Uptime: %llu ms\n",
           (unsigned long long)get_uptime_ms());
    printf("Commands received: %lu\n",
           (unsigned long)commands_received);
    printf("Events stored: %lu/%u\n",
           (unsigned long)log_count,
           (unsigned)EVENT_LOG_CAPACITY);
    printf("Simulated fault: %s\n",
           simulated_fault ? "ACTIVE" : "CLEAR");
    printf("FPGA integration: %s\n",
           (fpga_config_ok && fpga_spi_ready) ? "READY" : "NOT READY");
}

static void print_event_log(void)
{
    printf("\n--- Recent Events ---\n");

    if (log_count == 0) {
        printf("No events recorded.\n");
        return;
    }

    size_t oldest =
        (log_head + EVENT_LOG_CAPACITY - log_count)
        % EVENT_LOG_CAPACITY;

    for (size_t i = 0; i < log_count; i++) {
        size_t index = (oldest + i) % EVENT_LOG_CAPACITY;
        const event_t *event = &event_log[index];

        printf("#%lu | %llu ms | %-16s | value=%ld\n",
               (unsigned long)event->sequence,
               (unsigned long long)event->uptime_ms,
               event_name(event->code),
               (long)event->value);
    }
}

static void process_command(char *line)
{
    /* Remove leading whitespace. */
    size_t start = 0;

    while (line[start] != '\0' &&
           isspace((unsigned char)line[start])) {
        start++;
    }

    if (start > 0) {
        memmove(line, line + start, strlen(line + start) + 1);
    }

    /* Remove trailing whitespace. */
    size_t length = strlen(line);

    while (length > 0 &&
           isspace((unsigned char)line[length - 1])) {
        line[--length] = '\0';
    }

    /* Commands are case-insensitive. */
    for (size_t i = 0; line[i] != '\0'; i++) {
        line[i] = (char)toupper((unsigned char)line[i]);
    }

    if (line[0] == '\0') {
        return;
    }

    commands_received++;

    if (strcmp(line, "HELP") == 0) {
        record_event(EVENT_HELP, 0);
        print_help();

    } else if (strcmp(line, "STATUS") == 0) {
        record_event(EVENT_STATUS, 0);
        print_status();

    } else if (strcmp(line, "UPTIME") == 0) {
        record_event(EVENT_UPTIME, 0);
        printf("\nUptime: %llu ms\n",
               (unsigned long long)get_uptime_ms());

    } else if (strcmp(line, "LOG") == 0) {
        record_event(EVENT_LOG_READ, 0);
        print_event_log();

    } else if (strcmp(line, "CLEARLOG") == 0) {
        log_head = 0;
        log_count = 0;
        record_event(EVENT_LOG_CLEARED, 0);
        printf("\nEvent log cleared.\n");

    } else if (strcmp(line, "FAULT SIMULATE") == 0) {
        simulated_fault = true;
        record_event(EVENT_FAULT_SIMULATED, 1);
        printf("\nSimulated fault is ACTIVE.\n");

    } else if (strcmp(line, "FAULT CLEAR") == 0) {
        simulated_fault = false;
        record_event(EVENT_FAULT_CLEARED, 0);
        printf("\nSimulated fault cleared.\n");

    } else if (strcmp(line, "FAULT STATUS") == 0) {
        printf("\nSimulated fault: %s\n",
               simulated_fault ? "ACTIVE" : "CLEAR");
    } else if (strcmp(line, "WATCHDOG STATUS") == 0) {
         printf("\nWatchdog: ACTIVE\n");
        printf("Timeout: 3000 ms\n");

    } else if (strcmp(line, "WATCHDOG TEST") == 0) {
       record_event(EVENT_WATCHDOG_TEST, 0);

        printf("\nWatchdog test starting.\n");
       printf("The MCU should reset in about 3 seconds.\n");
       stdio_flush();

    /* Deliberately stop feeding the watchdog. */
    while (true) {
        tight_loop_contents();
    }

    } else if (strcmp(line, "EVENT COUNT") == 0) {
    uint32_t count = fpga_event_count;

    printf("\nFPGA rising-edge count: %lu\n",
           (unsigned long)count);
    printf("Nominal event rate: 100 edges/second\n");

    } else if (strcmp(line, "FPGA COUNT") == 0) {
        uint32_t fpga_count = 0;

        if (!fpga_spi_ready) {
            printf("\nERROR: FPGA SPI readback is not ready.\n");
        } else if (fpga_read_pulse_count(&fpga_count)) {
            printf("\nFPGA internal pulse count: %lu\n",
                   (unsigned long)fpga_count);
        } else {
            printf("\nERROR: FPGA counter SPI read failed.\n");
        }

    } else if (strcmp(line, "EVENT TEST") == 0) {

    if (event_test_active) {
        printf("\nAn event test is already running.\n");
    } else {
        event_test_start_count = fpga_event_count;
        event_test_start_ms = get_uptime_ms();
        event_test_active = true;

        printf("\nFPGA event test started.\n");
        printf("Duration: 5 seconds\n");
        printf("Expected rate: 100 Hz\n");
        printf("Please wait...\n");
    }

    } else if (strcmp(line, "EVENT BURST") == 0) {
    if (!fpga_config_ok) {
        printf("\nERROR: FPGA configuration is not ready.\n");
    } else if (event_test_active || burst_test_active) {
        printf("\nAnother event test is already running.\n");
    } else {
        burst_start_count = fpga_event_count;
        burst_last_count = burst_start_count;
        burst_start_ms = get_uptime_ms();
        burst_last_change_ms = burst_start_ms;
        burst_test_active = true;

        printf("\nStarting 10,000-pulse FPGA burst test...\n");
        if (pio_capture_ready) {
            /* Remove stale capture data and arm a 40 ms quiet-time window. */
            pio_sm_clear_fifos(capture_pio, capture_sm);
            pio_sm_put_blocking(capture_pio, capture_sm,
                                PIO_CAPTURE_IDLE_LOOPS);
        } else {
            printf("WARNING: PIO reference capture is unavailable.\n");
        }
        stdio_flush();

        gpio_put(FPGA_START_GPIO, 1);
        sleep_ms(1);
        gpio_put(FPGA_START_GPIO, 0);
    }

    } else {
        record_event(EVENT_UNKNOWN_COMMAND, 0);
        printf("\nUnknown command: %s\n", line);
        printf("Type HELP for commands.\n");
    }
}

static void __no_inline_not_in_flash_func(fpga_event_irq_handler)(void)
{
    uint32_t events = gpio_get_irq_event_mask(FPGA_EVENT_GPIO);

    if (events != 0u) {
        /* Acknowledge the pending event as early as possible. */
        gpio_acknowledge_irq(FPGA_EVENT_GPIO, events);

        if (events & GPIO_IRQ_EDGE_RISE) {
            fpga_event_count++;
        }
    }
}

static void update_event_test(void)
{
    if (!event_test_active) {
        return;
    }

    uint64_t now_ms = get_uptime_ms();
    uint64_t elapsed_ms = now_ms - event_test_start_ms;

    if (elapsed_ms < EVENT_TEST_DURATION_MS) {
        return;
    }

    uint32_t end_count = fpga_event_count;
    uint32_t observed = end_count - event_test_start_count;

    uint32_t expected = (uint32_t)(
        ((uint64_t)EVENT_NOMINAL_HZ * elapsed_ms + 500) / 1000
    );

    uint32_t measured_hz = (uint32_t)(
        ((uint64_t)observed * 1000 + elapsed_ms / 2) / elapsed_ms
    );

    uint32_t tolerance = expected / 100;  // ±1%

    bool passed =
    (observed >= expected - tolerance) &&
    (observed <= expected + tolerance);

    int64_t difference = (int64_t)observed - expected;

    event_test_active = false;

    printf("\n=== FPGA EVENT TEST RESULTS ===\n");
    printf("Elapsed:          %llu ms\n",
           (unsigned long long)elapsed_ms);
    printf("Expected events:  %lu\n", (unsigned long)expected);
    printf("Observed events:  %lu\n", (unsigned long)observed);
    printf("Difference:       %+lld\n",
           (long long)difference);
    printf("Measured rate:    %lu Hz\n",
           (unsigned long)measured_hz);
    printf("Result:           %s\n",
           passed ? "PASS" : "FAIL");
    printf("===============================\n");

    stdio_flush();
}

static void update_event_burst_test(void)
{
    if (!burst_test_active) {
        return;
    }

    uint64_t now_ms = get_uptime_ms();
    uint32_t current_count = fpga_event_count;

    if (current_count != burst_last_count) {
        burst_last_count = current_count;
        burst_last_change_ms = now_ms;
    }

    uint32_t observed = current_count - burst_start_count;
    uint64_t elapsed_ms = now_ms - burst_start_ms;

    bool finished =
        (observed > 0) &&
        ((now_ms - burst_last_change_ms) >= BURST_QUIET_MS);

    bool timed_out = elapsed_ms >= BURST_TIMEOUT_MS;

    if (!finished && !timed_out) {
        return;
    }

    burst_test_active = false;

    int32_t difference =
        (int32_t)observed - (int32_t)BURST_EXPECTED_PULSES;

    uint32_t fpga_generated = 0;
    bool counter_read_ok = fpga_spi_ready &&
                           fpga_read_pulse_count(&fpga_generated);

    uint32_t pio_raw = 0;
    uint32_t pio_observed = 0;
    bool pio_read_ok = pio_capture_ready &&
                       !pio_sm_is_rx_fifo_empty(capture_pio, capture_sm);
    if (pio_read_ok) {
        pio_raw = pio_sm_get(capture_pio, capture_sm);
        /* PIO X starts at 0xFFFFFFFF and decrements once per rising edge. */
        pio_observed = ~pio_raw;
    }

    bool generator_passed = counter_read_ok &&
                            fpga_generated == BURST_EXPECTED_PULSES;
    bool pio_input_passed = counter_read_ok && pio_read_ok &&
                            pio_observed == fpga_generated;
    bool irq_capture_passed = pio_read_ok && observed == pio_observed;
    bool passed = finished && !timed_out && generator_passed &&
                  pio_input_passed && irq_capture_passed;

    printf("\n=== FPGA BURST TEST RESULTS ===\n");
    printf("Elapsed:             %llu ms\n",
           (unsigned long long)elapsed_ms);
    printf("Expected events:     %u\n",
           (unsigned)BURST_EXPECTED_PULSES);
    printf("RP2040 IRQ observed: %lu\n",
           (unsigned long)observed);
    printf("IRQ vs expected:     %+ld\n",
           (long)difference);

    if (counter_read_ok) {
        printf("FPGA internal count: %lu\n",
               (unsigned long)fpga_generated);
        printf("Generator check:     %s\n",
               generator_passed ? "PASS" : "FAIL");
    } else {
        printf("FPGA internal count: READ FAILED\n");
        printf("Generator check:     FAIL\n");
    }

    if (pio_read_ok) {
        printf("RP2040 PIO observed: %lu\n",
               (unsigned long)pio_observed);
        printf("PIO vs FPGA check:   %s\n",
               pio_input_passed ? "PASS" : "FAIL");
        printf("IRQ vs PIO check:    %s\n",
               irq_capture_passed ? "PASS" : "FAIL");
        if (counter_read_ok && fpga_generated >= observed) {
            printf("IRQ gap vs FPGA:     %lu\n",
                   (unsigned long)(fpga_generated - observed));
        } else if (counter_read_ok) {
            printf("IRQ excess vs FPGA:  %lu\n",
                   (unsigned long)(observed - fpga_generated));
        }
    } else {
        printf("RP2040 PIO observed: NOT READY\n");
        printf("PIO vs FPGA check:   FAIL\n");
        printf("IRQ vs PIO check:    NOT AVAILABLE\n");
    }

    printf("Result:              %s\n",
           passed ? "PASS" : "FAIL");

    if (timed_out) {
        printf("Reason:              Timeout waiting for events\n");
    } else if (!finished) {
        printf("Reason:              No events received\n");
    } else if (!counter_read_ok) {
        printf("Reason:              SPI counter readback failed\n");
    } else if (!pio_read_ok) {
        printf("Reason:              PIO capture result not ready\n");
    }

    printf("==================================\n");
    stdio_flush();
}

static void poll_console(void)
{
    int ch = getchar_timeout_us(0);

    if (ch < 0) {
        return;
    }

    if (ch == '\r' || ch == '\n') {
        if (ch == '\n' && previous_was_cr) {
            previous_was_cr = false;
            return;
        }

        previous_was_cr = (ch == '\r');
        putchar('\n');

        if (command_overflow) {
            record_event(EVENT_INPUT_TOO_LONG,
                         (int32_t)command_length);

            printf("ERROR: command too long; maximum %d characters.\n",
                   COMMAND_BUFFER_SIZE - 1);
        } else {
            command_buffer[command_length] = '\0';
            process_command(command_buffer);
        }

        command_length = 0;
        command_overflow = false;

        printf("\nsentinel> ");
        stdio_flush();
        return;
    }

    previous_was_cr = false;

    /* Handle Backspace and Delete. */
    if (ch == '\b' || ch == 127) {
        if (command_length > 0 && !command_overflow) {
            command_length--;
            printf("\b \b");
            stdio_flush();
        }
        return;
    }

    /* Accept printable ASCII. */
    if (ch >= 32 && ch <= 126) {
        if (command_length < COMMAND_BUFFER_SIZE - 1 &&
            !command_overflow) {
            command_buffer[command_length++] = (char)ch;
            putchar(ch);
            stdio_flush();
        } else {
            command_overflow = true;
        }
    }
}

static void fpga_event_input_init(void)
{
    gpio_init(FPGA_EVENT_GPIO);
    gpio_set_dir(FPGA_EVENT_GPIO, GPIO_IN);
    gpio_disable_pulls(FPGA_EVENT_GPIO);

    /* Disable edge interrupts while configuring the handler. */
    gpio_set_irq_enabled(
        FPGA_EVENT_GPIO,
        GPIO_IRQ_EDGE_RISE,
        false
    );

    /* Clear any pending events left over from initialization. */
    gpio_acknowledge_irq(
        FPGA_EVENT_GPIO,
        GPIO_IRQ_EDGE_RISE |
        GPIO_IRQ_EDGE_FALL |
        GPIO_IRQ_LEVEL_HIGH |
        GPIO_IRQ_LEVEL_LOW
    );

    /* Install a direct handler for the GPIO interrupt bank. */
    irq_set_enabled(IO_IRQ_BANK0, false);
    irq_set_exclusive_handler(
        IO_IRQ_BANK0,
        fpga_event_irq_handler
    );

    gpio_set_irq_enabled(
        FPGA_EVENT_GPIO,
        GPIO_IRQ_EDGE_RISE,
        true
    );

    irq_set_priority(IO_IRQ_BANK0, 0x00);

    irq_set_priority(USBCTRL_IRQ, 0xC0);

    irq_set_enabled(IO_IRQ_BANK0, true);
}

static bool fpga_configure(void)
{
    spi_init(CFG_SPI, 1600 * 1000);

    gpio_set_function(PIN_CFG_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CFG_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CFG_SCK,  GPIO_FUNC_SPI);

    gpio_init(PIN_CFG_SS);
    gpio_set_dir(PIN_CFG_SS, GPIO_OUT);

    gpio_init(PIN_PWR);
    gpio_set_dir(PIN_PWR, GPIO_OUT);

    gpio_init(PIN_EN);
    gpio_set_dir(PIN_EN, GPIO_OUT);

    /* Begin the FPGA power-up sequence. */
    gpio_put(PIN_PWR, 0);
    gpio_put(PIN_EN, 0);
    watchdog_update();
    sleep_ms(500);

    gpio_put(PIN_CFG_SS, 0);
    sleep_ms(100);

    gpio_put(PIN_EN, 1);
    gpio_put(PIN_PWR, 1);
    sleep_ms(100);

    gpio_put(PIN_CFG_SS, 1);
    sleep_ms(2);

    /* Send the complete FPGA bitstream in one SPI frame. */
    gpio_put(PIN_CFG_SS, 0);
    watchdog_update();

    int written = spi_write_blocking(
        CFG_SPI,
        fpga_bitstream,
        (size_t)fpga_bitstream_len
    );

    gpio_put(PIN_CFG_SS, 1);
    watchdog_update();
    sleep_ms(100);

    spi_deinit(CFG_SPI);

    /* Release the SPI pins from their peripheral function. */
    for (int pin = PIN_CFG_MISO; pin <= PIN_CFG_MOSI; pin++) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
    }

    return written == (int)fpga_bitstream_len;
}

/*
 * Re-use SPI0 after the initial FPGA bitstream transfer has completed.
 * GPIO1 is manual chip-select (SIO); GPIO0/2/3 remain MISO/SCK/MOSI SPI pins.
 */
static bool fpga_spi_target_init(void)
{
    spi_init(CFG_SPI, FPGA_SPI_BAUD_HZ);
    spi_set_format(CFG_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_CFG_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CFG_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CFG_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CFG_SS);
    gpio_set_dir(PIN_CFG_SS, GPIO_OUT);
    gpio_put(PIN_CFG_SS, 1);
    sleep_us(50);

    return true;
}

/* Perform one 8-bit SPI transaction with a separate CS frame per byte. */
static bool fpga_spi_transfer_byte(uint8_t tx, uint8_t *rx)
{
    if (!fpga_spi_ready || rx == NULL) {
        return false;
    }

    gpio_put(PIN_CFG_SS, 0);
    sleep_us(5);
    int transferred = spi_write_read_blocking(CFG_SPI, &tx, rx, 1);
    gpio_put(PIN_CFG_SS, 1);
    sleep_us(10);

    return transferred == 1;
}

/*
 * FPGA read protocol: 0xA0 snapshots pulses_sent. The target shifts the
 * currently selected byte during each transaction, so A1/A2/A3 select the
 * next byte while returning the previous byte. The final dummy transfer
 * clocks out byte 3. Bytes are returned least-significant first.
 */
static bool fpga_read_pulse_count(uint32_t *count)
{
    if (!fpga_spi_ready || count == NULL) {
        return false;
    }

    uint8_t ignored = 0;
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;

    if (!fpga_spi_transfer_byte(FPGA_CMD_SNAPSHOT, &ignored) ||
        !fpga_spi_transfer_byte(FPGA_CMD_READ_B0, &b0) ||
        !fpga_spi_transfer_byte(FPGA_CMD_READ_B1, &b1) ||
        !fpga_spi_transfer_byte(FPGA_CMD_READ_B2, &b2) ||
        !fpga_spi_transfer_byte(0x00u, &b3)) {
        return false;
    }

    *count = ((uint32_t)b3 << 24) |
             ((uint32_t)b2 << 16) |
             ((uint32_t)b1 << 8)  |
             (uint32_t)b0;
    return true;
}

static bool fpga_pio_capture_init(void)
{
    if (!pio_can_add_program(capture_pio,
                             &sentinel_pulse_counter_program)) {
        return false;
    }

    capture_offset = pio_add_program(capture_pio,
                                     &sentinel_pulse_counter_program);

    /* Keep the existing SIO edge IRQ and add an independent PIO observer. */
    pio_gpio_init(capture_pio, FPGA_EVENT_GPIO);

    pio_sm_config config =
        sentinel_pulse_counter_program_get_default_config(capture_offset);
    sm_config_set_jmp_pin(&config, FPGA_EVENT_GPIO);
    sm_config_set_clkdiv(&config, 1.0f);

    pio_sm_init(capture_pio, capture_sm, capture_offset, &config);
    pio_sm_clear_fifos(capture_pio, capture_sm);
    pio_sm_set_enabled(capture_pio, capture_sm, true);
    return true;
}

static void fpga_start_output_init(void)
{
    gpio_init(FPGA_START_GPIO);
    gpio_set_dir(FPGA_START_GPIO, GPIO_OUT);
    gpio_put(FPGA_START_GPIO, 0);
}

int main(void)
{
    /* Check the previous reset cause before enabling the watchdog. */
    bool watchdog_reset = watchdog_caused_reboot();

    watchdog_enable(3000, true);

    stdio_init_all();

    while (!stdio_usb_connected()) {
        watchdog_update();
        sleep_ms(10);
    }

    record_event(EVENT_BOOT, watchdog_reset ? 1 : 0);

    printf("Configuring FPGA...\n");
    stdio_flush();

    fpga_start_output_init();
    fpga_config_ok = fpga_configure();

    if (fpga_config_ok) {
        printf("FPGA bitstream transmitted successfully (%lu bytes).\n",
            (unsigned long)fpga_bitstream_len);

        fpga_spi_ready = fpga_spi_target_init();
        if (fpga_spi_ready) {
            printf("FPGA SPI counter readback enabled at %u Hz.\n",
                   (unsigned)FPGA_SPI_BAUD_HZ);
        } else {
            printf("ERROR: FPGA SPI counter readback initialisation failed.\n");
        }

        fpga_event_input_init();
        printf("FPGA event IRQ counter enabled on GPIO14.\n");

        pio_capture_ready = fpga_pio_capture_init();
        printf("PIO reference capture: %s\n",
               pio_capture_ready ? "READY" : "FAILED");
    } else {
        printf("ERROR: FPGA bitstream transmission failed.\n");
    }

    stdio_flush();

    printf("\n=== Shrike Sentinel v0.3 ===\n");

    if (watchdog_reset) {
        printf("Previous boot: WATCHDOG RESET\n");
    } else {
        printf("Previous boot: NORMAL/OTHER RESET\n");
    }

    printf("Diagnostic console ready.\n");
    printf("Type HELP for commands.\n");
    printf("\nsentinel> ");
    stdio_flush();

    while (true) {
        poll_console();
        update_event_test();
        update_event_burst_test();
        watchdog_update();
        tight_loop_contents();

    }

    return 0;
}