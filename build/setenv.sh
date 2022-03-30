#!/bin/bash
export JRPC_PebbleFirmware=$PWD
export JRPC_PebbleFirmwareVersion=$1

export BUILD="west build -b thingy91_nrf9160ns"
export FIRMWARE_PATH=``

echo $JRPC_PebbleFirmware
