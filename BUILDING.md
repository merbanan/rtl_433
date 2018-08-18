# Building rtl_433

## Linux

Compiling rtl_433 requires [rtl-sdr](http://sdr.osmocom.org/trac/wiki/rtl-sdr) to be installed.

Depending on your system, you may also need to install the following libraries.

Debian:

    sudo apt-get install libtool libusb-1.0.0-dev librtlsdr-dev rtl-sdr build-essential autoconf cmake pkg-config

Centos/Fedora/RHEL (for Centos/RHEL with enabled EPEL):

    sudo dnf install libtool libusb-devel rtl-sdr-devel rtl-sdr

### CMake

Installation using cmake:

    cd rtl_433/
    mkdir build
    cd build
    cmake ../
    make
    make install

### autoconf

Installation using autoconf:

    cd rtl_433/
    autoreconf --install
    ./configure
    make
    make install

The final 'make install' step should be run as a user with appropriate permissions - if in doubt, 'sudo' it.

## Windows

### mingw-w64

You'll need librtlsdr and libusb, they are dependencies for rtl_433.

libusb has prebuilt binaries for windows, 
librtlsdr needs to be built (at least for the latest version, some binaries 
of older versions seem to be floating around)

#### librtlsdr

taken and adapted from here: https://www.onetransistor.eu/2017/03/compile-librtlsdr-windows-mingw.html

* install mingw-w64 (https://mingw-w64.org/) and cmake (https://cmake.org/)
    * it's easiest if you select the option to include cmake in your path, otherwise you'll need to do this manually
* download the libusb binaries from https://sourceforge.net/projects/libusb/files/libusb-1.0/ or from https://libusb.info/
    * take the latest release and then download the .7z file, the other file contains the sources (or 'windows binaries' on the .info website)
* extract the archive and open the extracted folder
* copy the contents of the include folder to `<mingw_installation_folder>/include`
* copy the `mingw64/dll/libusb-1.0.dll.a` file to `<mingw_installation_folder>/lib
* copy the `mingw64/dll/libusb-1.0.dll` file to `<mingw_installation_folder>/bin`
* download the source code of librtlsdr https://github.com/steve-m/librtlsdr
* go into the librtlsdr folder
* open cmakelists.txt with an editor that knows unix line endings
* go to `# Find build dependencies` (around line 65) and comment/remove the line with `find_package(Threads)`
* add the following lines instead:

```
SET(CMAKE_THREAD_LIBS_INIT "-lpthread")
SET(CMAKE_HAVE_THREADS_LIBRARY 1)
SET(THREADS_FOUND TRUE)
```

* go into the cmake/modules folder and open FindLibUSB.cmake with a text editor
* find the lines with the following text in them

```
/usr/include/libusb-1.0
/usr/include
/usr/local/include
```

* add some extra lines to point to the mingw include folder where you extracted libusb-1.0, making it look like this
    * take note of the "" around the folder names, these are needed when there are spaces in the folder name
    * you'll need to find out the exact paths for your system

```
/usr/include/libusb-1.0
/usr/include
/usr/local/include
"C:/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include"
"C:/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include/libusb-1.0"
```

* open a mingw terminal in the librtlsdr folder
* create build folder and go into it: `mkdir build && cd build`
* generate makefiles for mingw: `cmake -G "MinGW Makefiles" ..`
* build the librtlsdr library: `mingw32-make`

#### rtl_433

* clone the rtl_433 repository and cd into it
* create a build folder and go into it: `mkdir build && cd build`
* run `cmake -G "MinGW Makefiles" .. ` in the build directory
* run cmake-gui (this is easiest)
* set the source (the rtl_433 source code directory) and the build directory (one might create a build directory in the source directory)
* click configure
* select the grouped and advanced tickboxes
* go into the librtlsdr config group
* point the `LIBRTLSDR_INCLUDE_DIRS` to the include folder of the librtlsdr source
* point the `LIBRTLSDR_LIBRARIES` to the `librtlsdr.dll.a` file in the <librtlsdr_source>/build/src folder
    * that's the one you've built earlier
* edit `CMAKE_C_STANDARD_LIBRARIES` to include `-lws2_32` (this is for winsocks2)
* start a mingw terminal and run `mingw32-make` to build
    * when something in the tests folder doesn't build, you can disable it by commenting out `add_subdirectory(tests)` in the cmakelists.txt file in the source folder of rtl_433
* rtl_433.exe should be built now
* you need to place it in the same folder as librtlsdr.dll and libusb-1.0.dll (you should have seen both of them by now)
* good luck!
