#include "test_common.h"
#include "../lib/w5500/include/w5500.h"

// Global instance of W5500 driver - other test files will reference this
W5500 eth;

// Test SPI initialization
void test_spi_initialization() {
    // Initialize W5500 driver
    bool initResult = eth.begin();
    TEST_ASSERT_TRUE(initResult);

    // Test CS pin control (GPIO17)
    digitalWrite(W5500_PIN_CS, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_CS));
    digitalWrite(W5500_PIN_CS, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_CS));

    // Test RST pin control (GPIO20)
    digitalWrite(W5500_PIN_RST, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_RST));
    digitalWrite(W5500_PIN_RST, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_RST));

    // Test MOSI pin control (GPIO19)
    digitalWrite(W5500_PIN_MOSI, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_MOSI));
    digitalWrite(W5500_PIN_MOSI, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_MOSI));

    // Test SCK pin control (GPIO18)
    digitalWrite(W5500_PIN_SCK, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_SCK));
    digitalWrite(W5500_PIN_SCK, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_SCK));

    // Verify MISO pin can be read (GPIO16)
    uint8_t misoValue = digitalRead(W5500_PIN_MISO);
    TEST_ASSERT_TRUE(misoValue == HIGH || misoValue == LOW);

    // Verify INT pin can be read (GPIO21)
    uint8_t intValue = digitalRead(W5500_PIN_INT);
    TEST_ASSERT_TRUE(intValue == HIGH || intValue == LOW);

    // Verify chip ID
    uint8_t chipId = eth.readChipID();
    TEST_ASSERT_EQUAL_HEX8(0x04, chipId);
}

// Test basic read/write operations
void test_basic_read_write() {
    // Test writing to and reading from the Mode Register (MR)
    uint8_t testValue = 0x80; // Reset bit
    eth.writeRegister(W5500_MR, testValue);
    uint8_t readValue = eth.readRegister(W5500_MR);
    TEST_ASSERT_EQUAL_HEX8(testValue, readValue);
}

// Test interrupt handling
void test_interrupt_handling() {
    // Initialize W5500 if not already initialized
    if (!eth.begin()) {
        TEST_FAIL_MESSAGE("Failed to initialize W5500");
        return;
    }

    // Clear any pending interrupts first
    eth.clearInterrupt();
    
    // Verify INT pin is HIGH when no interrupts are pending
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_INT));
    
    // Enable IP conflict interrupt
    eth.enableInterrupt(W5500_IR_CONFLICT);
    
    // Verify interrupt mask was set
    uint8_t imr = eth.readRegister(W5500_IMR);
    TEST_ASSERT_BITS_HIGH(W5500_IR_CONFLICT, imr);
    
    // Simulate an IP conflict by writing to IR
    eth.writeRegister(W5500_IR, W5500_IR_CONFLICT);
    
    // Verify INT pin goes LOW
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_INT));
    
    // Clear the interrupt
    eth.clearInterrupt();
    
    // Verify INT pin goes back HIGH
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_INT));
    
    // Verify interrupt register was cleared
    uint8_t ir = eth.readRegister(W5500_IR);
    TEST_ASSERT_BITS_LOW(W5500_IR_CONFLICT, ir);
    
    // Test multiple interrupts
    eth.enableInterrupt(W5500_IR_UNREACH | W5500_IR_PPPOE);
    imr = eth.readRegister(W5500_IMR);
    TEST_ASSERT_BITS_HIGH(W5500_IR_UNREACH | W5500_IR_PPPOE, imr);
    
    // Simulate multiple interrupts
    eth.writeRegister(W5500_IR, W5500_IR_UNREACH | W5500_IR_PPPOE);
    
    // Verify INT pin is LOW
    TEST_ASSERT_EQUAL(LOW, digitalRead(W5500_PIN_INT));
    
    // Clear interrupts
    eth.clearInterrupt();
    
    // Verify INT pin is HIGH and interrupts are cleared
    TEST_ASSERT_EQUAL(HIGH, digitalRead(W5500_PIN_INT));
    ir = eth.readRegister(W5500_IR);
    TEST_ASSERT_BITS_LOW(W5500_IR_UNREACH | W5500_IR_PPPOE, ir);
}

// Test register access
void test_register_access() {
    // Test common register access
    uint8_t commonTestValue = 0xAA;
    eth.writeRegister(W5500_MR, commonTestValue, W5500_BLOCK_COMMON);
    uint8_t commonReadValue = eth.readRegister(W5500_MR, W5500_BLOCK_COMMON);
    TEST_ASSERT_EQUAL_HEX8(commonTestValue, commonReadValue);

    // Test socket register access (using socket 0)
    uint8_t socketTestValue = 0x55;
    uint16_t socketRegAddr = 0x0000; // Socket 0 mode register
    eth.writeRegister(socketRegAddr, socketTestValue, W5500_BLOCK_SOCKET, 0);
    uint8_t socketReadValue = eth.readRegister(socketRegAddr, W5500_BLOCK_SOCKET, 0);
    TEST_ASSERT_EQUAL_HEX8(socketTestValue, socketReadValue);

    // Test TX buffer access (using socket 0)
    uint8_t txTestValue = 0xCC;
    uint16_t txBufferAddr = 0x0000; // Start of TX buffer
    eth.writeRegister(txBufferAddr, txTestValue, W5500_BLOCK_TX_BUFFER, 0);
    uint8_t txReadValue = eth.readRegister(txBufferAddr, W5500_BLOCK_TX_BUFFER, 0);
    TEST_ASSERT_EQUAL_HEX8(txTestValue, txReadValue);

    // Test RX buffer access (using socket 0)
    uint8_t rxTestValue = 0x33;
    uint16_t rxBufferAddr = 0x0000; // Start of RX buffer
    eth.writeRegister(rxBufferAddr, rxTestValue, W5500_BLOCK_RX_BUFFER, 0);
    uint8_t rxReadValue = eth.readRegister(rxBufferAddr, W5500_BLOCK_RX_BUFFER, 0);
    TEST_ASSERT_EQUAL_HEX8(rxTestValue, rxReadValue);

    // Verify different sockets use different memory spaces
    uint8_t socket0Value = 0x77;
    uint8_t socket1Value = 0x88;
    eth.writeRegister(socketRegAddr, socket0Value, W5500_BLOCK_SOCKET, 0);
    eth.writeRegister(socketRegAddr, socket1Value, W5500_BLOCK_SOCKET, 1);
    
    uint8_t socket0Read = eth.readRegister(socketRegAddr, W5500_BLOCK_SOCKET, 0);
    uint8_t socket1Read = eth.readRegister(socketRegAddr, W5500_BLOCK_SOCKET, 1);
    
    TEST_ASSERT_EQUAL_HEX8(socket0Value, socket0Read);
    TEST_ASSERT_EQUAL_HEX8(socket1Value, socket1Read);
    TEST_ASSERT_NOT_EQUAL(socket0Read, socket1Read);
}

// Test chip identification
void test_chip_identification() {
    // Reset the chip first to ensure clean state
    eth.reset();
    
    // Read chip ID
    uint8_t chipId = eth.readChipID();
    
    // W5500 should return 0x04 as its chip ID
    TEST_ASSERT_EQUAL_HEX8(0x04, chipId);
    
    // Verify multiple reads return the same value
    TEST_ASSERT_EQUAL_HEX8(chipId, eth.readChipID());
}
