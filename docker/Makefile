SRC_DIR=`pwd`/../
BUILD_DIR=/build
PORT?=/dev/ttyUSB0

all:
	@echo "Build targets: "
	@echo "build-docker			Build an image for building tools with Docker"
	@echo "build    			Build the Alles firmware in the Docker environment"
	@echo "flash    			Flash an Alles device. Try 'PORT=/dev/xxx make flash' to specify a serial device"
	@echo "run				(for debugging) Run a shell using the above image with Docker"

build-docker:
	docker build -t alles-builder -f Dockerfile .

build:
	docker run \
        --rm \
		--mount type=bind,source=${SRC_DIR},target=${BUILD_DIR} \
		-w ${BUILD_DIR} \
		-it alles-builder \
        /usr/bin/bash -c ". /esp-idf/export.sh; idf.py fullclean build"

flash: 
	docker run \
        --rm \
		--mount type=bind,source=${SRC_DIR},target=${BUILD_DIR} \
        --device=${PORT} \
		-w ${BUILD_DIR} \
		-it alles-builder \
        /usr/bin/bash -c ". /esp-idf/export.sh; idf.py flash -p ${PORT}"
run:
	docker run \
		--rm \
		--mount type=bind,source=${SRC_DIR},target=${BUILD_DIR} \
		-w ${BUILD_DIR} \
		-it alles-builder \
		/usr/bin/bash

