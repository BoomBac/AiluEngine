import queue
import socket
import threading
import time
import errno
import select

from . import protocol


class LinkConnection:
    def __init__(self):
        self._outgoing = queue.Queue(maxsize=4096)
        self._running = False
        self._connecting = False
        self._host = ""
        self._port = 0
        self._auto_reconnect = False
        self._retry_at = 0.0
        self._retry_delay = 0.25
        self._deadline = 0.0
        self._incoming = bytearray()
        self._wire_packet = None
        self._wire_offset = 0
        self._wire_hello = False
        self._wire_started = None
        self._wire_send_ms = 0.0
        self._sequence = 1
        self.status = "Disconnected"
        self.last_error = ""
        self._queued_bytes = 0
        self._overflow = False
        self._queue_lock = threading.Lock()
        self._active_socket = None
        self._diagnostic_lock = threading.Lock()
        self._diagnostic_batch_id = 0
        self._diagnostic_active_batch = None
        self._diagnostic_batches = {}
        self._diagnostic_packets = {}

    @property
    def connected(self):
        return self.status == "Connected"

    @property
    def queued_bytes(self):
        with self._queue_lock:
            return self._queued_bytes

    def connect(self, host, port, auto_reconnect=True):
        if self._running:
            return
        self._host, self._port = host, port
        self._auto_reconnect = auto_reconnect
        self._running = True
        self._retry_at = 0.0
        self._retry_delay = 0.25
        self.poll()

    def disconnect(self):
        self._running = False
        self._close_socket()
        self._drain_outgoing()
        self._set_status("Disconnected")

    def send(self, message_type, payload=b""):
        # Full Sync can occupy one timer callback for a long time. Advance transport between generated packets.
        self.poll()
        if self._overflow:
            raise ConnectionError(self.last_error or "Send queue full; reconnect and perform Full Sync")
        data = protocol.packet(message_type, self._sequence, payload)
        if not self._wait_for_send_capacity(len(data)):
            raise ConnectionError(self.last_error or "Ailu disconnected while sending")
        self._sequence += 1
        enqueue_time = time.perf_counter()
        key = self._coalesce_key(data)
        with self._outgoing.mutex, self._queue_lock:
            if key:
                for index, queued in enumerate(self._outgoing.queue):
                    if queued is not None and self._coalesce_key(queued) == key:
                        self._drop_diagnostic_packet(queued)
                        self._outgoing.queue[index] = data
                        self._queued_bytes += len(data) - len(queued)
                        self._track_diagnostic_packet(data, enqueue_time)
                        self._outgoing.not_empty.notify()
                        return True
            if message_type == protocol.OBJECT_DELETE:
                import json
                guid = json.loads(payload.decode("utf-8")).get("guid", "").encode("utf-8")
                for index in range(len(self._outgoing.queue) - 1, -1, -1):
                    queued = self._outgoing.queue[index]
                    queued_type = int.from_bytes(queued[6:8], "little") if queued else 0
                    queued_key = self._coalesce_key(queued) if queued else None
                    if queued_type in {protocol.OBJECT_UPSERT, protocol.MESH_UPSERT,
                            protocol.MESH_PROTOTYPE_UPSERT,
                            protocol.LIGHT_UPSERT, protocol.CAMERA_UPSERT} and queued_key:
                        if queued_key[1].strip(b'"') == guid:
                            self._drop_diagnostic_packet(queued)
                            self._queued_bytes -= len(queued)
                            del self._outgoing.queue[index]
            if self._queued_bytes + len(data) > 256 * 1024 * 1024 or self._outgoing._qsize() >= 4096:
                for queued in self._outgoing.queue:
                    self._drop_diagnostic_packet(queued)
                self._outgoing.queue.clear()
                self._queued_bytes = 0
                self._overflow = True
                self.last_error = "Send queue full; reconnect and perform Full Sync"
                with self._diagnostic_lock:
                    if self._diagnostic_active_batch is not None:
                        batch = self._diagnostic_batches.get(self._diagnostic_active_batch)
                        if batch:
                            batch["failed"] = True
                self._drop_diagnostic_packet(self._wire_packet)
                self._close_socket()
                self._set_status("Disconnected", self.last_error)
                self._running = False
                raise ConnectionError(self.last_error)
            self._queued_bytes += len(data)
            self._outgoing.queue.append(data)
            self._track_diagnostic_packet(data, enqueue_time)
            self._outgoing.not_empty.notify()
            return True

    def _wait_for_send_capacity(self, packet_bytes):
        max_bytes = 256 * 1024 * 1024
        while self._running and self.connected and not self._overflow:
            with self._outgoing.mutex, self._queue_lock:
                if self._outgoing._qsize() < 4096 and self._queued_bytes + packet_bytes <= max_bytes:
                    return True
            self.poll()
            if self._running and self.connected and not self._overflow:
                time.sleep(0.001)
        return False

    def begin_diagnostic_batch(self, name):
        with self._diagnostic_lock:
            self._diagnostic_batch_id += 1
            batch_id = self._diagnostic_batch_id
            self._diagnostic_batches[batch_id] = {
                "name": name, "pending": 0, "sent_messages": 0, "sent_bytes": 0,
                "queue_wait_ms": 0.0, "queue_wait_max_ms": 0.0,
                "send_ms": 0.0, "send_max_ms": 0.0, "coalesced": 0,
                "types": {}, "generation_ms": 0.0, "phases": {}, "ended": False, "failed": False,
            }
            self._diagnostic_active_batch = batch_id
            return batch_id

    def end_diagnostic_batch(self, batch_id, generation_ms, phases, counts=None):
        with self._diagnostic_lock:
            batch = self._diagnostic_batches.get(batch_id)
            if batch is None:
                return
            batch["generation_ms"] = generation_ms
            batch["phases"] = phases
            batch["counts"] = counts or {}
            batch["ended"] = True
            if self._diagnostic_active_batch == batch_id:
                self._diagnostic_active_batch = None
            self._finish_diagnostic_batch(batch_id)

    def _track_diagnostic_packet(self, data, enqueue_time):
        sequence = int.from_bytes(data[12:20], "little")
        message_type = int.from_bytes(data[6:8], "little")
        with self._diagnostic_lock:
            batch_id = self._diagnostic_active_batch
            if batch_id is None:
                return
            batch = self._diagnostic_batches.get(batch_id)
            if batch is None:
                return
            self._diagnostic_packets[sequence] = (batch_id, enqueue_time, message_type, len(data))
            batch["pending"] += 1

    def _drop_diagnostic_packet(self, data):
        if data is None or len(data) < protocol.HEADER.size:
            return
        sequence = int.from_bytes(data[12:20], "little")
        with self._diagnostic_lock:
            packet = self._diagnostic_packets.pop(sequence, None)
            if packet:
                batch = self._diagnostic_batches.get(packet[0])
                if batch:
                    batch["pending"] = max(0, batch["pending"] - 1)
                    batch["coalesced"] += 1

    def _record_diagnostic_send(self, data, started, elapsed_ms):
        sequence = int.from_bytes(data[12:20], "little")
        with self._diagnostic_lock:
            packet = self._diagnostic_packets.pop(sequence, None)
            if not packet:
                return
            batch_id, enqueue_time, message_type, packet_bytes = packet
            batch = self._diagnostic_batches.get(batch_id)
            if not batch:
                return
            queue_wait_ms = max(0.0, (started - enqueue_time) * 1000.0)
            batch["pending"] = max(0, batch["pending"] - 1)
            batch["sent_messages"] += 1
            batch["sent_bytes"] += packet_bytes
            batch["queue_wait_ms"] += queue_wait_ms
            batch["queue_wait_max_ms"] = max(batch["queue_wait_max_ms"], queue_wait_ms)
            batch["send_ms"] += elapsed_ms
            batch["send_max_ms"] = max(batch["send_max_ms"], elapsed_ms)
            stats = batch["types"].setdefault(message_type, [0, 0])
            stats[0] += 1
            stats[1] += packet_bytes
            self._finish_diagnostic_batch(batch_id)

    def _finish_diagnostic_batch(self, batch_id):
        batch = self._diagnostic_batches.get(batch_id)
        if not batch or not batch["ended"] or batch["pending"]:
            return
        phase_text = " ".join(f"{name}={elapsed_ms:.2f}ms" for name, elapsed_ms in batch["phases"].items())
        count_text = " ".join(f"{name}={count}" for name, count in batch.get("counts", {}).items())
        type_text = " ".join(f"type{kind}={values[0]}/{values[1]}B" for kind, values in sorted(batch["types"].items()))
        print(
            f"[BlenderLiveLink] Send batch {batch['name']}: generation={batch['generation_ms']:.2f}ms "
            f"queue_wait_sum={batch['queue_wait_ms']:.2f}ms max_queue_wait={batch['queue_wait_max_ms']:.2f}ms "
            f"sendall_sum={batch['send_ms']:.2f}ms max_sendall={batch['send_max_ms']:.2f}ms "
            f"sent={batch['sent_messages']} bytes={batch['sent_bytes']} coalesced={batch['coalesced']} "
            f"status={'failed' if batch['failed'] else 'ok'} counts[{count_text}] "
            f"phases[{phase_text}] messages[{type_text}]"
        )
        del self._diagnostic_batches[batch_id]

    def send_json(self, message_type, value):
        import json
        return self.send(message_type, json.dumps(value, separators=(",", ":")).encode("utf-8"))

    def poll(self):
        if not self._running:
            return
        try:
            if self._active_socket is None:
                if time.monotonic() < self._retry_at:
                    return
                self._start_connect()
            sock = self._active_socket
            if self._connecting:
                _, writable, exceptional = select.select([], [sock], [sock], 0)
                if not writable and not exceptional:
                    if time.monotonic() >= self._deadline:
                        raise TimeoutError("Ailu connection timed out")
                    return
                error = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if error:
                    raise OSError(error, "Ailu connection failed")
                self._connecting = False
                hello = {"client": "Blender", "features": [
                    protocol.INSTANCE_BATCH_FEATURE, protocol.MESH_PROTOTYPE_FEATURE]}
                self._wire_packet = protocol.json_packet(protocol.HELLO, self._sequence, hello)
                self._sequence += 1
                self._wire_offset = 0
                self._wire_hello = True
            self._receive()
            if not self.connected and time.monotonic() >= self._deadline:
                raise TimeoutError("Ailu handshake timed out")
            self._flush()
        except (OSError, ValueError, ConnectionError) as error:
            self._close_socket()
            self._set_status("Disconnected", str(error))
            self._drain_outgoing()
            if self._auto_reconnect:
                self._retry_at = time.monotonic() + self._retry_delay
                self._retry_delay = min(self._retry_delay * 2.0, 5.0)
            else:
                self._running = False

    def _set_status(self, status, error=""):
        self.status, self.last_error = status, error

    def _drain_outgoing(self):
        with self._outgoing.mutex, self._queue_lock:
            self._outgoing.queue.clear()
            self._queued_bytes = 0
        with self._diagnostic_lock:
            self._diagnostic_packets.clear()
            self._diagnostic_active_batch = None
            for batch_id, batch in list(self._diagnostic_batches.items()):
                batch["pending"] = 0
                batch["ended"] = True
                batch["failed"] = True
                self._finish_diagnostic_batch(batch_id)

    @staticmethod
    def _coalesce_key(data):
        if len(data) < protocol.HEADER.size:
            return None
        message_type = int.from_bytes(data[6:8], "little")
        payload = data[protocol.HEADER.size:]
        if message_type in {protocol.MESH_UPSERT, protocol.MESH_PROTOTYPE_UPSERT} and len(payload) >= 36:
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

    def _close_socket(self):
        if self._active_socket:
            self._active_socket.close()
        self._active_socket = None
        self._connecting = False
        self._incoming.clear()
        self._wire_packet = None
        self._wire_offset = 0
        self._wire_hello = False
        self._wire_started = None
        self._wire_send_ms = 0.0

    def _start_connect(self):
        self._set_status("Connecting")
        address = socket.getaddrinfo(self._host, self._port, type=socket.SOCK_STREAM)[0]
        sock = socket.socket(address[0], address[1], address[2])
        self._active_socket = sock
        sock.setblocking(False)
        error = sock.connect_ex(address[4])
        if error not in {0, errno.EINPROGRESS, errno.EWOULDBLOCK, errno.EALREADY, 10035, 10036, 10037}:
            raise OSError(error, "Ailu connection failed")
        self._connecting = True
        self._deadline = time.monotonic() + 3.0

    def _receive(self):
        # Bound receive work per timer tick and parse fragmented protocol headers/payloads.
        for _ in range(16):
            try:
                data = self._active_socket.recv(65536)
            except BlockingIOError:
                break
            if not data:
                raise ConnectionError("Ailu closed the connection")
            self._incoming.extend(data)
        while len(self._incoming) >= protocol.HEADER.size:
            magic, version, kind, size, _ = protocol.HEADER.unpack_from(self._incoming)
            if magic != protocol.MAGIC or version != protocol.VERSION or size > 256 * 1024 * 1024:
                raise ValueError("Invalid Ailu Live Link packet")
            packet_size = protocol.HEADER.size + size
            if len(self._incoming) < packet_size:
                break
            payload = bytes(self._incoming[protocol.HEADER.size:packet_size])
            del self._incoming[:packet_size]
            if not self.connected:
                import json
                reply = json.loads(payload)
                if kind != protocol.HELLO_ACK or not isinstance(reply, dict) or reply.get("accepted") is not True:
                    raise ValueError("Ailu rejected protocol v1")
                features = reply.get("features", [])
                required_features = {protocol.INSTANCE_BATCH_FEATURE, protocol.MESH_PROTOTYPE_FEATURE}
                if not isinstance(features, list) or not required_features.issubset(features):
                    raise ValueError("Ailu plugin lacks Blender instance prototype support; update the Engine plugin")
                self._set_status("Connected")
                self._overflow = False
                self._retry_delay = 0.25

    def _flush(self):
        # A slow peer must never block Blender. Retain partial packets until the next poll.
        deadline = time.perf_counter() + 0.004
        remaining = 16 * 1024 * 1024
        while remaining > 0 and time.perf_counter() < deadline:
            if self._wire_packet is None:
                if not self.connected:
                    return
                try:
                    self._wire_packet = self._outgoing.get_nowait()
                except queue.Empty:
                    return
                self._wire_offset = 0
                self._wire_hello = False
                self._wire_started = None
                self._wire_send_ms = 0.0
                with self._queue_lock:
                    self._queued_bytes -= len(self._wire_packet)
            data = self._wire_packet
            started = time.perf_counter()
            try:
                end = self._wire_offset + min(remaining, 262144)
                sent = self._active_socket.send(memoryview(data)[self._wire_offset:end])
            except BlockingIOError:
                return
            if not sent:
                raise ConnectionError("Ailu closed the connection")
            if self._wire_started is None:
                self._wire_started = started
            self._wire_send_ms += (time.perf_counter() - started) * 1000.0
            self._wire_offset += sent
            remaining -= sent
            if self._wire_offset == len(data):
                if not self._wire_hello:
                    self._record_diagnostic_send(data, self._wire_started, self._wire_send_ms)
                self._wire_packet = None
                self._wire_offset = 0


connection = LinkConnection()
