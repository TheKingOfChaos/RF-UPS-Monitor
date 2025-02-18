#include "../include/w5500.h"

bool W5500::begin() {
    // Initialize pins
    pinMode(W5500_PIN_MISO, INPUT);
    pinMode(W5500_PIN_CS, OUTPUT);
    pinMode(W5500_PIN_SCK, OUTPUT);
    pinMode(W5500_PIN_MOSI, OUTPUT);
    pinMode(W5500_PIN_RST, OUTPUT);
    pinMode(W5500_PIN_INT, INPUT);

    // Set initial pin states
    digitalWrite(W5500_PIN_CS, HIGH);  // Deselect chip
    digitalWrite(W5500_PIN_RST, HIGH); // Release reset

    // Initialize SPI
    initSPI();

    // Reset the chip
    reset();

    // Verify chip presence by reading chip ID
    uint8_t chipId = readChipID();
    return chipId == 0x04; // W5500 chip ID should be 0x04
}

void W5500::reset() {
    // Hardware reset sequence
    digitalWrite(W5500_PIN_RST, LOW);
    delay(1);  // Hold reset for 1ms
    digitalWrite(W5500_PIN_RST, HIGH);
    delay(5);  // Wait for chip to stabilize
}

void W5500::initSPI() {
    SPI.begin();
    // W5500 supports up to 80MHz SPI clock
    // But we'll use a more conservative speed for reliability
    SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
}

uint8_t W5500::readRegister(uint16_t address, uint8_t block, uint8_t socket) {
    uint8_t value;
    
    select();
    
    // Send address and control byte
    transferControlByte(address, W5500_READ, block, socket);
    
    // Read data
    value = SPI.transfer(0x00);
    
    deselect();
    
    return value;
}

void W5500::writeRegister(uint16_t address, uint8_t value, uint8_t block, uint8_t socket) {
    select();
    
    // Send address and control byte
    transferControlByte(address, W5500_WRITE, block, socket);
    
    // Write data
    SPI.transfer(value);
    
    deselect();
}

uint8_t W5500::readChipID() {
    return readRegister(W5500_CHIP_ID);
}

void W5500::enableInterrupt(uint8_t interruptType) {
    // Read current interrupt mask
    uint8_t currentMask = readRegister(W5500_IMR);
    
    // Enable the specified interrupt type
    currentMask |= interruptType;
    
    // Write back the updated mask
    writeRegister(W5500_IMR, currentMask);
    
    // Verify the interrupt was enabled
    uint8_t verifyMask = readRegister(W5500_IMR);
    if ((verifyMask & interruptType) != interruptType) {
        // If verification fails, try one more time
        writeRegister(W5500_IMR, currentMask);
    }
}

void W5500::clearInterrupt() {
    // Read current interrupt status
    uint8_t currentInt = readRegister(W5500_IR);
    
    // Clear all pending interrupts by writing 1s to the IR
    // Writing 1 to a bit position clears that interrupt
    writeRegister(W5500_IR, currentInt);
    
    // Also clear any socket interrupts
    uint8_t socketInt = readRegister(W5500_SIR);
    if (socketInt) {
        writeRegister(W5500_SIR, socketInt);
    }
}

void W5500::transferControlByte(uint16_t address, uint8_t readWrite, uint8_t block, uint8_t socket) {
    // W5500 control phase:
    // 1. Address phase (16-bit address, MSB first)
    SPI.transfer(address >> 8);   // Address high byte
    SPI.transfer(address & 0xFF); // Address low byte
    
    // 2. Control phase (BSB[7:3] RWB[2] OM[1:0])
    // BSB: Block Select Bits - Combination of block and socket
    // RWB: Read/Write Bit
    // OM: Operation Mode - Fixed to 00 for 1 byte operation
    uint8_t controlByte = (block << 3) | (socket << 5) | (readWrite << 2);
    SPI.transfer(controlByte);
}
