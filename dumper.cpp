#include <cstdio>
#include <pico/stdlib.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include "dumper.hpp"
constexpr size_t CHUNK_SIZE = 4096;
uint8_t CMD_JEDEC = 0X9F;
uint8_t CMD_READ = 0X03; // 0x03 + ADDR eg read one byte from addr a1 MOSI: 03 00 00 A1 00
                                                                    //MISO: ?? ?? ?? ?? 5A
struct JedecId {
    uint8_t manufacturer;
    uint8_t memoryType;
    uint8_t capacity;

    uint32_t sizeBytes() const {
        // Capacity byte encoding used by most SPI NOR chips:
        // 0x17 = 2^23 bytes = 8 MiB
        // 0x18 = 2^24 bytes = 16 MiB
        // 0x19 = 2^25 bytes = 32 MiB
        return 1u << capacity;
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
    SPIFlash();
    ~SPIFlash();
    void select();
    void deselect();
    JedecId readID();
    void readDataFromAddr(uint32_t addr, uint8_t* buffer, size_t lenght);
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
    uint8_t cmd[4];
    cmd[0]=CMD_READ;
    cmd[1]=(addr >> 16) & 0xFF;
    cmd[2]=(addr >> 8) & 0xFF;
    cmd[3]= addr & 0xFF;
    select();
    spi_write_blocking(spi_port, cmd, sizeof(cmd));
    spi_read_blocking(spi_port,0x00,buffer,length);
    deselect();

    /*Intended Usage (Mostly for debugging)
            uint8_t buf[256];
            flash.readData(0, buf, sizeof(buf));
            dumpBuffer(buf, sizeof(buf)); still have to implement this
    */
}

void FwDumper::run() {
    printf("SPI_DUMP_STARTING\n");
    SPIFlash flash;
    JedecId id = flash.readID();
    if (!id.valid()) {
        printf("NO FLASH DETECTED\n");
        return;
    }
    printf("JEDEC: %02X %02X %02X\n",id.manufacturer,id.memoryType,id.capacity);
    printf("Size: %u bytes\n",id.sizeBytes());
    printf("Address mode: %s\n",id.requires4ByteAddressing() ? "4-byte" : "3-byte");
}


