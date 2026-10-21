#!/usr/bin/env python3
"""Expose physical cFS logs and UDP links to the SHIRE lab runner."""
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import uuid


def receive(stream, size):
    result = bytearray()
    while len(result) < size:
        block = stream.recv(size - len(result))
        if not block:
            raise RuntimeError("Board UDP tunnel closed")
        result.extend(block)
    return bytes(result)


def main():
    def terminate(_signum, _frame):
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, terminate)
    directory = Path('/run/shire-board')
    tunnel_port = int(os.environ.get('SHIRE_BOARD_TUNNEL', '50198'))
    command_port = int(os.environ.get('SHIRE_BOARD_COMMAND', '1234'))
    telemetry_port = int(os.environ.get('SHIRE_BOARD_TELEMETRY', '1235'))
    ground = os.environ.get('SHIRE_BOARD_GSW_HOST', 'shire-gsw')
    run_id = uuid.uuid4().hex
    request = directory / 'request.json'
    temporary = directory / f'{run_id}.json'
    with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('0.0.0.0', tunnel_port))
        listener.listen(1)
        udp.bind(('0.0.0.0', command_port))
        temporary.write_text(json.dumps({'run_id': run_id,
            'profile': os.environ.get('SHIRE_BARRIER_PROFILE', '0')}))
        temporary.replace(request)
        listener.setblocking(False)
        udp.setblocking(False)
        stream = None
        log = None
        try:
            while True:
                path = directory / f'{run_id}.log'
                if log is None and path.exists():
                    log = path.open('rb')
                if log:
                    data = log.read()
                    if data:
                        os.write(1, data)
                exit_file = directory / f'{run_id}.exit'
                if exit_file.exists():
                    raise SystemExit(int(exit_file.read_text()))
                readable, _, _ = select.select([listener, udp] + ([stream] if stream else []), [], [], .01)
                if listener in readable:
                    if stream:
                        raise RuntimeError('Duplicate Board UDP tunnel')
                    stream, _ = listener.accept()
                    stream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    stream.settimeout(5)
                if udp in readable:
                    packet, _ = udp.recvfrom(65507)
                    if not stream:
                        raise RuntimeError('Command received before Board UDP tunnel was ready')
                    stream.sendall(struct.pack('!I', len(packet)) + packet)
                if stream and stream in readable:
                    size, = struct.unpack('!I', receive(stream, 4))
                    if size > 65507:
                        raise RuntimeError('Oversized Board UDP tunnel frame')
                    udp.sendto(receive(stream, size), (ground, telemetry_port))
        finally:
            (directory / f'{run_id}.stop').touch()
            if stream:
                stream.close()
            if log:
                log.close()


if __name__ == '__main__':
    main()
