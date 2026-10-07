import argparse
import json
import socket
import struct


MAGIC = 0x4B4C4C41
VERSION = 1
HEADER = struct.Struct("<IHHIQ")
HELLO, HELLO_ACK, PING, PONG = 1, 2, 3, 4


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("peer closed before packet was complete")
        data.extend(chunk)
    return bytes(data)


def recv_packet(sock):
    magic, version, message_type, payload_size, sequence = HEADER.unpack(recv_exact(sock, HEADER.size))
    if magic != MAGIC or version != VERSION or payload_size > 256 * 1024 * 1024:
        raise ValueError("invalid Live Link response header")
    return message_type, sequence, recv_exact(sock, payload_size)


def send_packet(sock, message_type, sequence, payload=b""):
    sock.sendall(HEADER.pack(MAGIC, VERSION, message_type, len(payload), sequence) + payload)


def main():
    parser = argparse.ArgumentParser(description="Ailu Blender Live Link transport smoke client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17777)
    parser.add_argument("--pings", type=int, default=1000)
    parser.add_argument("--malformed", choices=("magic", "version", "oversized", "truncated"))
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.settimeout(5)
        if args.malformed:
            if args.malformed == "magic":
                sock.sendall(HEADER.pack(0, VERSION, HELLO, 0, 1))
            elif args.malformed == "version":
                sock.sendall(HEADER.pack(MAGIC, VERSION + 1, HELLO, 0, 1))
                message_type, sequence, payload = recv_packet(sock)
                if message_type != HELLO_ACK or sequence != 1 or json.loads(payload).get("accepted"):
                    raise RuntimeError("server did not reject the mismatched protocol version")
                return
            elif args.malformed == "oversized":
                sock.sendall(HEADER.pack(MAGIC, VERSION, HELLO, 256 * 1024 * 1024 + 1, 1))
            else:
                sock.sendall(HEADER.pack(MAGIC, VERSION, HELLO, 4, 1) + b"x")
            if sock.recv(1):
                raise RuntimeError("server kept malformed client connected")
            print(f"Malformed {args.malformed} packet disconnected")
            return

        send_packet(sock, HELLO, 1, b"{\"client\":\"python-smoke\"}")
        message_type, sequence, payload = recv_packet(sock)
        if message_type != HELLO_ACK or sequence != 1 or not json.loads(payload).get("accepted"):
            raise RuntimeError("server rejected HELLO")

        for sequence in range(2, args.pings + 2):
            payload = struct.pack("<Q", sequence)
            send_packet(sock, PING, sequence, payload)
            message_type, response_sequence, response_payload = recv_packet(sock)
            if message_type != PONG or response_sequence != sequence or response_payload != payload:
                raise RuntimeError(f"invalid PONG for sequence {sequence}")

    print(f"HELLO accepted; {args.pings} PING/PONG exchanges passed")


if __name__ == "__main__":
    main()
