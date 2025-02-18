#include "test_common.h"
#include "../lib/network/include/network.h"
#include "../lib/w5500/include/w5500.h"

// Global instances
extern W5500 eth; // Defined in test_w5500_driver.cpp
Network net;

// Initialize hardware in setup() of test_dummy.cpp
// eth.begin();
// net.begin(&eth);

// Test data
const uint8_t testIP[] = {10,132,0,200};
const uint8_t testSubnet[] = {255, 255, 255, 0};
const uint8_t testGateway[] = {10,132,0,1};
const uint8_t testDNS[] = {8, 8, 8, 8};

void test_static_ip_configuration() {
    IPConfig config;
    memcpy(config.ip, testIP, 4);
    memcpy(config.subnet, testSubnet, 4);
    memcpy(config.gateway, testGateway, 4);
    memcpy(config.dns, testDNS, 4);
    config.dhcpEnabled = false;

    // Configure network
    bool result = net.configure(config);
    TEST_ASSERT_TRUE(result);

    // Verify configuration
    const IPConfig& currentConfig = net.getConfig();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testIP, currentConfig.ip, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testSubnet, currentConfig.subnet, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testGateway, currentConfig.gateway, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testDNS, currentConfig.dns, 4);
    TEST_ASSERT_FALSE(currentConfig.dhcpEnabled);
}

void test_dhcp_configuration() {
    // Start DHCP
    bool result = net.startDHCP();
    TEST_ASSERT_TRUE(result);

    // Verify DHCP is active
    const IPConfig& config = net.getConfig();
    TEST_ASSERT_TRUE(config.dhcpEnabled);

    // Verify we got valid IP settings
    TEST_ASSERT_NOT_EQUAL(0, config.ip[0]);
    TEST_ASSERT_NOT_EQUAL(0, config.subnet[0]);
    TEST_ASSERT_NOT_EQUAL(0, config.gateway[0]);

    // Stop DHCP
    net.stopDHCP();
    TEST_ASSERT_FALSE(net.getConfig().dhcpEnabled);
}

void test_udp_socket_creation() {
    // Create UDP socket
    int8_t socket = net.createUDPSocket(5000);
    TEST_ASSERT_GREATER_OR_EQUAL(0, socket);
    TEST_ASSERT_LESS_THAN(8, socket);

    // Try to create another socket with same port (should fail)
    int8_t duplicateSocket = net.createUDPSocket(5000);
    TEST_ASSERT_EQUAL(-1, duplicateSocket);

    // Close socket
    net.closeSocket(socket);

    // Should now be able to create socket with same port
    socket = net.createUDPSocket(5000);
    TEST_ASSERT_GREATER_OR_EQUAL(0, socket);
    TEST_ASSERT_LESS_THAN(8, socket);
}

void test_udp_packet_send_receive() {
    // Create send and receive sockets
    int8_t sendSocket = net.createUDPSocket(5000);
    int8_t recvSocket = net.createUDPSocket(5001);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sendSocket);
    TEST_ASSERT_GREATER_OR_EQUAL(0, recvSocket);

    // Prepare test packet
    const char* testData = "Hello, UDP!";
    UDPPacket sendPacket;
    sendPacket.data = (uint8_t*)testData;
    sendPacket.length = strlen(testData) + 1;
    memcpy(sendPacket.destIP, testIP, 4);
    sendPacket.destPort = 5001;

    // Send packet
    int16_t bytesSent = net.sendUDP(sendSocket, sendPacket);
    TEST_ASSERT_EQUAL(sendPacket.length, bytesSent);

    // Receive packet
    UDPPacket recvPacket;
    uint8_t buffer[32];
    recvPacket.data = buffer;
    int16_t bytesReceived = net.receiveUDP(recvSocket, recvPacket);

    // Verify received data
    TEST_ASSERT_EQUAL(sendPacket.length, bytesReceived);
    TEST_ASSERT_EQUAL_STRING(testData, (char*)recvPacket.data);
    TEST_ASSERT_EQUAL(5000, recvPacket.srcPort);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testIP, recvPacket.srcIP, 4);

    // Clean up
    net.closeSocket(sendSocket);
    net.closeSocket(recvSocket);
}

