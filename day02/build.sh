#!/bin/sh -eu

make ${MAKE_OPTS:-} -C kernel ./build/kernel.elf

#APPS_DIR=apps
#RESOURCE_DIR=resource

for MK in $(ls apps/*/Makefile)
do
	APP_DIR=$(dirname $MK)
	APP=$(basename $APP_DIR)
	make ${MAKE_OPTS:-} -C $APP_DIR $APP
done

if [ "${1:-}" = "run" ]
then
	SJBDOS_DIR=$PWD $HOME/osbook/devenv/run_sjbdos.sh
elif [ "${1:-}" = "debug" ]
then
	#DBG_OPTS="-s -S -d int -no-reboot -no-shutdown"
	DBG_OPTS="-s -S"
	DBG_OPTS=$DBG_OPTS SJBDOS_DIR=$PWD $HOME/osbook/devenv/run_sjbdos.sh
fi
