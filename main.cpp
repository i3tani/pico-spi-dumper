#include <cstdint>
#include <cstdio>
#include <pico/stdlib.h>
#include <pico/stdio_usb.h>
#include "dumper.hpp"
#include "analyzer.hpp"

static constexpr uint8_t CMD_DUMP    = 0x3A;
static constexpr uint8_t CMD_ANALYZE = 0x3B;

static uint8_t wait_for_command() {
    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }
        return static_cast<uint8_t>(ch);
    }
}

int main() {
    stdio_init_all();

    // Wait until the PC opens the serial port (avoids lost boot messages).
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(500);

    printf("Ready. Send command byte:\n");
    printf("  0x%02X - firmware dump\n", CMD_DUMP);
    printf("  0x%02X - logic analyzer\n", CMD_ANALYZE);

    while (true) {
        uint8_t cmd = wait_for_command();
        
        switch (cmd) {
        case CMD_DUMP:
            printf("MODE: firmware dumper\n");
            FwDumper().run();
            break;
        case CMD_ANALYZE:
            printf("MODE: logic analyzer\n");
            LogicAnalyzer().run();
            break;
        default:
            printf("Unknown command: 0x%02X\n", cmd);
            break;
        }

        printf("Waiting for command...\n");
    }

    return 0;
}
