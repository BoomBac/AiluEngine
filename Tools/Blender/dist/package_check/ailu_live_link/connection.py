import queue
import socket
import threading
import time

from . import protocol


class LinkConnection:
    def __init__(self):
        self._outgoing = queue.Queue(maxsize=4096)
        self._state = queue.SimpleQueue()
        self._stop = threading.Event()
        self._thread = None
        self._sequence = 1
        self.status = "Disconnected"
        self.last_error = ""
        self._queued_bytes = 0
        self._overflow = False
        self._queue_lock = threading.Lock()
        self._socket_lock = threading.Lock()
        self._active_socket = None

    @property
    def connected(self):
        return self.status == "Connected"

    @property
    def queued_bytes(self):
        with self._queue_lock:
            return self._queued_bytes

    def connect(self, host, port, auto_reconnect=True):
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, args=(host, port, auto_reconnect), daemon=False)
        self._thread.start()

    def disconnect(self):
        self._stop.set()
        try:
            self._outgoing.put_nowait(None)
        except queue.Full:
            pass
        with self._socket_lock:
            if self._active_socket:
                try:
                    self._active_socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        if self._thread:
            self._thread.join()
            self._thread = None
        self._set_status("Disconnected")

    def send(self, message_type, payload=b""):
        if self._overflow:
            return False
        data = protocol.packet(message_type, self._sequence, payload)
        self._sequence += 1
        key = self._coalesce_key(data)
        with self._outgoing.mutex, self._queue_lock:
            if key:
                for index, queued in enumerate(self._outgoing.queue):
                    if queued is not None and self._coalesce_key(queued) == key:
                        self._outgoing.queue[index] = data
                        self._queued_bytes += len(data) - len(queued)
                        self._outgoing.not_empty.notify()
                        return True
            if message_type == protocol.OBJECT_DELETE:
                import json
                guid = json.loads(payload.decode("utf-8")).get("guid", "").encode("utf-8")
                for index in range(len(self._outgoing.queue) - 1, -1, -1):
                    queued = self._outgoing.queue[index]
                    queued_type = int.from_bytes(queued[6:8], "little") if queued else 0
                    queued_payload = queued[protocol.HEADER.size:] if queued else b""
                    queued_key = self._coalesce_key(queued) if queued else None
                    if queued_type in {protocol.OBJECT_UPSERT, protocol.MESH_UPSERT,
                            protocol.LIGHT_UPSERT, protocol.CAMERA_UPSERT} and queued_key:
                        if queued_key[1].strip(b'"') == guid:
                            self._queued_bytes -= len(queued)
                            del self._outgoing.queue[index]
            if self._queued_bytes + len(data) > 256 * 1024 * 1024 or self._outgoing._qsize() >= 4096:
                self._outgoing.queue.clear()
                self._queued_bytes = 0
                self._overflow = True
                self.last_error = "Send queue full; reconnect and perform Full Sync"
                self._outgoing.queue.append(None)
                self._outgoing.not_empty.notify()
                with self._socket_lock:
                    if self._active_socket:
                        try:
                            self._active_socket.shutdown(socket.SHUT_RDWR)
                        except OSError:
                            pass
                return False
            self._queued_bytes += len(data)
            self._outgoing.queue.append(data)
            self._outgoing.not_empty.notify()
            return True

    def send_json(self, message_type, value):
        import json
        return self.send(message_type, json.dumps(value, separators=(",", ":")).encode("utf-8"))

    def poll(self):
        while True:
            try:
                self.status, self.last_error = self._state.get_nowait()
            except queue.Empty:
                break

    def _set_status(self, status, error=""):
        self._state.put((status, error))

    def _drain_outgoing(self):
        with self._outgoing.mutex, self._queue_lock:
            self._outgoing.queue.clear()
            self._queued_bytes = 0

    @staticmethod
    def _coalesce_key(data):
        if len(data) < protocol.HEADER.size:
            return None
        message_type = int.from_bytes(data[6:8], "little")
        payload = data[protocol.HEADER.size:]
        if message_type == protocol.MESH_UPSERT and len(payload) >= 36:
            return message_type, payload[:36]
        if message_type not in {protocol.OBJECT_UPSERT, protocol.MATERIAL_UPSERT,
                protocol.LIGHT_UPSERT, protocol.CAMERA_UPSERT}:
            return None
        marker = b'"material_guid"' if message_type == protocol.MATERIAL_UPSERT else b'"guid"'
        start = payload.find(marker)
        if start < 0:
            return None
        colon = payload.find(b":", start + len(marker))
        first = payload.find(b'"', colon + 1) if colon >= 0 else -1
        end = payload.find(b'"', first + 1) if first >= 0 else -1
        return (message_type, payload[first + 1:end]) if end >= 0 else None

    def _run(self, host, port, auto_reconnect):
        delay = 0.25
        while not self._stop.is_set():
            sock = None
            try:
                self._set_status("Connecting")
                sock = socket.create_connection((host, port), timeout=3.0)
                with self._socket_lock:
                    self._active_socket = sock
                sock.settimeout(1.0)
                sock.sendall(protocol.packet(protocol.HELLO, self._sequence, b'{"client":"Blender"}'))
                self._sequence += 1
                kind, _, payload = protocol.recv_packet(sock)
                if kind != protocol.HELLO_ACK or b'"accepted":true' not in payload:
                    raise ValueError("Ailu rejected protocol v1")
                self._set_status("Connected")
                self._overflow = False
                delay = 0.25
                while not self._stop.is_set():
                    try:
                        data = self._outgoing.get(timeout=0.1)
                    except queue.Empty:
                        continue
                    if data is None:
                        break
                    with self._queue_lock:
                        self._queued_bytes = max(0, self._queued_bytes - len(data))
                    sock.sendall(data)
            except Exception as error:
                self._set_status("Disconnected", str(error))
                self._drain_outgoing()
                if not auto_reconnect:
                    break
                self._stop.wait(delay)
                delay = min(delay * 2.0, 5.0)
            finally:
                if sock:
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    sock.close()
                with self._socket_lock:
                    self._active_socket = None
        self._set_status("Disconnected", self.last_error)


connection = LinkConnection()
