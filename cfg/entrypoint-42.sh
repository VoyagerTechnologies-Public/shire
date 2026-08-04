#!/bin/bash

# Clean up stale VNC lock files from any previous unclean shutdown
rm -f /tmp/.X1-lock /tmp/.X11-unix/X1 ~/.vnc/*.pid 2>/dev/null || true

mkdir -p ~/.vnc

# ensure default VNC password is set
if [ -n "$VNC_PASSWORD" ]; then
    echo "$VNC_PASSWORD" | vncpasswd -f > ~/.vnc/passwd
else
    echo "0123456789" | vncpasswd -f > ~/.vnc/passwd
fi
chmod 600 ~/.vnc/passwd

/opt/TurboVNC/bin/vncserver -securitytypes tlsnone,x509none,none

# alter the below script to invoke different applications
/startapp.sh &

APP_PID=$!

# Handle shutdown signals
cleanup() {
    echo "[entrypoint] Caught signal, shutting down..."

    # Kill app process group
    kill -TERM "$APP_PID" 2>/dev/null || true

    # Stop VNC server
    /opt/TurboVNC/bin/vncserver -kill :1 2>/dev/null || true

    wait "$APP_PID" 2>/dev/null || true
    echo "[entrypoint] Shutdown complete"
    exit 0
}

trap cleanup SIGTERM SIGINT

echo "[entrypoint] Starting noVNC (websockify)..."

# Run websockify in the background so the shell stays as PID 1 and can
# handle SIGTERM to clean up the VNC server before exit.
websockify --web=/usr/share/novnc/ --cert="$HOME/novnc.pem" 80 localhost:5901 &
WEBSOCKIFY_PID=$!

wait $WEBSOCKIFY_PID
