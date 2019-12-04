FROM gitpod/workspace-full

RUN sudo apt-get update \
 && sudo apt-get install -y \
    libtool libusb-1.0-0-dev librtlsdr-dev rtl-sdr build-essential autoconf cmake pkg-config \
 && sudo rm -rf /var/lib/apt/lists/*


