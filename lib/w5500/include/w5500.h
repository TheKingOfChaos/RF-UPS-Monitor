#ifndef W5500_H
#define W5500_H

#include <Arduino.h>
#include <SPI.h>

// W5500 SPI Operation modes
#define W5500_READ        0x00
#define W5500_WRITE       0x01

// W5500 Register Block Selection
#define W5500_BLOCK_COMMON    0x00    // Common Register
#define W5500_BLOCK_SOCKET    0x01    // Socket Register
#define W5500_BLOCK_TX_BUFFER 0x02    // Socket TX Buffer
#define W5500_BLOCK_RX_BUFFER 0x03    // Socket RX Buffer

// Common Register Addresses
#define W5500_MR        0x0000    // Mode Register
#define W5500_IR        0x0015    // Interrupt Register
#define W5500_IMR       0x0016    // Interrupt Mask Register
#define W5500_SIR       0x0017    // Socket Interrupt Register
#define W5500_SIMR      0x0018    // Socket Interrupt Mask Register
#define W5500_CHIP_ID   0x0039    // Chip Identification Register (0x04)

// Interrupt Types
#define W5500_IR_CONFLICT    0x80 // IP Conflict
#define W5500_IR_UNREACH     0x40 // Destination Unreachable
#define W5500_IR_PPPOE       0x20 // PPPoE Connection Close
#define W5500_IR_MP          0x10 // Magic Packet Received

// Pin Configuration (as per .clinerules)
#define W5500_PIN_MISO  16
#define W5500_PIN_CS    17
#define W5500_PIN_SCK   18
#define W5500_PIN_MOSI  19
#define W5500_PIN_RST   20
#define W5500_PIN_INT   21

class W5500 {
public:
    /**
     * @brief Initialize W5500 driver
     * @return true if initialization successful, false otherwise
     */
    bool begin();

    /**
     * @brief Reset the W5500 chip
     */
    void reset();

    /**
     * @brief Read from a W5500 register
     * @param address Register address
     * @param block Register block (common, socket, tx buffer, rx buffer)
     * @param socket Socket number (0-7) for socket-specific operations
     * @return Register value
     */
    uint8_t readRegister(uint16_t address, uint8_t block = W5500_BLOCK_COMMON, uint8_t socket = 0);

    /**
     * @brief Write to a W5500 register
     * @param address Register address
     * @param value Value to write
     * @param block Register block (common, socket, tx buffer, rx buffer)
     * @param socket Socket number (0-7) for socket-specific operations
     */
    void writeRegister(uint16_t address, uint8_t value, uint8_t block = W5500_BLOCK_COMMON, uint8_t socket = 0);

    /**
     * @brief Read the chip ID to verify W5500 presence
     * @return Chip ID value (should be 0x04 for W5500)
     */
    uint8_t readChipID();

    /**
     * @brief Enable specific interrupt type
     * @param interruptType Type of interrupt to enable
     */
    void enableInterrupt(uint8_t interruptType);

    /**
     * @brief Clear pending interrupts
     */
    void clearInterrupt();

private:
    /**
     * @brief Initialize SPI interface
     */
    void initSPI();

    /**
     * @brief Select W5500 chip (CS low)
     */
    inline void select() { digitalWrite(W5500_PIN_CS, LOW); }

    /**
     * @brief Deselect W5500 chip (CS high)
     */
    inline void deselect() { digitalWrite(W5500_PIN_CS, HIGH); }

    /**
     * @brief Transfer control byte (address, RW mode, block)
     * @param address Register address
     * @param readWrite Read/Write mode
     * @param block Register block
     * @param socket Socket number
     */
    void transferControlByte(uint16_t address, uint8_t readWrite, uint8_t block, uint8_t socket);
};

#endif // W5500_H
