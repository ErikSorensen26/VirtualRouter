FROM mcr.microsoft.com/devcontainers/cpp:debian-12

# Install additional packages needed for the development environment
RUN apt-get update && apt-get -y install --no-install-recommends \
    vim \
    clang \
    iputils-ping \
    libpcap-dev \
    libx11-dev \
    wireshark \
    tcpdump \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean
