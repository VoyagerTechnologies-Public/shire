#!/bin/sh
# Run only inside unshare --mount --ipc --pid --fork --kill-child=TERM.
set -eu
root=$1
run_id=$2
profile=$3
. "$root/etc/shire-board-env"
mount --make-rprivate /
mount -t proc proc "$root/proc"
mount -t tmpfs -o mode=755 tmpfs "$root/dev"
mknod -m 666 "$root/dev/null" c 1 3
mknod -m 666 "$root/dev/zero" c 1 5
mknod -m 666 "$root/dev/random" c 1 8
mknod -m 666 "$root/dev/urandom" c 1 9
mkdir -p "$root/dev/shm" "$root/dev/mqueue"
mount -t tmpfs -o mode=1777 tmpfs "$root/dev/shm"
mount -t mqueue none "$root/dev/mqueue"
# This sysctl belongs to the new IPC namespace, not the board's namespace.
echo "$SHIRE_BOARD_MQUEUE_MAX" > "$root/proc/sys/fs/mqueue/msg_max"
if command -v prlimit >/dev/null 2>&1; then
    exec prlimit --msgqueue=unlimited:unlimited --rtprio="$SHIRE_BOARD_RTPRIO:$SHIRE_BOARD_RTPRIO" -- \
        chroot "$root" /bin/sh /run-shire-inner.sh "$run_id" "$profile"
fi
# Retain support for board images whose shell provides these limits but whose
# util-linux installation omits prlimit. Debian dash does not support -q.
if ! (ulimit -q && ulimit -r) >/dev/null 2>&1; then
    echo 'Board runtime requires prlimit or a shell supporting ulimit -q and -r' >&2
    exit 1
fi
ulimit -q unlimited
ulimit -r "$SHIRE_BOARD_RTPRIO"
exec chroot "$root" /bin/sh /run-shire-inner.sh "$run_id" "$profile"
