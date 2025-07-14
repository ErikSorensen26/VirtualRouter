#include <Egress.h>
#include <iostream>

Egress::Egress(const std::string& interface) 
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_handle = pcap_open_live(interface.c_str(), BUFSIZ, 1, 1000, errbuf);
    if (pcap_handle == NULL) {
        std::cerr << "Error opening device: " << errbuf << std::endl;
        throw std::runtime_error("Failed to open device");
    }
}

Egress::~Egress() 
{
    if (pcap_handle != NULL) 
    {
        pcap_close(pcap_handle);
    }
}

bool Egress::sendPacket(const uint8_t* base256Str, size_t size) 
{
  if (pcap_sendpacket(pcap_handle, base256Str, size) != 0) 
  {
      std::cerr << "Error sending packet: " << pcap_geterr(pcap_handle) << std::endl;
      return false;
  }
  return true;
}
