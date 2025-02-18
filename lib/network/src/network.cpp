#include "../include/network.h"

// W5500 Socket Register Addresses
#define Sn_MR        0x0000    // Socket n Mode Register
#define Sn_CR        0x0001    // Socket n Command Register
#define Sn_IR        0x0002    // Socket n Interrupt Register
#define Sn_SR        0x0003    // Socket n Status Register
#define Sn_PORT      0x0004    // Socket n Source Port Register
#define Sn_DIPR      0x000C    // Socket n Destination IP Address Register
#define Sn_DPORT     0x0010    // Socket n Destination Port Register
#define Sn_TX_FSR    0x0020    // Socket n TX Free Size Register
#define Sn_TX_RD     0x0022    // Socket n TX Read Pointer Register
#define Sn_TX_WR     0x0024    // Socket n TX Write Pointer Register
#define Sn_RX_RSR    0x0026    // Socket n RX Received Size Register
#define Sn_RX_RD     0x0028    // Socket n RX Read Pointer Register
#define Sn_RX_WR     0x002A    // Socket n RX Write Pointer Register

// Socket Commands
#define SOCK_OPEN    0x01
#define SOCK_CLOSE   0x10
#define SOCK_SEND    0x20
#define SOCK_RECV    0x40

// DHCP Configuration
#define DHCP_TIMEOUT_MS 10000  // 10 second timeout for DHCP
#define DHCP_PORT 68           // DHCP client port
#define DHCP_RENEWAL_TIME_MS (12 * 60 * 60 * 1000) // 12 hours

bool Network::begin(W5500* eth) {
    if (!eth) return false;
    
    w5500 = eth;
    dhcpActive = false;
    dhcpRenewalTime = 0;
    
    // Initialize socket states
    memset(socketStates, SOCK_CLOSED, sizeof(socketStates));
    
    return true;
}

bool Network::configure(const IPConfig& config) {
    if (dhcpActive) {
        stopDHCP();
    }
    
    applyConfig(config);
    currentConfig = config;
    
    return true;
}

void Network::applyConfig(const IPConfig& config) {
    // Set network configuration in W5500 registers
    // Gateway
    for (int i = 0; i < 4; i++) {
        w5500->writeRegister(0x0001 + i, config.gateway[i]);
    }
    
    // Subnet
    for (int i = 0; i < 4; i++) {
        w5500->writeRegister(0x0005 + i, config.subnet[i]);
    }
    
    // MAC Address (using a default for now)
    uint8_t mac[] = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56};
    for (int i = 0; i < 6; i++) {
        w5500->writeRegister(0x0009 + i, mac[i]);
    }
    
    // IP Address
    for (int i = 0; i < 4; i++) {
        w5500->writeRegister(0x000F + i, config.ip[i]);
    }
}

bool Network::startDHCP() {
    // Create DHCP socket
    int8_t dhcpSocket = createUDPSocket(DHCP_PORT);
    if (dhcpSocket < 0) return false;
    
    // TODO: Implement full DHCP client
    // For now, just simulate DHCP by using test values
    IPConfig dhcpConfig;
    dhcpConfig.ip[0] = 192;
    dhcpConfig.ip[1] = 168;
    dhcpConfig.ip[2] = 1;
    dhcpConfig.ip[3] = 100;
    
    dhcpConfig.subnet[0] = 255;
    dhcpConfig.subnet[1] = 255;
    dhcpConfig.subnet[2] = 255;
    dhcpConfig.subnet[3] = 0;
    
    dhcpConfig.gateway[0] = 192;
    dhcpConfig.gateway[1] = 168;
    dhcpConfig.gateway[2] = 1;
    dhcpConfig.gateway[3] = 1;
    
    dhcpConfig.dns[0] = 8;
    dhcpConfig.dns[1] = 8;
    dhcpConfig.dns[2] = 8;
    dhcpConfig.dns[3] = 8;
    
    dhcpConfig.dhcpEnabled = true;
    
    applyConfig(dhcpConfig);
    currentConfig = dhcpConfig;
    
    dhcpActive = true;
    dhcpRenewalTime = millis() + DHCP_RENEWAL_TIME_MS;
    
    closeSocket(dhcpSocket);
    return true;
}

void Network::stopDHCP() {
    dhcpActive = false;
    currentConfig.dhcpEnabled = false;
}

int8_t Network::createUDPSocket(uint16_t port) {
    int8_t socket = findFreeSocket();
    if (socket < 0) return -1;
    
    if (!initSocket(socket, port, SOCK_UDP)) {
        return -1;
    }
    
    socketStates[socket] = SOCK_UDP;
    return socket;
}

void Network::closeSocket(uint8_t socket) {
    if (socket >= 8) return;
    
    // Send close command
    w5500->writeRegister(Sn_CR, SOCK_CLOSE, W5500_BLOCK_SOCKET, socket);
    
    // Wait for socket to close
    while (w5500->readRegister(Sn_SR, W5500_BLOCK_SOCKET, socket) != SOCK_CLOSED) {
        delay(1);
    }
    
    socketStates[socket] = SOCK_CLOSED;
}

