#!/usr/bin/env python3
"""
nsd_spp_bridge.py - RFCOMM-to-TCP bridge for USBSID-Pico's Bluetooth SPP
Network SID Device (NSD) transport.

No stock NSD client speaks Bluetooth SPP directly (ACID 64, JSidplay2 and
SIDPlay only speak the TCP variant) - this relays a local TCP port to the
board's Bluetooth SPP service byte for byte in both directions. Point your
NSD client at 127.0.0.1:<local_port> instead of the board's WiFi IP.

Usage:
    python3 nsd_spp_bridge.py <BOARD_BT_ADDR> [local_port] [rfcomm_channel]

Linux only: uses socket.AF_BLUETOOTH / BTPROTO_RFCOMM from the Python
standard library, no extra dependency needed.
"""
import socket
import sys
import threading

DEFAULT_PORT = 6581
DEFAULT_CHANNEL = 1


def relay(src, dst):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    finally:
        try:
            dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def handle_client(tcp_conn, bt_addr, channel):
    bt_sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
    bt_sock.connect((bt_addr, channel))
    threads = [
        threading.Thread(target=relay, args=(tcp_conn, bt_sock), daemon=True),
        threading.Thread(target=relay, args=(bt_sock, tcp_conn), daemon=True),
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    tcp_conn.close()
    bt_sock.close()


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <BOARD_BT_ADDR> [local_port={DEFAULT_PORT}] [rfcomm_channel={DEFAULT_CHANNEL}]")
        sys.exit(1)

    bt_addr = sys.argv[1]
    local_port = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT
    channel = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_CHANNEL

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", local_port))
    srv.listen(1)
    print(f"NSD SPP bridge: 127.0.0.1:{local_port} <-> {bt_addr} channel {channel}")

    while True:
        conn, _ = srv.accept()
        handle_client(conn, bt_addr, channel)


if __name__ == "__main__":
    main()
