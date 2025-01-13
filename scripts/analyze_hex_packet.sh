#!/bin/bash

# Script Name: analyze_hex_packet.sh
# Description: Takes a hex packet as input, formats it for text2pcap, displays the formatted hex data,
#              converts it to a PCAP file with specified link type, opens it with Wireshark,
#              and deletes temporary files after Wireshark is closed.
# Dependencies: text2pcap, wireshark
# Usage: ./analyze_hex_packet.sh "HEX_PACKET_STRING" [LINK_TYPE]
# Example: ./analyze_hex_packet.sh "AABBCCDDEEFF..." 1

# Exit immediately if a command exits with a non-zero status.
set -e

#######################################
# Function: Display usage instructions
# Arguments: None
# Returns: Exits the script
#######################################
usage() {
    echo "Usage: $0 \"HEX_PACKET_STRING\" [LINK_TYPE]"
    echo
    echo "Arguments:"
    echo "  HEX_PACKET_STRING   The hexadecimal string representing the packet."
    echo "  LINK_TYPE           (Optional) Link-layer header type for text2pcap."
    echo "                      Common link types:"
    echo "                        1   - Ethernet"
    echo "                        101 - Raw IP"
    echo "                        113 - IEEE 802.11 (Wi-Fi)"
    echo "                        147 - PPP (Point-to-Point Protocol)"
    echo "                        228 - FDDI"
    echo "                      Default: 1 (Ethernet)"
    echo
    echo "Examples:"
    echo "  $0 \"AABBCCDDEEFF112233445566080045...\" 1"
    echo "  $0 \"450000341c4640004006...\""
    exit 1
}

#######################################
# Function: Check for required dependencies
# Arguments: None
# Returns: Exits the script if dependencies are missing
#######################################
check_dependencies() {
    local dependencies=("text2pcap" "wireshark")
    for cmd in "${dependencies[@]}"; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "Error: Required command '$cmd' is not installed."
            echo "Please install it before running this script."
            echo "For example, on Debian/Ubuntu:"
            echo "  sudo apt-get update && sudo apt-get install wireshark"
            exit 1
        fi
    done
}

#######################################
# Function: Format hex string to text2pcap-compatible dump
# Arguments:
#   $1 - Hex string
# Returns:
#   Outputs formatted hex dump to stdout
#######################################
format_hex_dump() {
    local hex_string="$1"
    local total_length=${#hex_string}
    local bytes_per_line=32  # 2 hex digits per byte (16 bytes)

    # Add a timestamp line (ISO 8601 format)
    echo "I $(date -u +"%Y-%m-%dT%H:%M:%SZ")"

    # Process the hex string in chunks of 32 characters (16 bytes)
    for ((i=0; i<total_length; i+=bytes_per_line)); do
        local chunk=${hex_string:i:bytes_per_line}
        # Ensure the chunk has exactly bytes_per_line characters
        if [ ${#chunk} -lt $bytes_per_line ]; then
            # Pad with spaces if necessary
            chunk=$(printf "%-32s" "$chunk")
        fi
        # Print the address offset
        printf "%06X " $((i / 2))
        # Print the hex bytes separated by spaces
        echo "$chunk" | sed 's/\(..\)/\1 /g'
    done

    # Print the final offset line
    printf "%06X\n" $(( (total_length + 1) / 2 ))
}

#######################################
# Function: Display formatted hex dump
# Arguments:
#   $1 - Formatted hex dump
# Returns: None
#######################################
display_formatted_hex() {
    local FORMATTED_HEX="$1"
    echo "📝 **Formatted Hex Dump:**"
    echo "----------------------------------------"
    echo "$FORMATTED_HEX"
    echo "----------------------------------------"
    echo ""
}

#######################################
# Function: Open PCAP with Wireshark
# Arguments:
#   $1 - PCAP file path
# Returns: None
#######################################
open_with_wireshark() {
    local PCAP_FILE="$1"
    echo "🔍 **Opening PCAP in Wireshark...**"
    wireshark "$PCAP_FILE" &
    WISHKARK_PID=$!
    # Wait for Wireshark to close
    wait $WISHKARK_PID
}

#######################################
# Main Script Execution
#######################################

# Check if at least one argument is provided
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Error: Incorrect number of arguments."
    usage
fi

# Check dependencies
check_dependencies

# Assign arguments
HEX_PACKET="$1"
LINK_TYPE="${2:-1}"  # Default to 1 (Ethernet) if not provided

# Validate the hex string: should contain only 0-9, a-f, A-F and have even length
if ! [[ "$HEX_PACKET" =~ ^([0-9A-Fa-f]{2})+$ ]]; then
    echo "Error: Invalid hex string. Ensure it contains only hexadecimal characters (0-9, A-F) and has an even number of digits."
    exit 1
fi

# Validate that LINK_TYPE is a positive integer
if ! [[ "$LINK_TYPE" =~ ^[0-9]+$ ]]; then
    echo "Error: LINK_TYPE must be a positive integer representing the link-layer header type."
    usage
fi

# Create temporary directory and files
TEMP_DIR=$(mktemp -d)
FORMATTED_HEX_FILE="$TEMP_DIR/packet_formatted.txt"
PCAP_FILE="$TEMP_DIR/packet.pcap"

# Ensure cleanup of temporary files on exit
cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

# Format the hex string into text2pcap-compatible dump and write to FORMATTED_HEX_FILE
FORMATTED_HEX=$(format_hex_dump "$HEX_PACKET")
echo "$FORMATTED_HEX" > "$FORMATTED_HEX_FILE"

# Display the formatted hex data
display_formatted_hex "$FORMATTED_HEX"

# Define the timestamp pattern matching the formatted dump
# Example: "I 2025-01-12T17:47:55Z"
TIMESTAMP_PATTERN="I %Y-%m-%dT%H:%M:%SZ"

# Convert hex to PCAP using text2pcap
# -l specifies the link-layer header type
# -t specifies the timestamp format
# Avoid using -C to prevent pcapng format; default is pcap
echo "🔄 **Converting hex to PCAP...**"
if ! text2pcap -l "$LINK_TYPE" -t "$TIMESTAMP_PATTERN" "$FORMATTED_HEX_FILE" "$PCAP_FILE"; then
    echo "Error: Failed to convert hex to PCAP. Please ensure the hex string represents a valid packet for the specified link type."
    echo "Refer to https://www.wireshark.org/docs/man-pages/text2pcap.html for valid link type numbers."
    exit 1
fi
echo "✅ Conversion successful. PCAP file created at $PCAP_FILE"

# Open the PCAP file with Wireshark
open_with_wireshark "$PCAP_FILE"

# End of script
