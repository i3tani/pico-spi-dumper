#include <cstdio>
#include <pico/stdlib.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <pico/stdio_usb.h>
#include "dumper.hpp"
constexpr size_t CHUNK_SIZE = 4096;
static uint8_t buffer[CHUNK_SIZE];
static constexpr uint8_t CMD_JEDEC = 0x9F;
static constexpr uint8_t CMD_READ  = 0x03;
static constexpr uint8_t CMD_READ4b  = 0x13;
// 0x03 + ADDR eg read one byte from addr a1 MOSI: 03 00 00 A1 00
//MISO: ?? ?? ?? ?? 5A
struct __attribute__((packed)) DumpHeader {
    char magic[4];
    uint8_t version;
    uint32_t chunkSize;
};

DumpHeader ender = {
    .magic = {'D', 'I', 'P', 'S'},
    .version = 1,
    .chunkSize = CHUNK_SIZE
};

DumpHeader hdr = {
    .magic = {'S', 'P', 'I', 'D'},
    .version = 1,
    .chunkSize = CHUNK_SIZE
};
struct JedecId {
    uint8_t manufacturer;
    uint8_t memoryType;
    uint8_t capacity;

    uint32_t sizeBytes() const {
        if (capacity >= 32)
            return 0;
        return 1UL << capacity;
    }

    bool requires4ByteAddressing() const {
        return sizeBytes() > (16u * 1024u * 1024u);
    }
    bool valid() const {
        return !(manufacturer == 0xff && memoryType == 0xff && capacity == 0xff) && !(manufacturer == 0x00 && memoryType == 0x00 && capacity == 0x00);
    }
};
class SPIFlash {
private:
    spi_inst_t* spi_port = spi0;
    uint pin_miso = 16;
    uint pin_cs = 17;
    uint pin_sck = 18;
    uint pin_mosi = 19;
    uint baudrate = 10000000;

public:
    bool fourByte = false;
    SPIFlash();
    ~SPIFlash();
    void select();
    void deselect();
    JedecId readID();
    void readDataFromAddr(uint32_t addr, uint8_t* buffer, size_t length);
};

SPIFlash::SPIFlash() {
    spi_init(spi_port, baudrate);
    gpio_set_function(pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(pin_mosi, GPIO_FUNC_SPI);
    gpio_init(pin_cs);
    gpio_set_dir(pin_cs, GPIO_OUT);
    deselect();
}

SPIFlash::~SPIFlash() {
    spi_deinit(spi_port);
}

void SPIFlash::deselect() {
    sleep_us(5);
    gpio_put(pin_cs, true);
    sleep_us(5);
}

void SPIFlash::select() {
    sleep_us(5);
    gpio_put(pin_cs, false);
    sleep_us(5);
}

JedecId SPIFlash::readID() {
    select();
    printf("Initializing JEDEC ID\n");
    uint8_t response[3] = {0};
    spi_write_blocking(spi_port, &CMD_JEDEC, 1);
    spi_read_blocking(spi_port, 0x00, response, 3);
    deselect();
    return {
        response[0],
        response[1],
        response[2]
    };
}

void SPIFlash::readDataFromAddr(uint32_t addr, uint8_t* buffer, size_t length){
    uint8_t cmd[5];
    size_t cmdLen;
    if (fourByte) {
        cmd[0] = CMD_READ4b;
        cmd[1] = (addr >> 24) & 0xFF;
        cmd[2] = (addr >> 16) & 0xFF;
        cmd[3] = (addr >> 8)  & 0xFF;
        cmd[4] =  addr        & 0xFF;
        cmdLen = 5;
    } else {
        cmd[0] = CMD_READ;
        cmd[1] = (addr >> 16) & 0xFF;
        cmd[2] = (addr >> 8)  & 0xFF;
        cmd[3] =  addr        & 0xFF;
        cmdLen = 4;
    }
    select();
    spi_write_blocking(spi_port, cmd, cmdLen);
    spi_read_blocking(spi_port, 0x00, buffer, length);   // ← restore this
    deselect();                                          // ← and this
}

void FwDumper::run() {
    printf("SPI_DUMP_STARTING\n");
    SPIFlash flash;
    JedecId id = flash.readID();
    if (!id.valid()) {
        printf("NO FLASH DETECTED\n");
        return;
    }
    flash.fourByte = id.requires4ByteAddressing();
    printf("JEDEC: %02X %02X %02X\n",id.manufacturer,id.memoryType,id.capacity);
    printf("Size: %u bytes\n",id.sizeBytes());
    printf("Address mode: %s\n",id.requires4ByteAddressing() ? "4-byte" : "3-byte");
    printf("DUMPING START\n");
    uint32_t totalSize = id.sizeBytes();
    fwrite(&hdr, sizeof(hdr), 1, stdout);
    fflush(stdout);
    for(uint32_t addr = 0; addr < totalSize; addr += CHUNK_SIZE){
        size_t chunkSize = (addr + CHUNK_SIZE <= totalSize) ? CHUNK_SIZE : (totalSize - addr);
        flash.readDataFromAddr(addr, buffer, chunkSize);
        fwrite(buffer, 1, chunkSize, stdout);      
    }
    fwrite(&ender, sizeof(ender), 1, stdout);
    fflush(stdout);
}


