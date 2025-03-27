#!/bin/sh -ex

DEVENV_DIR="$(dirname $0)"
MOUNT_POINT=./mnt

if [ "$DISK_IMG" = "" ]
then
	DISK_IMG=./sjbdos.img
fi

if [ "$SJBDOS_DIR" = "" ]
then
	if [ $# -lt 1 ]
	then
		echo "Usage: $0 <dayXX>"
		exit 1
	fi
	$SJBDOS_DIR="$HOME/osbook/$1"
fi

LOADER_EFI="$SJBDOS_DIR/../devenv/edk2/Build/SJBDLoaderX64/DEBUG_CLANG38/X64/Loader.efi"
KERNEL_ELF="$SJBDOS_DIR/kernel/build/kernel.elf"

$DEVENV_DIR/make_image.sh $DISK_IMG $MOUNT_POINT $LOADER_EFI $KERNEL_ELF
$DEVENV_DIR/mount_image.sh $DISK_IMG $MOUNT_POINT

if [ "$APPS_DIR" != "" ]
then
	sudo mkdir $MOUNT_POINT/$APPS_DIR
fi

for APP in $(ls "$SJBDOS_DIR/apps")
do
	if [ -f $SJBDOS_DIR/apps/$APP/$APP ]
	then
		sudo cp "$SJBDOS_DIR/apps/$APP/$APP" $MOUNT_POINT/$APPS_DIR
	fi
done

if [ "$RESOURCE_DIR" != "" ]
then
	sudo cp $SJBDOS_DIR/$RESOURCE_DIR/* $MOUNT_POINT/
fi

sleep 0.5
sudo umount $MOUNT_POINT
