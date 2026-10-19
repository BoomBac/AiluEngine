import json
import struct

MAGIC = 0x4B4C4C41
VERSION = 1
HEADER = struct.Struct("<IHHIQ")
HELLO, HELLO_ACK, PING, PONG = 1, 2, 3, 4
FULL_SYNC_BEGIN, FULL_SYNC_END, OBJECT_UPSERT, OBJECT_DELETE = 10, 11, 12, 13
MESH_UPSERT, MATERIAL_UPSERT, LIGHT_UPSERT, CAMERA_UPSERT = 20, 21, 22, 23
MESH_PROTOTYPE_UPSERT = 24
SCENE_SETTINGS_UPSERT = 25
SCENE_DISPLAY_FEATURE = "scene_display_v1"
SERVER_STATUS = 30
INSTANCE_BATCH_UPSERT = 31
MESH_STATIC_BATCH_UPSERT = 32
INSTANCE_BATCH_FEATURE = "instance_batch_v1"
MESH_PROTOTYPE_FEATURE = "mesh_prototype_v1"
MESH_STATIC_BATCH_FEATURE = "static_mesh_batch_v1"


def packet(message_type, sequence, payload=b""):
    return HEADER.pack(MAGIC, VERSION, message_type, len(payload), sequence) + payload


def json_packet(message_type, sequence, value):
    return packet(message_type, sequence, json.dumps(value, separators=(",", ":")).encode("utf-8"))


def recv_exact(sock, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("Ailu closed the connection")
        chunks.extend(chunk)
    return bytes(chunks)


def recv_packet(sock):
    magic, version, kind, size, sequence = HEADER.unpack(recv_exact(sock, HEADER.size))
    if magic != MAGIC or version != VERSION or size > 256 * 1024 * 1024:
        raise ValueError("Invalid Ailu Live Link packet")
    return kind, sequence, recv_exact(sock, size)

WORLD_UPSERT = 26
WORLD_FEATURE = "world_environment_v1"
