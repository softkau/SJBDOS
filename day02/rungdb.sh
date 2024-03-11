#!/bin/sh -eu

gdb kernel/build/kernel.elf -ex "target remote localhost:1234"
#gdb apps/grep/grep -ex "target remote localhost:1234"
#gdb apps/more/more -ex "target remote localhost:1234"
