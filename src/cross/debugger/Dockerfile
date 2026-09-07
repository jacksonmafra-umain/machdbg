FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y software-properties-common && \
    add-apt-repository ppa:ubuntu-toolchain-r/test && \
    apt-get update && apt-get install -y --no-install-recommends \
    gcc-13 g++-13 \
    make \
    ninja-build \
    git \
    wget \
    file \
    fuse \
    libfuse2 \
    qt5-default \
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