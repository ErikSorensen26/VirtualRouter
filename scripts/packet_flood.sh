#!/bin/bash

modprobe pktgen
IF=lo
DST=127.0.0.1
PGDEV=/proc/net/pktgen/$IF

for k in /proc/net/pktgen/kpktgend_*; do
    echo "rem_device_all" > $k
    echo "add_device $IF" > $k
done

{
    echo "clone_skb 0"
    echo "pkt_size 128"
    echo "dst $DST"
    echo "count 0"
    echo "delay 0"
} > $PGDEV

echo "start" > /proc/net/pktgen/pgctrl
sleep 10
echo "stop" > /proc/net/pktgen/pgctrl
grep -H . $PGDEV
