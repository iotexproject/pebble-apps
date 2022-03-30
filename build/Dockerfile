FROM ubuntu:bionic
ENV DEBIAN_FRONTEND noninteractive
RUN rm /bin/sh && ln -s /bin/bash /bin/sh

RUN /build/kitware-archive.sh

RUN apt install --no-install-recommends git cmake ninja-build gperf \
    ccache dfu-util device-tree-compiler wget \
    python3-dev python3-pip xz-utils file \
    make gcc gcc-multilib g++-multilib libsdl2-dev -y

RUN wget https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu-rm/9-2020q2/gcc-arm-none-eabi-9-2020-q2-update-x86_64-linux.tar.bz2 && \
    tar -jxvf gcc-arm-none-eabi-9-2020-q2-update-x86_64-linux.tar.bz2 && \
    mv gcc-arm-none-eabi-9-2020-q2-update gnuarmemb && \
    rm gcc-arm-none-eabi-9-2020-q2-update-x86_64-linux.tar.bz2

RUN wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.14.0/zephyr-sdk-0.14.0_linux-aarch64.tar.gz -P /app/zephyr && \
    tar -xvf /app/zephyr/zephyr-sdk-0.14.0_linux-aarch64.tar.gz -C /app/zephyr && \
    rm zephyr-sdk-0.14.0_linux-aarch64.tar.gz

RUN python3 -m pip install --upgrade pip setuptools tk wheel && \
    pip3 install west

RUN source ~/.bashrc

WORKDIR /app/pebble-firmware/ncs

RUN west init -m https://github.com/nrfconnect/sdk-nrf --mr v1.4.0 && \
    west update && \
    west zephyr-export

RUN pip3 install -r zephyr/scripts/requirements.txt && \
    pip3 install -r nrf/scripts/requirements.txt && \
    pip3 install -r bootloader/mcuboot/scripts/requirements.txt

RUN wget https://launchpad.net/ubuntu/+source/device-tree-compiler/1.4.7-1/+build/15279267/+files/device-tree-compiler_1.4.7-1_amd64.deb && \
    apt install ./device-tree-compiler_1.4.7-1_amd64.deb && \
    rm ./device-tree-compiler_1.4.7-1_amd64.deb

RUN rm -rf /app/pebble-firmware/ncs/nrf/boards/arm/thingy91_nrf9160 && \
    cp -rv /app/pebble-firmware-legacy/nrf/boards/arm/thingy91_nrf9160 /app/pebble-firmware/ncs/nrf/boards/arm/

RUN source /app/pebble-firmware/ncs/zephyr/zephyr-env.sh

#RUN /zephyr/zephyr.run

WORKDIR /app/pebble-firmware/ncs


# CMD [ "west", "build", "-b", "thingy91_nrf9160ns", "/app/pebble-firmware-legacy/nrf/applications/Aries" ]
# CMD [ "west", "build", "-b", "thingy91_nrf9160ns", "/app/pebble-firmware-legacy/nrf/applications/Aries" ]

# RUN west build -b thingy91_nrf9160ns /app/pebble-firmware-legacy/nrf/applications/asset_tracker/
#/app/pebble-firmware-legacy/nrf/applications/asset_tracker/
#/app/pebble-firmware/nrf/applications/Aries