#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include "w5500.h"

// Network Configuration
struct IPConfig {
    uint8_t ip[4];        // IP address
    uint8_t subnet[4];    // Subnet mask
    uint8_t gateway[4];   // Gateway address
    uint8_t dns[4];       // DNS server
    bool dhcpEnabled;     // Whether DHCP is enabled
};

// UDP Packet Structure
struct UDPPacket {
    uint8_t* data;        // Packet data
    uint16_t length;      // Data length
    uint8_t srcIP[4];     // Source IP
    uint16_t srcPort;     // Source port
    uint8_t destIP[4];    // Destination IP
    uint16_t destPort;    // Destination port
};

// Socket States
enum SocketState {
    SOCK_CLOSED      = 0x00,
    SOCK_INIT        = 0x13,
    SOCK_UDP         = 0x22
};

class Network {
public:
    /**
     * @brief Initialize network stack
     * @param eth Pointer to W5500 driver instance
     * @return true if initialization successful
     */
    bool begin(W5500* eth);

    /**
     * @brief Configure network settings
     * @param config Network configuration
     * @return true if configuration successful
     */
    bool configure(const IPConfig& config);

    /**
     * @brief Start DHCP client
     * @return true if DHCP lease obtained
     */
    bool startDHCP();

    /**
     * @brief Stop DHCP client
     */
    void stopDHCP();

    /**
     * @brief Create UDP socket
     * @param port Local port number
     * @return Socket number (0-7) or -1 on failure
     */
    int8_t createUDPSocket(uint16_t port);

    /**
     * @brief Close UDP socket
     * @param socket Socket number
     */
    void closeSocket(uint8_t socket);

    /**
     * @brief Send UDP packet
     * @param socket Socket number
     * @param packet UDP packet to send
     * @return Number of bytes sent or -1 on failure
     */
    int16_t sendUDP(uint8_t socket, const UDPPacket& packet);

    /**
     * @brief Receive UDP packet
     * @param socket Socket number
     * @param packet UDP packet to fill
     * @return Number of bytes received or -1 if no data
     */
    int16_t receiveUDP(uint8_t socket, UDPPacket& packet);

    /**
     * @brief Get current IP configuration
     * @return Current IP configuration
     */
    const IPConfig& getConfig() const { return currentConfig; }

    /**
     * @brief Check if network is connected
     * @return true if physical link is up
     */
    bool isConnected();

    /**
     * @brief Process network tasks
     * Should be called regularly to handle DHCP renewal
     */
    void process();

private:
    W5500* w5500;                     // W5500 driver instance
    IPConfig currentConfig;           // Current network configuration
    bool dhcpActive;                  // DHCP client state
    uint32_t dhcpRenewalTime;         // Next DHCP renewal time
    uint8_t socketStates[8];          // Track socket states

    /**
     * @brief Initialize socket
     * @param socket Socket number
     * @param port Local port
     * @param protocol Protocol (UDP)
     * @return true if successful
     */
    bool initSocket(uint8_t socket, uint16_t port, SocketState protocol);

    /**
     * @brief Find free socket
     * @return Socket number or -1 if none available
     */
    int8_t findFreeSocket();

    /**
     * @brief Handle DHCP process
     * @return true if DHCP is active and valid
     */
    bool handleDHCP();

    /**
     * @brief Apply network configuration
     * @param config Configuration to apply
     */
    void applyConfig(const IPConfig& config);
};

#endif // NETWORK_H
