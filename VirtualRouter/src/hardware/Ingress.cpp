#include <Ingress.h>
#include <iostream>
#include <pcap.h>

// Mutex for synchronizing access to the packet queue.
std::mutex packetQueueMutex;

Ingress::Ingress(const std::string& device, const std::string mask, const size_t inQueSize) : packetQueue(inQueSize) 
{
    char errbuf[PCAP_ERRBUF_SIZE]; // Buffer for error messages.
    
    // Open a live capture session on the specified network device.
    pcap_handle = pcap_open_live(device.c_str(), BUFSIZ, 1, 1000, errbuf);
    if (pcap_handle == NULL) 
    {
        // Print an error message and exit if the device cannot be opened.
        std::cerr << "Error opening device " << device << ": " << errbuf << std::endl;
        exit(1);
    }
}

Ingress::~Ingress() 
{
    if (pcap_handle != NULL) 
    {
        pcap_close(pcap_handle); // Close the pcap handle if it is open.
    }
}

void Ingress::stopSnif() 
{
    pcap_close(pcap_handle);
}

int Ingress::startCapture(const char* filter_exp) 
{
    struct bpf_program fp; // Structure for the compiled filter program.
    bpf_u_int32 netmask = subnet; // Netmask for the filter.

    // Compile the filter expression into a BPF program.
    if (pcap_compile(pcap_handle, &fp, filter_exp, 0, netmask) == -1) 
    {
        std::cerr << "Error compiling filter: " << pcap_geterr(pcap_handle) << std::endl;
        return 1;
    }
    
    // Set the compiled filter program for the capture session.
    if (pcap_setfilter(pcap_handle, &fp) == -1) 
    {
        std::cerr << "Error setting filter: " << pcap_geterr(pcap_handle) << std::endl;
        return 1;
    }
    
    // Start capturing packets and process them with the packetHandler function.
    pcap_loop(pcap_handle, -1, packetHandler, reinterpret_cast<u_char*>(this));
    return 0;
}

void Ingress::packetHandler(u_char* user, const struct pcap_pkthdr* pkthdr, const u_char* packet) 
{
    Ingress* ingress = reinterpret_cast<Ingress*>(user); // Cast user data to Ingress pointer.

    // Ethernet header is 14 bytes
    if (pkthdr->caplen < 14) {
        // Packet too shor, ignore
        return;
    }

    std::string packetData(reinterpret_cast<const char*>(packet), pkthdr->caplen); // Extract packet data.
    
    // Lock the mutex and enqueue the packet data.
    {
        std::lock_guard<std::mutex> lock(packetQueueMutex);
        ingress->packetQueue.enqueue(packetData);
    }
}
