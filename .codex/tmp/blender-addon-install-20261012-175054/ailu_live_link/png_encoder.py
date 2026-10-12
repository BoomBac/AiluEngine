"""FullSync-owned encoder process with bounded pipe backpressure and ready-file publication."""
import os
import struct
import subprocess
import time
from pathlib import Path

import bpy
import numpy as np
from . import protocol


class PngEncoder:
    @staticmethod
    def PythonPath():
        version = f"{bpy.app.version[0]}.{bpy.app.version[1]}"
        return Path(bpy.app.binary_path).parent / version / "python/bin/python.exe"

    @staticmethod
    def IsAvailable():
        return os.name == "nt" and PngEncoder.PythonPath().is_file()

    def __init__(self, directory):
        python = self.PythonPath()
        if not python.is_file():
            raise RuntimeError("Diagnostic PNG worker requires Blender's bundled Python executable")
        self._log = open(os.path.join(directory, "png_worker.log"), "wb")
        try:
            self._process = subprocess.Popen([str(python), str(Path(__file__).with_name("png_worker.py"))],
                                             stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=self._log,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
        except OSError:
            self._log.close()
            raise
        self._paths = set()
        self._pending = []
        self._submit_ms = 0.0
        self._wait_ms = 0.0

    def SendPixels(self, path, pixels):
        started = time.perf_counter()
        if self._process.poll() is not None:
            raise RuntimeError("PNG encoder exited; see png_worker.log")
        Path(path + ".done").unlink(missing_ok=True)
        name = path.encode("utf-8")
        height, width = pixels.shape[:2]
        rgba = np.floor(np.clip(pixels[::-1], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        self._process.stdin.write(struct.pack("<III", width, height, len(name)))
        self._process.stdin.write(name)
        self._process.stdin.write(rgba.tobytes())
        self._process.stdin.flush()
        self._paths.add(path)
        self._submit_ms += (time.perf_counter() - started) * 1000
        return path

    def QueueMaterial(self, value, link):
        self._pending.append((value, link))
        self.Drain()

    def Drain(self, wait=False):
        started = time.perf_counter()
        deadline = time.monotonic() + 120
        while self._pending:
            value, link = self._pending[0]
            paths = [path for name, path in value.items() if name.endswith("_path") and path in self._paths]
            if all(os.path.isfile(path + ".done") for path in paths):
                link.send_json(protocol.MATERIAL_UPSERT, value)
                self._pending.pop(0)
            elif self._process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("PNG worker failed or timed out; see png_worker.log")
            elif wait:
                time.sleep(0.001)
            else:
                break
        if wait:
            self._wait_ms += (time.perf_counter() - started) * 1000

    def Close(self):
        try:
            try:
                self._process.stdin.close()
            except BrokenPipeError:
                pass
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.terminate()
                self._process.wait(timeout=10)
        finally:
            self._log.close()
            for path in self._paths:
                Path(path + ".done").unlink(missing_ok=True)
                Path(path + ".writing").unlink(missing_ok=True)
            print(f"[BlenderLiveLink] PNG worker: images={len(self._paths)} "
                  f"submit={self._submit_ms:.2f}ms wait={self._wait_ms:.2f}ms")
