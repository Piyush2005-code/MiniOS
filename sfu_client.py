"""
sfu_client.py — Python host client for the MiniOS Simple Framed UDP (SFU)
inference server.

Wire format (all little-endian):
  Offset  Size  Field
  ------  ----  -----
   0       4    magic        0xDEAD6969
   4       1    version      0x01
   5       1    msg_type     (see SFU_MSG_* constants)
   6       2    flags        0x0000
   8       4    req_id
  12       4    seq_num      0
  16       4    total_seq    1
  20       2    checksum     CRC16-CCITT over payload only
  22       2    payload_len
  24       N    payload      raw float32 LE values

Dependencies: Python ≥ 3.8 stdlib + numpy only.
"""

import socket
import struct
import time
import numpy as np

# ---------------------------------------------------------------------------
#  Protocol constants (mirror sfu.h)
# ---------------------------------------------------------------------------

SFU_MAGIC   = 0xDEAD6969
SFU_VERSION = 0x01

# msg_type values
SFU_MSG_INFER_REQUEST  = 0x01
SFU_MSG_INFER_RESPONSE = 0x02
SFU_MSG_ACK            = 0x03
SFU_MSG_NACK           = 0x04
SFU_MSG_PING           = 0x05
SFU_MSG_PONG           = 0x06
SFU_MSG_CMD            = 0x07
SFU_MSG_CMD_RESPONSE   = 0x08
SFU_MSG_ERROR          = 0x10

_MSG_TYPE_NAMES = {
    SFU_MSG_INFER_REQUEST:  "INFER_REQUEST",
    SFU_MSG_INFER_RESPONSE: "INFER_RESPONSE",
    SFU_MSG_ACK:            "ACK",
    SFU_MSG_NACK:           "NACK",
    SFU_MSG_PING:           "PING",
    SFU_MSG_PONG:           "PONG",
    SFU_MSG_CMD:            "CMD",
    SFU_MSG_CMD_RESPONSE:   "CMD_RESPONSE",
    SFU_MSG_ERROR:          "ERROR",
}

# Header format string for struct (little-endian, 24 bytes total)
# < I B B H I I I H H
#   │ │ │ │ │ │ │ │ └ payload_len  uint16
#   │ │ │ │ │ │ │ └── checksum     uint16
#   │ │ │ │ │ │ └──── total_seq   uint32
#   │ │ │ │ │ └────── seq_num     uint32
#   │ │ │ │ └──────── req_id      uint32
#   │ │ │ └────────── flags       uint16
#   │ │ └──────────── msg_type    uint8
#   │ └────────────── version     uint8
#   └──────────────── magic       uint32
_HDR_FMT  = "<IBBHIIIHH"
_HDR_SIZE = struct.calcsize(_HDR_FMT)  # must be 24
assert _HDR_SIZE == 24, f"Header struct size is {_HDR_SIZE}, expected 24"


# ---------------------------------------------------------------------------
#  CRC16-CCITT
# ---------------------------------------------------------------------------

class CRC16:
    """CRC16-CCITT: polynomial 0x1021, initial value 0xFFFF.

    Matches the miniOS ``SFU_Checksum`` implementation exactly:
    bit-by-bit (no table), MSB-first shift register.
    """

    POLY = 0x1021
    INIT = 0xFFFF

    @staticmethod
    def compute(data: bytes) -> int:
        """Return the 16-bit CRC of *data*.

        Returns 0 for empty input (mirrors the C implementation which
        returns 0 when ``len == 0`` or ``payload == NULL``).
        """
        if not data:
            return 0

        crc = CRC16.INIT
        for byte in data:
            crc ^= (byte << 8)
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ CRC16.POLY) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc


# ---------------------------------------------------------------------------
#  SFU client
# ---------------------------------------------------------------------------

