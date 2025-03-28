# Usage: source buildenv.sh

BASEDIR="$(dirname "$(realpath "$0")")"
echo "BASEDIR=${BASEDIR}"
LIBCDIR="${BASEDIR}/x86_64-elf"
EDK2DIR="${BASEDIR}/edk2"

if [ ! -d $LIBCDIR ]
then
    echo "$LIBCDIRが存在しません。"
    echo "以下のファイルを手動でダウンロードし、$(dirname $LIBCDIR)に展開してください。"
    echo "https://github.com/uchan-nos/mikanos-build/releases/download/v2.0/x86_64-elf.tar.gz "
else
    export CPPFLAGS="\
    -I$LIBCDIR/include/c++/v1 -I$LIBCDIR/include -I$LIBCDIR/include/freetype2 \
    -I$EDK2DIR/MdePkg/Include -I$EDK2DIR/MdePkg/Include/X64 \
    -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS \
    -DEFIAPI='__attribute__((ms_abi))'"
    export LDFLAGS="-L$LIBCDIR/lib"
fi

