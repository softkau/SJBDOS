#! /bin/sh

ln -s ../kernel/elf.h ./
ln -s ../kernel/frame_buffer_config.h ./
ln -s ../kernel/memmap.h ./
ln -s $(pwd) ../../devenv/edk2/

echo created symbolic links.