class SFUClient:
    """UDP client that speaks the Simple Framed UDP (SFU) protocol.

    Parameters
    ----------
    host : str
        Server address (default ``'127.0.0.1'``).
    port : int
        Server UDP port (default ``9000``).
    timeout : float
        Per-attempt socket receive timeout in seconds (default ``0.5``).
    retries : int
        Number of retransmit attempts before raising ``TimeoutError``
        (default ``3``).
    debug : bool
        When True, print every packet sent and received (default ``False``).
    skip_startup_ping : bool
        Skip the startup connectivity check.  Used internally during the
        retry loop to avoid infinite recursion (default ``False``).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 9000,
        timeout: float = 0.5,
        retries: int = 3,
        debug: bool = False,
        skip_startup_ping: bool = False,
    ):
        self.host    = host
        self.port    = port
        self.timeout = timeout
        self.retries = retries
        self.debug   = debug

        self._req_id: int = 0

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(self.timeout)

        if not skip_startup_ping:
            self._startup_check()

    # ------------------------------------------------------------------
    #  Internal helpers
    # ------------------------------------------------------------------

    def _startup_check(self, max_attempts: int = 10, delay: float = 1.0) -> None:
        """Attempt ping() up to *max_attempts* times before giving up.

        Prints "Waiting for miniOS…" on each retry so the caller knows
        QEMU hasn't finished booting yet.
        """
        for attempt in range(1, max_attempts + 1):
            try:
                rtt = self.ping()
                if self.debug:
                    print(f"[startup] connected after {attempt} attempt(s), RTT={rtt:.2f} ms")
                return
            except TimeoutError:
                if attempt < max_attempts:
                    print(f"Waiting for miniOS... (attempt {attempt}/{max_attempts})")
                    time.sleep(delay)
        raise TimeoutError(
            f"miniOS did not respond after {max_attempts} ping attempts "
            f"({self.host}:{self.port})"
        )

    def _next_req_id(self) -> int:
        """Return next request ID, wrapping at 2^32."""
        rid = self._req_id
        self._req_id = (self._req_id + 1) & 0xFFFFFFFF
        return rid

    def _pack_header(self, msg_type: int, req_id: int, payload: bytes) -> bytes:
        """Serialize a 24-byte SFU header with correct CRC16 checksum."""
        checksum    = CRC16.compute(payload)
        payload_len = len(payload)
        return struct.pack(
            _HDR_FMT,
            SFU_MAGIC,   # magic
            SFU_VERSION, # version
            msg_type,    # msg_type
            0x0000,      # flags (no fragmentation)
            req_id,      # req_id
            0,           # seq_num
            1,           # total_seq
            checksum,    # checksum
            payload_len, # payload_len
        )

    def _unpack_header(self, data: bytes) -> dict:
        """Deserialize and validate a 24-byte SFU header.

        Returns
        -------
        dict with keys: magic, version, msg_type, flags, req_id,
                        seq_num, total_seq, checksum, payload_len,
                        checksum_ok

        Raises
        ------
        ValueError
            If *magic* does not match ``SFU_MAGIC`` or the CRC16 over
            the trailing payload bytes does not match the header's
            *checksum* field.
        """
        if len(data) < _HDR_SIZE:
            raise ValueError(
                f"Packet too short: {len(data)} bytes (minimum {_HDR_SIZE})"
            )

        (magic, version, msg_type, flags,
         req_id, seq_num, total_seq,
         checksum, payload_len) = struct.unpack_from(_HDR_FMT, data, 0)

        if magic != SFU_MAGIC:
            raise ValueError(
                f"Bad magic: 0x{magic:08X} (expected 0x{SFU_MAGIC:08X})"
            )

        payload_bytes = data[_HDR_SIZE : _HDR_SIZE + payload_len]
        computed      = CRC16.compute(payload_bytes)
        checksum_ok   = (computed == checksum)

        if not checksum_ok:
            raise ValueError(
                f"CRC16 mismatch: got 0x{computed:04X}, "
                f"header says 0x{checksum:04X}"
            )

        return {
            "magic":       magic,
            "version":     version,
            "msg_type":    msg_type,
            "flags":       flags,
            "req_id":      req_id,
            "seq_num":     seq_num,
            "total_seq":   total_seq,
            "checksum":    checksum,
            "payload_len": payload_len,
            "checksum_ok": checksum_ok,
        }

    def _debug_send(self, msg_type: int, req_id: int, payload: bytes) -> None:
        if not self.debug:
            return
        name = _MSG_TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
        print(
            f">> SEND type=0x{msg_type:02X} ({name}) "
            f"req_id={req_id} payload={len(payload)} bytes"
        )

    def _debug_recv(self, hdr: dict, payload: bytes) -> None:
        if not self.debug:
            return
        mt   = hdr["msg_type"]
        name = _MSG_TYPE_NAMES.get(mt, f"0x{mt:02X}")
        csum_status = "OK" if hdr["checksum_ok"] else "FAIL"
        print(
            f"<< RECV type=0x{mt:02X} ({name}) "
            f"req_id={hdr['req_id']} payload={len(payload)} bytes "
            f"checksum={csum_status}"
        )

    def _send_recv(
        self,
        msg_type: int,
        payload: bytes,
        req_id: int | None = None,
    ) -> dict:
        """Send one SFU packet and wait for a reply with a matching req_id.

        Retries up to ``self.retries`` times on timeout.

        Parameters
        ----------
        msg_type : int
            ``SFU_MSG_*`` constant for the outgoing packet.
        payload : bytes
            Raw payload bytes.
        req_id : int or None
            If ``None`` a new ID is minted; otherwise the provided value
            is used (useful for retransmits).

        Returns
        -------
        dict with keys ``'header'`` (parsed header dict) and
        ``'payload'`` (raw payload bytes).

        Raises
        ------
        TimeoutError
            If all retries are exhausted without a matching reply.
        """
        if req_id is None:
            req_id = self._next_req_id()

        header_bytes = self._pack_header(msg_type, req_id, payload)
        packet       = header_bytes + payload

        last_exc: Exception = TimeoutError("unknown")

        for attempt in range(1, self.retries + 1):
            try:
                self._sock.sendto(packet, (self.host, self.port))
                self._debug_send(msg_type, req_id, payload)

                # Drain packets until we get one with our req_id
                deadline = time.monotonic() + self.timeout
                while time.monotonic() < deadline:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    self._sock.settimeout(remaining)
                    try:
                        data, _ = self._sock.recvfrom(65535)
                    except socket.timeout:
                        break

                    try:
                        hdr = self._unpack_header(data)
                    except ValueError:
                        # Corrupted or alien packet — discard and keep waiting
                        continue

                    resp_payload = data[_HDR_SIZE : _HDR_SIZE + hdr["payload_len"]]
                    self._debug_recv(hdr, resp_payload)

                    if hdr["req_id"] == req_id:
                        # Skip intermediate ACK/NACK — keep waiting for the
                        # substantive response (PONG, INFER_RESPONSE, ERROR)
                        if hdr["msg_type"] in (SFU_MSG_ACK, SFU_MSG_NACK):
                            continue
                        return {"header": hdr, "payload": resp_payload}
                    # else: belongs to a different req_id, keep draining

                raise socket.timeout("timed out waiting for reply")

            except socket.timeout as exc:
                last_exc = exc
                if self.debug:
                    print(
                        f"[retry] attempt {attempt}/{self.retries} timed out "
                        f"for req_id={req_id}"
                    )

        raise TimeoutError(
            f"No reply from {self.host}:{self.port} after {self.retries} "
            f"attempt(s) for req_id={req_id}"
        ) from last_exc

    # ------------------------------------------------------------------
    #  Public API
    # ------------------------------------------------------------------

    def ping(self) -> float:
        """Send a PING and return the round-trip time in milliseconds.

        Raises
        ------
        TimeoutError
            If no PONG arrives within the configured timeout × retries.
        """
        t0  = time.perf_counter()
        rsp = self._send_recv(SFU_MSG_PING, b"")
        rtt = (time.perf_counter() - t0) * 1000.0

        if rsp["header"]["msg_type"] != SFU_MSG_PONG:
            raise RuntimeError(
                f"Expected PONG (0x{SFU_MSG_PONG:02X}), "
                f"got 0x{rsp['header']['msg_type']:02X}"
            )
        return rtt

    def infer(self, inputs: np.ndarray) -> np.ndarray:
        """Run inference on *inputs* and return the result as float32.

        Parameters
        ----------
        inputs : np.ndarray
            Input tensor.  Will be cast to ``float32`` automatically.

        Returns
        -------
        np.ndarray
            Output tensor (``float32``), shaped from the raw response bytes.

        Raises
        ------
        RuntimeError
            If miniOS returns an ERROR packet.
        TimeoutError
            If no response arrives within timeout × retries.
        """
        arr     = np.asarray(inputs, dtype=np.float32)
        payload = arr.tobytes()  # little-endian on any LE host; explicit below
        # Ensure we always send true LE regardless of host endianness
        payload = arr.astype("<f4").tobytes()

        rsp = self._send_recv(SFU_MSG_INFER_REQUEST, payload)
        hdr = rsp["header"]

        if hdr["msg_type"] == SFU_MSG_ERROR:
            # Payload may contain an error string or be empty
            msg = rsp["payload"].decode("ascii", errors="replace").strip("\x00")
            raise RuntimeError(
                f"miniOS returned ERROR: {msg!r}" if msg else "miniOS returned ERROR"
            )

        if hdr["msg_type"] != SFU_MSG_INFER_RESPONSE:
            raise RuntimeError(
                f"Unexpected msg_type 0x{hdr['msg_type']:02X} "
                f"(expected INFER_RESPONSE 0x{SFU_MSG_INFER_RESPONSE:02X})"
            )

        resp_bytes = rsp["payload"]
        if len(resp_bytes) % 4 != 0:
            raise RuntimeError(
                f"Response payload length {len(resp_bytes)} is not a multiple of 4"
            )

        return np.frombuffer(resp_bytes, dtype="<f4").copy()

    def cmd(self, command: str) -> str:
        """Send a text-based command and return the string response.

        Parameters
        ----------
        command : str
            The command string (e.g. "LIST_MODELS").

        Returns
        -------
        str
            The text response from miniOS.

        Raises
        ------
        RuntimeError
            If miniOS returns an ERROR or NACK.
        TimeoutError
            If no response arrives within timeout × retries.
        """
        payload = command.encode("ascii")
        rsp = self._send_recv(SFU_MSG_CMD, payload)
        hdr = rsp["header"]

        if hdr["msg_type"] == SFU_MSG_ERROR:
            msg = rsp["payload"].decode("ascii", errors="replace").strip("\x00")
            raise RuntimeError(f"miniOS returned ERROR: {msg}" if msg else "miniOS returned ERROR")

        if hdr["msg_type"] != SFU_MSG_CMD_RESPONSE:
            # Check for NACK (common if cmd is unknown)
            if hdr["msg_type"] == SFU_MSG_NACK:
                raise RuntimeError(f"miniOS NACKed command: {command!r}")
            raise RuntimeError(
                f"Unexpected msg_type 0x{hdr['msg_type']:02X} "
                f"(expected CMD_RESPONSE 0x{SFU_MSG_CMD_RESPONSE:02X})"
            )

        return rsp["payload"].decode("ascii", errors="replace").strip("\x00")

    def close(self) -> None:
        """Close the underlying UDP socket."""
        self._sock.close()

    # ------------------------------------------------------------------
    #  Context manager support
    # ------------------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
