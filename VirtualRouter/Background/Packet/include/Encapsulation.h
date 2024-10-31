#pragma once

#include "Process.h"
#include <Checksums.h>

#include <any>

using namespace std;

// Encapsulates packet information into a formatted string.
std::string Encapsulate(PacketInfo& packet, string encapsulated = "");