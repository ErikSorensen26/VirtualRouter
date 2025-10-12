#!/bin/bash
set -e

echo "=== 1. Create two TAP interfaces ==="
sudo ip tuntap add dev tap0 mode tap
sudo ip tuntap add dev tap1 mode tap

# Assign IPs while still down
sudo ip addr add 10.10.0.1/24 dev tap0
sudo ip addr add 10.10.0.2/24 dev tap1

echo "=== 2. Bring up TAPs (still not linked) ==="
sudo ip link set tap0 up
sudo ip link set tap1 up
ping -c 2 -I tap0 10.10.0.2 || echo "Ping failed (expected, no link)"

echo "=== 3. Create a point-to-point bridge (acts like a cable) ==="
sudo ip link add br-tap0-tap1 type bridge
sudo ip link set tap0 master br-tap0-tap1
sudo ip link set tap1 master br-tap0-tap1
sudo ip link set br-tap0-tap1 up

echo "=== 4. Ping should now work ==="
ping -c 2 -I tap0 10.10.0.2

echo "=== 5. Unplug tap1 (remove from bridge) ==="
sudo ip link set tap1 nomaster
ping -c 2 -I tap0 10.10.0.2 || echo "Ping failed (expected after unlink)"

echo "=== 6. Replug tap1 ==="
sudo ip link set tap1 master br-tap0-tap1
ping -c 2 -I tap0 10.10.0.2

echo "=== 7. Cleanup ==="
sudo ip link del tap0
sudo ip link del tap1
sudo ip link del br-tap0-tap1

