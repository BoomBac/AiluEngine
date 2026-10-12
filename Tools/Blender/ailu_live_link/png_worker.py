"""Standalone PNG encoder. Receives copied top-to-bottom RGBA8 bytes; never imports Blender."""
import os
import struct
import sys
import zlib


def ReadExact(stream, size):
    data = bytearray()
    while len(data) < size:
        block = stream.read(size - len(data))
        if not block:
            raise EOFError("Incomplete PNG job")
        data.extend(block)
    return data


def WriteChunk(stream, kind, data):
    stream.write(struct.pack(">I", len(data)))
    stream.write(kind)
    stream.write(data)
    stream.write(struct.pack(">I", zlib.crc32(data, zlib.crc32(kind)) & 0xffffffff))


def Run():
    source = sys.stdin.buffer
    while header := source.read(12):
        if len(header) != 12:
            raise EOFError("Incomplete PNG header")
        width, height, name_size = struct.unpack("<III", header)
        if not (1 <= width <= 4096 and 1 <= height <= 4096 and 1 <= name_size <= 32768):
            raise ValueError("Invalid PNG job dimensions/path")
        path = bytes(ReadExact(source, name_size)).decode("utf-8")
        pixels = ReadExact(source, width * height * 4)
        stride = width * 4
        compressor = zlib.compressobj(1)
        with open(path + ".writing", "wb") as destination:
            destination.write(b"\x89PNG\r\n\x1a\n")
            WriteChunk(destination, b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            for row in range(height):
                # PNG filter None avoids per-pixel Python loops; compression cost stays in this process.
                compressed = compressor.compress(b"\0" + pixels[row * stride:(row + 1) * stride])
                if compressed:
                    WriteChunk(destination, b"IDAT", compressed)
            WriteChunk(destination, b"IDAT", compressor.flush())
            WriteChunk(destination, b"IEND", b"")
        os.replace(path + ".writing", path)
        with open(path + ".done", "wb"):
            pass


if __name__ == "__main__":
    Run()
