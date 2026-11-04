# tsMuxeR (single executable with Dolby Vision splitting and mux queue) with its
# regular desktop GUI shown in a web browser, plus MediaInfo, for NAS use.
#
#   docker build -f docker/Dockerfile -t tsmuxer-dv .
#   docker run -p 5800:5800 -v /path/to/config:/config -v /path/to/videos:/storage tsmuxer-dv
#
# then open http://<host>:5800

# ---- build stage -----------------------------------------------------------
FROM debian:13 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git ca-certificates \
        zlib1g-dev libfreetype-dev libgl-dev \
        qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTSMUXER_UNIFIED=ON \
        -DQT_VERSION=6 \
    && cmake --build /build \
    && strip /build/tsMuxerGUI/tsMuxeR

# ---- runtime stage ---------------------------------------------------------
# jlesage/baseimage-gui runs a desktop application and serves it to a web
# browser (port 5800) and to VNC clients (port 5900).
FROM jlesage/baseimage-gui:debian-13-v4

# The base image only creates /etc/passwd and /etc/group (links to /tmp) when
# the container starts, and links /var/log to /config/log: provide minimal ones
# during the build so package scripts (chown root:staff, adduser...) succeed.
# adwaita-qt6 is the Qt style the base image selects when DARK_MODE=1 (GTK apps
# such as MediaInfo switch to Adwaita:dark on their own).
# dbus-x11 provides the D-Bus session GTK (MediaInfo) needs, instead of the
# default dbus-user-session which would drag systemd into the container.
RUN printf 'root:x:0:\nstaff:x:50:\nusers:x:100:\nnogroup:x:65534:\n' > /tmp/.group \
    && printf 'root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n' > /tmp/.passwd \
    && printf 'root:*:19000:0:99999:7:::\nnobody:*:19000:0:99999:7:::\n' > /tmp/.shadow \
    && mkdir -p /config/log \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get update && apt-get install -y --no-install-recommends \
        libqt6widgets6 libqt6gui6 libqt6core6 qt6-qpa-plugins qt6-translations-l10n \
        adwaita-qt6 \
        libfreetype6 zlib1g \
        dbus-x11 mediainfo-gui \
        fonts-liberation fonts-dejavu-core \
        locales \
    && sed -i 's/^# *\(fr_FR.UTF-8\)/\1/' /etc/locale.gen && locale-gen \
    && rm -rf /var/lib/apt/lists/* /tmp/.group /tmp/.passwd /tmp/.shadow /config/log

COPY --from=build /build/tsMuxerGUI/tsMuxeR /usr/bin/tsMuxeR
COPY docker/startapp.sh /startapp.sh
COPY tsMuxerGUI/images/icon.png /tmp/icon.png
RUN chmod +x /startapp.sh \
    && install_app_icon.sh /tmp/icon.png && rm /tmp/icon.png \
    && set-cont-env APP_NAME "tsMuxeR" \
    && set-cont-env APP_VERSION "dv"

# UTF-8 locale so accented file names on the NAS are handled correctly
ENV LANG=fr_FR.UTF-8 \
    LC_ALL=fr_FR.UTF-8

# /config: settings of tsMuxeR and MediaInfo; /storage: the NAS video shares
VOLUME ["/config", "/storage"]
