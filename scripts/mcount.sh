#!/usr/bin/bash
# Count packets on an interface from start until you press enter
# Default TX packets, use -r for RX instead, -b for both

set -euo pipefail

usage() {
    cat << EOF
Usage: ${0##*/} [-r|-b] <iface>
  -r   count RX packets instead of TX
  -b   show both TX and RX
Example: ${0##*/} eth0
         ${0##*/} -r enp3s0
         ${0##*/} -b ens5

EOF
    exit 1
}

mode="tx"
while getopts ":rb" opt; do
    case "$opt" in
        r) mode="rx" ;;
        b) mode="both" ;;
        *) usage ;;
    esac
done
shift $((OPTIND-1))

IFACE="${1:-}"
[[ -z "$IFACE" ]] && usage

STAT_DIR="/sys/class/net/$IFACE/statistics"
[[ -d "$STAT_DIR" ]] || { echo "Interface '$IFACE' not found."; exit 2; }

file_for()
{
    local which="$1"
    echo "$STAT_DIR/${which}_packets"
}

read_val() { <"$1" tr -d '\n'; }

# Pick files
TXF="$(file_for tx)"
RXF="$(file_for rx)"
for f in "$TXF" "$RXF"; do [[ -r "$f" ]] || { echo "Missing counter: $f"; exit 3; }; done

# Take starting snapshot
t0_ns=$(date +%s%N)
tx0=$(read_val "$TXF")
rx0=$(read_val "$RXF")

echo "Counting on $IFACE (mode: $mode). Press ENTER to stop..."
# Make Enter end immediately (no buffering)
# shellcheck disable=SC2162
read -r

# Final snapshot
t1_ns=$(date +%s%N)
tx1=$(read_val "$TXF")
rx1=$(read_val "$RXF")

# Compute
dt_ns=$((t1_ns - t0_ns))
dt_s=$(awk -v ns="$dt_ns" 'BEGIN{printf("%.6f", ns/1e9)}')

dtx=$((tx1 - tx0))
drx=$((rx1 - rx0))

# Output based on mode
echo "Duration: ${dt_s}s"
case "$mode" in
    tx)
        mpps=$(awk -v p="$dtx" -v s="$dt_s" 'BEGIN{printf("%.3f", p/(s*1e6))}')
        printf "TX packets: %d (avg: %s Mpps)\n" "$dtx" "$mpps"
        ;;
    rx)
        mpps=$(awk -v p="$drx" -v s="$dt_s" 'BEGIN{printf("%.3f", p/(s*1e6))}')
        printf "RX packets: %d (avg: %s Mpps)\n" "$drx" "$mpps"
        ;;
    both)
        mpps=$(awk -v p="$dtx" -v s="$dt_s" 'BEGIN{printf("%.3f", p/(s*1e6))}')
        mpps=$(awk -v p="$drx" -v s="$dt_s" 'BEGIN{printf("%.3f", p/(s*1e6))}')
        printf "TX packets: %d (avg: %s Mpps)\n" "$dtx" "$mpps"
        printf "RX packets: %d (avg: %s Mpps)\n" "$drx" "$mpps"
        ;;
esac

