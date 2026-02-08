FROM aflplusplus/aflplusplus:latest

RUN apt-get update && apt-get install -y \
    python3 \
    python3-dev \
    python3-setuptools \
    libpcre3-dev \
    build-essential \
    gdb \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /klipper

CMD ["/bin/bash"]