void test_link_status() {
    // Test initial link status
    bool linkUp = net.isLinkUp();
    TEST_ASSERT_TRUE(linkUp); // Should be up after initialization

    // Test link speed and duplex mode
    uint8_t speed = net.getLinkSpeed();
    bool fullDuplex = net.isFullDuplex();
    TEST_ASSERT_TRUE(speed == 10 || speed == 100); // Should be either 10 or 100 Mbps
    TEST_ASSERT_TRUE(fullDuplex); // Should be full duplex mode
}

void test_network_performance() {
    // Create test socket
    int8_t socket = net.createUDPSocket(5000);
    TEST_ASSERT_GREATER_OR_EQUAL(0, socket);

    // Prepare large test packet (1KB)
    const size_t dataSize = 1024;
    uint8_t* testData = new uint8_t[dataSize];
    for(size_t i = 0; i < dataSize; i++) {
        testData[i] = i & 0xFF;
    }

    UDPPacket packet;
    packet.data = testData;
    packet.length = dataSize;
    memcpy(packet.destIP, testIP, 4);
    packet.destPort = 5001;

    // Test sending multiple packets rapidly
    const int numPackets = 10;
    unsigned long startTime = millis();
    
    for(int i = 0; i < numPackets; i++) {
        int16_t bytesSent = net.sendUDP(socket, packet);
        TEST_ASSERT_EQUAL(dataSize, bytesSent);
        delay(1); // Small delay to prevent overwhelming the network
    }

    unsigned long duration = millis() - startTime;
    
    // Calculate throughput (bytes per second)
    float throughput = (numPackets * dataSize * 1000.0f) / duration;
    
    // Throughput should be at least 100KB/s on a 10Mbps link
    TEST_ASSERT_GREATER_OR_EQUAL(100000, throughput);

    // Clean up
    delete[] testData;
    net.closeSocket(socket);
}

void test_error_recovery() {
    // Test recovery from link down
    net.forcePhyDown(); // Force PHY to down state
    delay(100);
    TEST_ASSERT_FALSE(net.isLinkUp());

    net.forcePhyUp(); // Restore PHY
    delay(100);
    TEST_ASSERT_TRUE(net.isLinkUp());

    // Test recovery from invalid packet
    int8_t socket = net.createUDPSocket(5000);
    TEST_ASSERT_GREATER_OR_EQUAL(0, socket);

    // Send malformed packet (zero length)
    UDPPacket invalidPacket;
    invalidPacket.data = nullptr;
    invalidPacket.length = 0;
    memcpy(invalidPacket.destIP, testIP, 4);
    invalidPacket.destPort = 5001;

    int16_t result = net.sendUDP(socket, invalidPacket);
    TEST_ASSERT_EQUAL(-1, result); // Should fail gracefully

    // Verify socket is still usable
    const char* testData = "Recovery Test";
    UDPPacket validPacket;
    validPacket.data = (uint8_t*)testData;
    validPacket.length = strlen(testData) + 1;
    memcpy(validPacket.destIP, testIP, 4);
    validPacket.destPort = 5001;

    result = net.sendUDP(socket, validPacket);
    TEST_ASSERT_EQUAL(validPacket.length, result);

    net.closeSocket(socket);
}

void test_network_stability() {
    // Create multiple sockets
    const int numSockets = 4;
    int8_t sockets[numSockets];
    
    for(int i = 0; i < numSockets; i++) {
        sockets[i] = net.createUDPSocket(5000 + i);
        TEST_ASSERT_GREATER_OR_EQUAL(0, sockets[i]);
    }

    // Test concurrent socket operations
    const char* testData = "Stability Test";
    UDPPacket packet;
    packet.data = (uint8_t*)testData;
    packet.length = strlen(testData) + 1;
    memcpy(packet.destIP, testIP, 4);

    for(int round = 0; round < 5; round++) {
        for(int i = 0; i < numSockets; i++) {
            packet.destPort = 5000 + ((i + 1) % numSockets); // Send to next socket
            int16_t bytesSent = net.sendUDP(sockets[i], packet);
            TEST_ASSERT_EQUAL(packet.length, bytesSent);

            // Try to receive on all sockets
            UDPPacket recvPacket;
            uint8_t buffer[32];
            recvPacket.data = buffer;
            net.receiveUDP(sockets[i], recvPacket);
        }
        delay(10); // Allow time for packet processing
    }

    // Clean up
    for(int i = 0; i < numSockets; i++) {
        net.closeSocket(sockets[i]);
    }
}
