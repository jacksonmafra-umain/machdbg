# Ubuntu 24.04 LTS: 20.04 left standard support in May 2025 and ships libbrotli 1.0.7,
# which never received the CVE-2026-111497 fix. GCC 13 is in the 24.04 archive, so the
# toolchain PPA is no longer needed; qt5-default was dropped after 20.04 (qtbase5-dev
# provides the same tools) and libfuse2 became libfuse2t64 with the 64-bit time_t move.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    gcc-13 g++-13 \
    make \
    ninja-build \
    git \
    wget \
    file \
    fuse \
    libfuse2t64 \
    qtbase5-dev \
    qttools5-dev \
    qttools5-dev-tools \
    libqt5svg5-dev \
    libqt5websockets5-dev \
    libxcb1-dev \
    libqt5svg5-dev \
    libxcb1-dev \
    libxcb-xinerama0 \
    libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

# Point to GCC 13 by default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Install newer CMake
RUN wget -q "https://github.com/Kitware/CMake/releases/download/v3.31.11/cmake-3.31.11-linux-x86_64.sh" \
    -O /tmp/cmake.sh && \
    chmod +x /tmp/cmake.sh && \
    /tmp/cmake.sh --skip-license --prefix=/usr/local && \
    rm /tmp/cmake.sh

# linuxdeploy + Qt plugin
RUN wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    -O /usr/local/bin/linuxdeploy && chmod +x /usr/local/bin/linuxdeploy

RUN wget -q "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
    -O /usr/local/bin/linuxdeploy-plugin-qt && chmod +x /usr/local/bin/linuxdeploy-plugin-qt

ENV APPIMAGE_EXTRACT_AND_RUN=1

WORKDIR /build