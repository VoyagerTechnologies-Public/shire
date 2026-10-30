#!/usr/bin/env python3
"""Inject per-run configuration into an immutable Zynq initramfs and boot it."""
import gzip
import io
import os
from pathlib import Path
import shlex
import socket
import tempfile


def overlay_archive(root):
    """Create a root-owned newc archive without needing root or device access."""
    result = io.BytesIO()
    paths = sorted(root.rglob("*"))
    for index, path in enumerate([*paths, None], 1):
        name = str(path.relative_to(root)) if path else "TRAILER!!!"
        data = path.read_bytes() if path and path.is_file() else b""
        mode = path.stat().st_mode if path else 0
        encoded = name.encode() + b"\0"
        fields = [index, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        header = b"070701" + b"".join(f"{value:08x}".encode() for value in fields)
        result.write(header + encoded)
        result.write(b"\0" * (-(len(header) + len(encoded)) % 4))
        result.write(data)
        result.write(b"\0" * (-len(data) % 4))
    return result.getvalue()


def main():
    with tempfile.TemporaryDirectory(prefix="shire-qemu-") as temporary:
        root = Path(temporary)
        (root / "etc/init.d").mkdir(parents=True)
        hosts = "127.0.0.1 localhost\n"
        for name in ("shire-server", "shire-director", "shire-gsw", "shire-cryptolib"):
            hosts += f"{socket.gethostbyname(name)} {name}\n"
        (root / "etc/hosts").write_text(hosts)
        names = ("SIMULITH_PUB_ADDR", "SIMULITH_REP_ADDR", "SIMULITH_SYNC_TRANSPORT",
                 "SIMULITH_DEVICE_TRANSPORT", "SIMULITH_DEVICE_HOST", "SIMULITH_LOG_MODE",
                 "SHIRE_GROUND_OUTPUT_HZ", "SHIRE_SYNCHRONIZE_GROUND_OUTPUT", "SHIRE_BARRIER_PROFILE")
        (root / "etc/shire-env").write_text("\n".join(
            f"export {name}={shlex.quote(os.environ[name])}" for name in names if name in os.environ) + "\n")
        initramfs_init = root / "init"
        initramfs_init.write_text("#!/bin/sh\nmount -t devtmpfs devtmpfs /dev || exit 1\nexec /sbin/init\n")
        initramfs_init.chmod(0o755)
        init = root / "etc/init.d/S99shire"
        init.write_text("""#!/bin/sh
case "$1" in
  start)
    mkdir -p /dev/mqueue
    mount -t mqueue none /dev/mqueue
    sysctl -w fs.mqueue.msg_max=10000
    . /etc/shire-env
    export LD_LIBRARY_PATH=/opt/shire/app
    cd /opt/shire/app || exit 1
    echo 'SHIRE_QEMU_START armv7l-shire'
    ./core-cpu1 &
    echo $! > /run/shire.pid
    ;;
  stop) test ! -f /run/shire.pid || kill "$(cat /run/shire.pid)" ;;
esac
""")
        init.chmod(0o755)
        # Linux accepts concatenated compressed initramfs archives. The second
        # archive replaces only runtime configuration; the base stays immutable.
        ramdisk = Path("/tmp/shire-rootfs.cpio.gz")
        with ramdisk.open("wb") as target:
            target.write(Path("/boot/rootfs.cpio.gz").read_bytes())
            target.write(gzip.compress(overlay_archive(root), mtime=0))
    command = ["qemu-system-arm", "-M", "xilinx-zynq-a9", "-m", "1024",
               "-display", "none", "-serial", "null", "-serial", "mon:stdio",
               "-kernel", "/boot/uImage", "-dtb", "/boot/zynq-zc702.dtb",
               "-initrd", str(ramdisk), "-append", "console=ttyPS0,115200 rdinit=/init",
               "-netdev", "user,id=net0,hostfwd=udp::1234-:1234,hostfwd=tcp::2222-:22",
               "-net", "nic,netdev=net0"]
    print("[qemu] booting open-source Zynq Linux / ARM32 SHIRE", flush=True)
    os.execvp(command[0], command)


if __name__ == "__main__":
    main()
