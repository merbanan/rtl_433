#!/bin/bash

set -e

libusb_ver=1.0.25
rtlsdr_ver=0.6.0
pothos_ver=2021.07.25-vc16

[ "$(uname)" = "Darwin" ] && export tools=/opt/local

# prefer GNU commands
realpath=$(command -v grealpath || :)
realpath="${realpath:-realpath}"
sed=$(command -v gsed || :)
sed="${sed:-sed}"

# from https://libusb.info/
if [ ! -e libusb/include/libusb-1.0/libusb.h ]
then
[ -e libusb-${libusb_ver}.7z ] || curl -L -O https://github.com/libusb/libusb/releases/download/v${libusb_ver}/libusb-${libusb_ver}.7z
mkdir -p libusb
7z x -olibusb -y libusb-${libusb_ver}.7z
fi

source_dir=$(dirname $($realpath -s $0))
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

if [ ! -d rtl-sdr-${rtlsdr_ver} ]
then
# or git clone https://github.com/osmocom/rtl-sdr.git
[ -e rtl-sdr-${rtlsdr_ver}.tar.gz ] || curl -L -o rtl-sdr-${rtlsdr_ver}.tar.gz https://github.com/osmocom/rtl-sdr/archive/${rtlsdr_ver}.tar.gz
tar xzf rtl-sdr-${rtlsdr_ver}.tar.gz
fi

cd rtl-sdr-${rtlsdr_ver}

if [ ! -e $sysroot32/usr/lib/librtlsdr.a ]
then
export CMAKE_SYSROOT=$sysroot32 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-i686.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32/usr/lib/librtlsdr_static.a $sysroot32/usr/lib/librtlsdr.a
fi

if [ ! -e $sysroot32static/usr/lib/librtlsdr.a ]
then
export CMAKE_SYSROOT=$sysroot32static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-i686.cmake -DBUILD_SHARED_LIBS:BOOL=OFF .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32static/usr/lib/librtlsdr_static.a $sysroot32static/usr/lib/librtlsdr.a
rm $sysroot32static/usr/lib/librtlsdr.dll.a
rm $sysroot32static/usr/bin/librtlsdr.dll
fi

if [ ! -e $sysroot64/usr/lib/librtlsdr.a ]
then
export CMAKE_SYSROOT=$sysroot64 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-x86-64.cmake .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64/usr/lib/librtlsdr_static.a $sysroot64/usr/lib/librtlsdr.a
fi

if [ ! -e $sysroot64static/usr/lib/librtlsdr.a ]
then
export CMAKE_SYSROOT=$sysroot64static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-x86-64.cmake -DBUILD_SHARED_LIBS:BOOL=OFF .. && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64static/usr/lib/librtlsdr_static.a $sysroot64static/usr/lib/librtlsdr.a
rm $sysroot64static/usr/lib/librtlsdr.dll.a
rm $sysroot64static/usr/bin/librtlsdr.dll
fi

cd ..

if [ ! -e $sysroot64/usr/bin/SoapySDR.dll -o ! -e $sysroot64/usr/lib/SoapySDR.lib ]
then
# from https://downloads.myriadrf.org/builds/PothosSDR/
[ -e PothosSDR-${pothos_ver}-x64.exe ] || curl -L -O https://downloads.myriadrf.org/builds/PothosSDR/PothosSDR-${pothos_ver}-x64.exe
mkdir -p pothos
7z x -opothos -y PothosSDR-${pothos_ver}-x64.exe
# workaround: 7-Zip 9.20 creates strange root directories
[ -e pothos/bin ] || mv pothos/*/* pothos/ || :
cp pothos/bin/SoapySDR.dll $sysroot64/usr/bin
cp -R pothos/include/SoapySDR $sysroot64/usr/include
cp pothos/lib/SoapySDR.lib $sysroot64/usr/lib
cp -R pothos/cmake $sysroot64/usr
$sed -i 's/.*INTERFACE_COMPILE_OPTIONS.*//g' $sysroot64/usr/cmake/SoapySDRExport.cmake
fi

# build rtl_433

export CMAKE_SYSROOT=$sysroot32 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-i686.cmake $source_dir && make && make install ; cd ..
rm -rf build-tmp
# Non-static 32-bit binary from w64 compiler is broken with
# missing libgcc_s_sjlj-1.dll, libwinpthread-1.dll
# neither CMAKE_EXE_LINKER_FLAGS="-static-libgcc"
# nor target_link_libraries(rtl_433 -static-libgcc)
# fix this. Ideas welcome.
mv $sysroot32/usr/bin/rtl_433.exe $sysroot32/usr/bin/rtl_433_32bit_nonstatic_broken.exe

export CMAKE_SYSROOT=$sysroot32static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-i686.cmake $source_dir && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot32static/usr/bin/rtl_433.exe $sysroot32static/usr/bin/rtl_433_32bit_static.exe

export CMAKE_SYSROOT=$sysroot64 ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DENABLE_SOAPYSDR=ON -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-x86-64.cmake $source_dir && make && make install ; cd ..
rm -rf build-tmp

export CMAKE_SYSROOT=$sysroot64static ; echo $CMAKE_SYSROOT
mkdir build-tmp ; cd build-tmp ; cmake -DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/Toolchain-gcc-mingw-w64-x86-64.cmake $source_dir && make && make install ; cd ..
rm -rf build-tmp
mv $sysroot64static/usr/bin/rtl_433.exe $sysroot64static/usr/bin/rtl_433_64bit_static.exe

# collect package

cp $source_dir/.deploy/WINDOWS-MINGWW64.txt README.txt

echo Packing rtl_433-win-x32.zip
zip --junk-paths rtl_433-win-x32.zip sysroot32*/usr/bin/*.dll sysroot32*/usr/bin/rtl_433*.exe README.txt

echo Packing rtl_433-win-x64.zip
zip --junk-paths rtl_433-win-x64.zip sysroot64*/usr/bin/*.dll sysroot64*/usr/bin/rtl_433*.exe README.txt
