#!/bin/bash

CONNECTION_NAME="Wired connection 1"  # Change this to your connection name

if [ "$1" == "enable" ]; then
    nmcli connection modify "$CONNECTION_NAME" ipv4.method auto
    echo "DHCP enabled"
elif [ "$1" == "disable" ]; then
    nmcli connection modify "$CONNECTION_NAME" ipv4.method manual
    nmcli connection modify "$CONNECTION_NAME" ipv4.addresses 192.168.1.100/24  # Adjust IP as needed
    nmcli connection modify "$CONNECTION_NAME" ipv4.gateway 192.168.1.1
    nmcli connection modify "$CONNECTION_NAME" ipv4.dns 8.8.8.8
    echo "DHCP disabled"
else
    echo "Usage: $0 [enable|disable]"
fi

nmcli connection down "$CONNECTION_NAME"
nmcli connection up "$CONNECTION_NAME"

