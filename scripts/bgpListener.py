#!/usr/bin/env python3

import socket
import threading
import binascii

HOST = "0.0.0.0"
PORT = 179  # Change to 1179 if not running as root

def hexdump(data: bytes) -> str:
    hex_part = binascii.hexlify(data).decode()
    ascii_part = "".join(chr(b) if 32 <= b <= 126 else "." for b in data)
    return f"\nHEX   : {hex_part}\nASCII : {ascii_part}\n"

def handle_client(conn: socket.socket, addr):
    print(f"[+] Connection from {addr[0]}:{addr[1]}")
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break

            print(f"[{addr[0]}:{addr[1]}] Received {len(data)} bytes")
            print(hexdump(data))

    except Exception as e:
        print(f"[!] Error with {addr}: {e}")
    finally:
        print(f"[-] Connection closed {addr[0]}:{addr[1]}")
        conn.close()

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen()

        print(f"[*] Listening on {HOST}:{PORT}")

        while True:
            conn, addr = server.accept()
            thread = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            thread.start()

if __name__ == "__main__":
    main()

