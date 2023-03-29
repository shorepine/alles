FROM docker.io/library/ubuntu:22.10

RUN apt-get -qq update -y
RUN DEBIAN_FRONTEND=noninteractive \
       apt-get install -y --no-install-recommends \
            build-essential \
            python3-pip \
            cmake \
            git \
            libusb-1.0.0-dev \
            vim

RUN git clone -b v4.4 --depth 1 --recursive https://github.com/espressif/esp-idf.git /esp-idf
WORKDIR /esp-idf
RUN ./install.sh esp32
