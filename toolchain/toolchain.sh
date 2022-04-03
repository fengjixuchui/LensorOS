#!/bin/bash

#     Dependencies:
#     |-- Curl
#     |-- GNU Diff/Patch
#     |-- GNU Make
#     |-- GNU Tar (xz-utils)
#     `-- x86_64-linux-gnu compiler

# Set variables.
ScriptDirectory="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TARGET="x86_64-lensor"
PREFIX="$ScriptDirectory/cross"
SYSROOT="$ScriptDirectory/../root"
# Ensure known working directory (assuming script wasn't moved).
cd $ScriptDirectory
# Download, extract, and patch source archives if they haven't been already.
if [ ! -d "binutils-2.38" ]; then
    echo -e "\n\n -> Downloading GNU Binutils Source Archive\n\n"
    curl https://mirrors.kernel.org/gnu/binutils/binutils-2.38.tar.xz \
         --output binutils-2.38.tar.xz
    echo -e "\n\n -> Extracting GNU Binutils\n\n"
    mkdir -p "binutils-2.38"
    tar -xf binutils-2.38.tar.xz -C .
    echo -e "\n\n -> Patching GNU Binutils\n\n"
    patch -s -u -p0 < $ScriptDirectory/binutils-2.38-lensor.patch
else
	echo -e "\n\n -> Using existing GNU Binutils Source\n\n"
fi
if [ ! -d "gcc-11.2.0" ]; then
    echo -e "\n\n -> Downloading GNU Compiler Collection Source Archive\n\n"
    curl https://mirrors.kernel.org/gnu/gcc/gcc-11.2.0/gcc-11.2.0.tar.xz \
         --output gcc-11.2.0.tar.xz
    echo -e "\n\n -> Extracting GNU Compiler Collection\n\n"
    mkdir -p "gcc-11.2.0"
    tar -xf gcc-11.2.0.tar.xz -C .
    echo -e "\n\n -> Downloading GNU Compiler Collection Prerequisites\n\n"
    cd gcc-11.2.0
    ./contrib/download_prerequisites
    cd $ScriptDirectory
    echo -e "\n\n -> Patching GNU Compiler Collection\n\n"
    patch -s -u -p0 < $ScriptDirectory/gcc-11.2.0-lensor.patch
else
	echo -e "\n\n -> Using existing GNU Compiler Collection Source\n\n"
fi
if [ ! -d "$SYSROOT" ] ; then
    echo -e "\n\n -> Bootstrapping System Root at $SYSROOT\n\n"
    mkdir -p "$SYSROOT"
    # Copy base filesystem with pre-built libc
    #     binaries into newly-created sysroot.
    # This solves the issue of having to build a
    #     bootstrap version of the compiler first.
    cp -r ../base/* "$SYSROOT/"
    # Copy header files from libc to sysroot.
    cd ../user/libc/
    find ./ -name '*.h' -exec cp --parents '{}' -t $SYSROOT/inc ';'
    cd $ScriptDirectory
else
	echo -e "\n\n -> Using existing System Root at $SYSROOT\n\n"
fi
# Create output build directory.
mkdir -p cross
if [ ! -d "binutils-build" ] ; then
    echo -e "\n\n -> Configuring GNU Binutils\n\n"
    mkdir -p binutils-build
    cd binutils-build
    $ScriptDirectory/binutils-2.38/configure  \
        --target=$TARGET                      \
        --prefix="$PREFIX"                    \
        --with-sysroot="$SYSROOT"             \
        --disable-nls                         \
        --disable-werror
    # Build binutils.
    echo -e "\n\n -> Building & Installing GNU Binutils\n\n"
    make -j
    make install -j
else
	echo -e "\n\n -> Using existing build of GNU Binutils\n\n"
fi
if [ ! -d "gcc-build" ] ; then
    echo -e "\n\n -> Configuring GNU Compiler Collection\n\n"
    cd $ScriptDirectory
    mkdir -p gcc-build
    cd gcc-build
    $ScriptDirectory/gcc-11.2.0/configure     \
        --target=$TARGET                      \
        --prefix="$PREFIX"                    \
        --disable-nls                         \
        --enable-languages=c,c++              \
        --with-sysroot="$SYSROOT"
    # Build GCC.
    echo -e "\n\n -> Building & Installing GNU Compiler Collection\n\n"
    make all-gcc -j
    make all-target-libgcc -j
    make install-gcc -j
    make install-target-libgcc -j
else
	echo -e "\n\n -> Using existing build of GNU Compiler Collection\n\n"
fi

