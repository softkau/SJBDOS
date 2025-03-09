#!/bin/sh -ex

if [ $# -lt 1 ]
then
    echo "Usage: $0 <image name>"
    exit 1
fi

DEVENV_DIR=$(dirname "$0")
DISK_IMG=$1

#... or STD
TYPE="STD"

if [ ! -f $DISK_IMG ]
then
    echo "No such file: $DISK_IMG"
    exit 1
fi

OPT=" -monitor stdio"

if [ "$TYPE" = "DAEMON" ]
then
    OPT=" -daemonize"
fi

qemu-system-x86_64 $DBG_OPTS \
    -m 2G \
    -drive if=pflash,format=raw,readonly,file=$DEVENV_DIR/OVMF_CODE.fd \
    -drive if=pflash,format=raw,file=$DEVENV_DIR/OVMF_VARS.fd \
    -drive if=ide,index=0,media=disk,format=raw,file=$DISK_IMG \
    -device nec-usb-xhci,id=xhci \
    -device usb-mouse -device usb-kbd \
    $OPT \
    # -vnc :1 \
    $QEMU_OPTS
