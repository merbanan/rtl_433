#!/bin/bash

set -e

libusb_ver=1.0.22
rtlsdr_ver=0.6.0

# from https://libusb.info/
[ -e libusb-${libusb_ver}.7z ] || curl -L -O https://github.com/libusb/libusb/releases/download/v${libusb_ver}/libusb-${libusb_ver}.7z
mkdir libusb
7zr x -olibusb -y libusb-${libusb_ver}.7z

sysroot32=$(pwd)/sysroot32
sysroot64=$(pwd)/sysroot64
sysroot32static=$(pwd)/sysroot32static
sysroot64static=$(pwd)/sysroot64static

mkdir -p sysroot{32,64}{,static}/usr/{include,lib,bin}

cp libusb/include/libusb-1.0/libusb.h $sysroot32/usr/include
cp libusb/include/libusb-1.0/libusb.h $sysroot64/usr/include
cp libusb/include/libusb-1.0/libusb.h $sysroot32static/usr/include
cp libusb/include/libusb-1.0/libusb.h $sysroot64static/usr/include

cp libusb/MinGW32/static/libusb-1.0.a $sysroot32static/usr/lib
cp libusb/MinGW64/static/libusb-1.0.a $sysroot64static/usr/lib

cp libusb/MinGW32/dll/libusb-1.0.dll $sysroot32/usr/bin
cp libusb/MinGW32/dll/libusb-1.0.dll.a $sysroot32/usr/lib
cp libusb/MinGW64/dll/libusb-1.0.dll $sysroot64/usr/bin
cp libusb/MinGW64/dll/libusb-1.0.dll.a $sysroot64/usr/lib

# or git clone https://github.com/osmocom/rtl-sdr.git
[ -e rtl-sdr-${rtlsdr_ver}.tar.gz ] || curl -L -o rtl-sdr-${rtlsdr_ver}.tar.gz https://github.com/osmocom/rtl-sdr/archive/${rtlsdr_ver}.tar.gz
tar xzf rtl-sdr-${rtlsdr_ver}.tar.gz
cd rtl-sdr-${rtlsdr_ver}

[ "$(uname)" = "Darwin" ] && export tools=/opt/local

export CMAKE_SYSROOT=$sysroot32 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-gcc-mingw-w64-i686.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32/usr/lib/librtlsdr_static.a $sysroot32/usr/lib/librtlsdr.a

export CMAKE_SYSROOT=$sysroot32static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-gcc-mingw-w64-i686.cmake -DBUILD_SHARED_LIBS:BOOL=OFF .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32static/usr/lib/librtlsdr_static.a $sysroot32static/usr/lib/librtlsdr.a
rm $sysroot32static/usr/lib/librtlsdr.dll.a
rm $sysroot32static/usr/bin/librtlsdr.dll

export CMAKE_SYSROOT=$sysroot64 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-gcc-mingw-w64-x86-64.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64/usr/lib/librtlsdr_static.a $sysroot64/usr/lib/librtlsdr.a

export CMAKE_SYSROOT=$sysroot64static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-gcc-mingw-w64-x86-64.cmake -DBUILD_SHARED_LIBS:BOOL=OFF .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64static/usr/lib/librtlsdr_static.a $sysroot64static/usr/lib/librtlsdr.a
rm $sysroot64static/usr/lib/librtlsdr.dll.a
rm $sysroot64static/usr/bin/librtlsdr.dll

cd ..

# build rtl_433

export CMAKE_SYSROOT=$sysroot32 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-gcc-mingw-w64-i686.cmake .. && make && make install ; cd ..
rm -rf build-tmp

export CMAKE_SYSROOT=$sysroot32static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-gcc-mingw-w64-i686.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32static/usr/bin/rtl_433.exe $sysroot32static/usr/bin/rtl_433_32bit_static.exe

export CMAKE_SYSROOT=$sysroot64 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-gcc-mingw-w64-x86-64.cmake .. && make && make install ; cd ..
rm -rf build-tmp

export CMAKE_SYSROOT=$sysroot64static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-gcc-mingw-w64-x86-64.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64static/usr/bin/rtl_433.exe $sysroot64static/usr/bin/rtl_433_64bit_static.exe

# collect package

echo Packing rtl_433-win-x32.zip
zip --junk-paths rtl_433-win-x32.zip sysroot32*/usr/bin/*.dll sysroot32*/usr/bin/rtl_433*.exe

echo Packing rtl_433-win-x64.zip
zip --junk-paths rtl_433-win-x64.zip sysroot64*/usr/bin/*.dll sysroot64*/usr/bin/rtl_433*.exe
