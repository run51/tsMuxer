#!/bin/sh
# Started by the base image inside the web/VNC desktop session.
export HOME=/config

# First start only: the interface language (TSMUXER_LANGUAGE: "en", the default, or "fr") and the
# video shares as output folder. Settings changed in the GUI afterwards are kept in /config.
CONF_DIR="${XDG_CONFIG_HOME:-/config/xdg/config}/tsMuxeR"
if [ ! -f "$CONF_DIR/tsMuxeR.conf" ]; then
    case "$TSMUXER_LANGUAGE" in
        fr*|Fr*|FR*) LANGUAGE_NAME='Fran\303\247ais' ;;
        *) LANGUAGE_NAME='English' ;;
    esac
    mkdir -p "$CONF_DIR"
    printf "[main]\nlanguage=$LANGUAGE_NAME\noutputDir=/storage\n" > "$CONF_DIR/tsMuxeR.conf"
fi

cd /storage 2>/dev/null || cd /config
exec /usr/bin/tsMuxeR
