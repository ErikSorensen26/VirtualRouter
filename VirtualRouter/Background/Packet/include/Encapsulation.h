#pragma once

#include "Process.h"
#include <Checksums.h>
#include <ByteString.hpp>

// Encapsulates packet information into a formatted string.
ByteString encapsulate(PacketInfo& packet, ByteString encapsulated = "");
