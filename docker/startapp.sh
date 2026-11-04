#!/bin/sh
# Started by the base image inside the web/VNC desktop session.
export HOME=/config

# First start only: French interface and the video shares as output folder.
# Settings changed in the GUI afterwards are kept in /config.
CONF_DIR="${XDG_CONFIG_HOME:-/config/xdg/config}/tsMuxeR"
if [ ! -f "$CONF_DIR/tsMuxeR.conf" ]; then
    mkdir -p "$CONF_DIR"
    printf '[main]\nlanguage=Fran\303\247ais\noutputDir=/storage\n' > "$CONF_DIR/tsMuxeR.conf"
fi

cd /storage 2>/dev/null || cd /config
exec /usr/bin/tsMuxeR
