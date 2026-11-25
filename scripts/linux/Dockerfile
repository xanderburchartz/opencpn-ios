# Bouwomgeving voor OpenCPN en plugins op Debian 13 (trixie), dezelfde release als de navpi.
# Op een Apple-chip draait dit native als linux/arm64.
# De bouwpakketten komen uit de Build-Depends van ci/control van OpenCPN (master), die
# scripts/build-linux.sh als control naast dit bestand zet.
FROM debian:trixie

ARG DEBIAN_FRONTEND=noninteractive
COPY control /tmp/control

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build ccache git gettext file pkg-config \
      ca-certificates python3 patchelf xz-utils \
 && deps="$(awk '/^Build-Depends:/{f=1;sub(/^Build-Depends:/,"");print;next} f&&/^[A-Za-z-]+:/{f=0} f' /tmp/control | tr -d '\n')" \
 && apt-get satisfy -y --no-install-recommends "$deps" \
 && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/ccache
WORKDIR /work
