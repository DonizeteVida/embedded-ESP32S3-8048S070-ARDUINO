#pragma once
#include <Arduino.h>
namespace fake { inline unsigned spi_begins=0,spi_ends=0; inline std::array<int,4> spi_pins{}; }
struct SPIClass {
    void begin(int sck,int miso,int mosi,int cs) { ++fake::spi_begins; fake::spi_pins={sck,miso,mosi,cs}; }
    void end() { ++fake::spi_ends; }
};
inline SPIClass SPI;