int16_t Network::sendUDP(uint8_t socket, const UDPPacket& packet) {
    if (socket >= 8 || socketStates[socket] != SOCK_UDP) return -1;
    
    // Set destination IP and port
    for (int i = 0; i < 4; i++) {
        w5500->writeRegister(Sn_DIPR + i, packet.destIP[i], W5500_BLOCK_SOCKET, socket);
    }
    w5500->writeRegister(Sn_DPORT, packet.destPort >> 8, W5500_BLOCK_SOCKET, socket);
    w5500->writeRegister(Sn_DPORT + 1, packet.destPort & 0xFF, W5500_BLOCK_SOCKET, socket);
    
    // Get TX write pointer
    uint16_t txWritePtr = (w5500->readRegister(Sn_TX_WR, W5500_BLOCK_SOCKET, socket) << 8) |
                          w5500->readRegister(Sn_TX_WR + 1, W5500_BLOCK_SOCKET, socket);
    
    // Write data to TX buffer
    for (uint16_t i = 0; i < packet.length; i++) {
        w5500->writeRegister(txWritePtr + i, packet.data[i], W5500_BLOCK_TX_BUFFER, socket);
    }
    
    // Update TX write pointer
    txWritePtr += packet.length;
    w5500->writeRegister(Sn_TX_WR, txWritePtr >> 8, W5500_BLOCK_SOCKET, socket);
    w5500->writeRegister(Sn_TX_WR + 1, txWritePtr & 0xFF, W5500_BLOCK_SOCKET, socket);
    
    // Send
    w5500->writeRegister(Sn_CR, SOCK_SEND, W5500_BLOCK_SOCKET, socket);
    
    // Wait for send to complete
    while (w5500->readRegister(Sn_CR, W5500_BLOCK_SOCKET, socket));
    
    return packet.length;
}

int16_t Network::receiveUDP(uint8_t socket, UDPPacket& packet) {
    if (socket >= 8 || socketStates[socket] != SOCK_UDP) return -1;
    
    // Check if data is available
    uint16_t rxSize = (w5500->readRegister(Sn_RX_RSR, W5500_BLOCK_SOCKET, socket) << 8) |
                      w5500->readRegister(Sn_RX_RSR + 1, W5500_BLOCK_SOCKET, socket);
    
    if (rxSize == 0) return -1;
    
    // Get RX read pointer
    uint16_t rxReadPtr = (w5500->readRegister(Sn_RX_RD, W5500_BLOCK_SOCKET, socket) << 8) |
                         w5500->readRegister(Sn_RX_RD + 1, W5500_BLOCK_SOCKET, socket);
    
    // Read data from RX buffer
    for (uint16_t i = 0; i < rxSize && i < 1472; i++) { // Max UDP payload
        packet.data[i] = w5500->readRegister(rxReadPtr + i, W5500_BLOCK_RX_BUFFER, socket);
    }
    
    // Get source IP and port
    for (int i = 0; i < 4; i++) {
        packet.srcIP[i] = w5500->readRegister(Sn_DIPR + i, W5500_BLOCK_SOCKET, socket);
    }
    packet.srcPort = (w5500->readRegister(Sn_DPORT, W5500_BLOCK_SOCKET, socket) << 8) |
                     w5500->readRegister(Sn_DPORT + 1, W5500_BLOCK_SOCKET, socket);
    
    // Update RX read pointer
    rxReadPtr += rxSize;
    w5500->writeRegister(Sn_RX_RD, rxReadPtr >> 8, W5500_BLOCK_SOCKET, socket);
    w5500->writeRegister(Sn_RX_RD + 1, rxReadPtr & 0xFF, W5500_BLOCK_SOCKET, socket);
    
    // Issue RECV command
    w5500->writeRegister(Sn_CR, SOCK_RECV, W5500_BLOCK_SOCKET, socket);
    
    packet.length = rxSize;
    return rxSize;
}

bool Network::isConnected() {
    // Read PHY status register
    uint8_t phyStatus = w5500->readRegister(0x003C); // PHY Status Register
    return (phyStatus & 0x01) == 0x01; // Check link status bit
}

void Network::process() {
    if (dhcpActive && millis() >= dhcpRenewalTime) {
        // Time to renew DHCP lease
        startDHCP();
    }
}

bool Network::initSocket(uint8_t socket, uint16_t port, SocketState protocol) {
    // Set socket mode
    w5500->writeRegister(Sn_MR, protocol, W5500_BLOCK_SOCKET, socket);
    
    // Set source port
    w5500->writeRegister(Sn_PORT, port >> 8, W5500_BLOCK_SOCKET, socket);
    w5500->writeRegister(Sn_PORT + 1, port & 0xFF, W5500_BLOCK_SOCKET, socket);
    
    // Open socket
    w5500->writeRegister(Sn_CR, SOCK_OPEN, W5500_BLOCK_SOCKET, socket);
    
    // Wait for socket to initialize
    while (w5500->readRegister(Sn_SR, W5500_BLOCK_SOCKET, socket) != SOCK_UDP) {
        if (w5500->readRegister(Sn_SR, W5500_BLOCK_SOCKET, socket) == SOCK_CLOSED) {
            return false;
        }
        delay(1);
    }
    
    return true;
}

int8_t Network::findFreeSocket() {
    for (int8_t i = 0; i < 8; i++) {
        if (socketStates[i] == SOCK_CLOSED) {
            return i;
        }
    }
    return -1;
}

bool Network::handleDHCP() {
    // TODO: Implement actual DHCP client protocol
    return dhcpActive;
}
