// Ingress.h

#ifndef INGRESS_H
#define INGRESS_H

#include <pcap.h>
#include <string>
#include <mutex>
#include <Functions.h>
#include <ByteString.hpp>

#include "Queue.hpp"

/**
 * @file Ingress.h
 * @brief Provides the Ingress class for capturing and queuing network packets.
 */

// Initialize the static mutex.
extern std::mutex packetQueueMutex;

/**
 * @class Ingress
 * @brief Handles capturing network packets from a specified network interface.
 *
 * The Ingress class is responsible for initiating a live packet capture session
 * on a specified network device using the libpcap library. Captured packets are
 * enqueued into a thread-safe ring buffer for further processing.
 *
 * @note Ensure that the network interface provided exists and is up before using
 *       the Ingress class to avoid runtime errors.
 */
class Ingress {
public:

    /**
     * @brief Constructs an Ingress object and initializes the pcap handle.
     *
     * This constructor opens a live capture session on the specified network device
     * with the given subnet mask and initializes the packet queue with the specified size.
     * If the device cannot be opened, it prints an error message and exits the program.
     *
     * @param device The name of the network device (e.g., "eth0") to capture packets from.
     * @param mask A string representing the subnet mask (e.g., "255.255.255.0").
     * @param inQueSize The maximum size of the packet queue.
     *
     * @note The constructor will terminate the program if the pcap handle cannot be opened.
     */
    Ingress(const std::string& device, const std::string mask, const size_t inQueSize);

    /**
     * @brief Destructs the Ingress object and closes the pcap handle.
     *
     * The destructor ensures that the pcap handle is properly closed to release
     * any associated resources.
     */
    ~Ingress();

    /**
     * @brief Starts capturing packets with the specified filter expression.
     *
     * This method compiles the provided filter expression into a BPF program and
     * applies it to the capture session. It then enters the packet capture loop,
     * processing packets using the `packetHandler` callback function.
     *
     * @param filter_exp A C-string containing the filter expression (e.g., "tcp port 80").
     * @return An integer status code: `0` on success, `1` on failure.
     *
     * @note The capture loop will run indefinitely until manually terminated.
     */
    int startCapture(const char* filter_exp);

    /**
     * @brief Stops the packet capture session.
     *
     * This method closes the pcap handle, effectively stopping the packet capture loop.
     */
    void stopSnif();

    /**
     * @brief Thread-safe ring buffer to store captured packets.
     *
     * The `packetQueue` is used to enqueue captured packets for further processing.
     * It is implemented as a ring buffer with a fixed maximum size.
     */
    RingBuffer<ByteString> packetQueue;

private:

    pcap_t* pcap_handle;    ///< Holds the pcap handle for capturing packets.
    bpf_u_int32 subnet;     ///< Stores the subnet mask in the network byte order.

    /**
     * @brief Callback function for processing captured packets.
     *
     * This static method is called by libpcap for each captured packet. It enqueues
     * the packet data into the `packetQueue` in a thread-safe manner.
     *
     * @param user A pointer to user-defined data; in this case, a pointer to the Ingress instance.
     * @param pkthdr A pointer to the pcap packet header containing metadata about the packet.
     * @param packet A pointer to the raw packet data.
     */
    static void packetHandler(u_char* user, const struct pcap_pkthdr* pkthdr, const u_char* packet);
};

#endif // INGRESS_H
