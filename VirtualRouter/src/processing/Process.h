// Process.h

#ifndef PROCESS_H
#define PROCESS_H

#include <cstdint>
#include <cstddef>

class Interface;
class VirtualRouter;
struct PacketInfo;

/**
 * @class ProcessPacket
 * @brief Handles processing of captured network packets across various protocol layers.
 *
 * The `ProcessPacket` class is responsible for analyzing and handling network packets
 * by examining different protocol headers (Layer 2 to Layer 5). It interacts with
 * the `Interface` class to manage ARP replies, update routing tables, and handle DHCP
 * and EIGRP operations.
 *
 * @note Ensure that the `Interface` object provided is valid and properly initialized
 *       before using the `ProcessPacket` class to avoid undefined behavior.
 */
void processPacket(const uint8_t* data, size_t len, PacketInfo& packet, VirtualRouter* vrf, Interface* interface);

#endif
