#include <Egress.h>

// Constructor initializes the pcap handle for sending packets on the specified interface.
Egress::Egress(const std::string& interface) 
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_handle = pcap_open_live(interface.c_str(), BUFSIZ, 1, 1000, errbuf);
    if (pcap_handle == NULL) {
        std::cerr << "Error opening device: " << errbuf << std::endl;
        throw std::runtime_error("Failed to open device");
    }
}

// Destructor closes the pcap handle if it's not NULL.
Egress::~Egress() 
{
    if (pcap_handle != NULL) 
    {
        pcap_close(pcap_handle);
    }
}

// Sends a packet with data converted from base256 string format.
bool Egress::sendPacket(const std::string& base256Str) 
{
  std::vector<unsigned char> packet_data = base256ToBytes(base256Str);
  int packet_length = packet_data.size();
  if (pcap_sendpacket(pcap_handle, packet_data.data(), packet_length) != 0) 
  {
      std::cerr << "Error sending packet: " << pcap_geterr(pcap_handle) << std::endl;
      return false;
  }
  return true;
}

// Converts a base256 string to a vector of unsigned char bytes.
std::vector<unsigned char> Egress::base256ToBytes(const std::string& base256Str) 
{
    std::vector<unsigned char> bytes(base256Str.begin(), base256Str.end());
    return bytes;
}
