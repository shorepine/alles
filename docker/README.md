# Docker container for Alles production

These scripts build the Alles firmwware in a container, using a fully isloated
ESP-IDF toolchain.

To build it, you'll need a Linux computer with [Docker](https://docs.docker.com/engine/install/ubuntu/) installed. Any OS capable of running Docker can work to build the image, however a Linux-based system is required to flash devices through the container.

You will aslo need make and git; you can install them on Ubuntu like so:

    sudo apt update
    sudo apt install build-essential git

Once you have these prerequisites, clone the repository, then build the Docker image:

    git clone https://github.com/blinkinlabs/alles.git
    cd alles/docker
    make build-docker

This will take a little while. Once it is done, you can build the firmware in the container:

    make build

Once the firmware is built, flash an attached Alles device:

    make flash

Note: The above assumes that the USB-to-serial converter in the Alles was detected as /dev/ttyUSB0; if it was mounted somewhere else, you can specify the position:

    PORT=/dev/ttyXXXX make flash
