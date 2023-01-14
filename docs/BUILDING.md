# Building rtl_433

rtl_433 currently supports these input types:
* [RTL-SDR](http://sdr.osmocom.org/trac/wiki/rtl-sdr) (optional, recommended)
* [SoapySDR](https://github.com/pothosware/SoapySDR/wiki) (optional)
* files: CU8, CS16, CF32 I/Q data, U16 AM data (built-in)
* rtl_tcp remote data servers (built-in)

Building rtl_433 with RTL-SDR or SoapySDR support is optional but using RTL-SDR is highly recommended.
The libraries and header files for RTL-SDR and/or SoapySDR should be installed beforehand.

## Nightly builds

Some distributions offer nightly builds.

### openSUSE

openSUSE users of at least Leap 42.3 or Tumbleweed can add the repository with daily builds:

    $ sudo zypper addrepo -f obs://home:mnhauke:rtl_433:nightly/rtl_433
    rtl_433-nightly
    $ sudo zypper install rtl_433

The usual update mechanism will now keep the rtl_433 version current.

### Fedora

Fedora users (31, 32 and Rawhide) can add the following copr repository to get nightly builds:

    $ sudo dnf copr enable tvass/rtl_433
    $ sudo dnf install rtl_433

The usual update mechanism will now keep the rtl_433 version current.

## Linux / Mac OS X

Depending on your system, you may need to install the following libraries.

Debian:

* If you require TLS connections, install `libssl-dev`.

````
sudo apt-get install libtool libusb-1.0-0-dev librtlsdr-dev rtl-sdr build-essential cmake pkg-config
````


Centos/Fedora/RHEL with EPEL repo using cmake:

  * If `dnf` doesn't exist, use `yum`.
  * If you require TLS connections, install `openssl-devel`.

````
sudo dnf install libtool libusbx-devel rtl-sdr-devel rtl-sdr cmake
````

Mac OS X with MacPorts:

* If you require TLS connections, install `openssl` from either MacPorts or Homebrew.

````
sudo port install rtl-sdr cmake
````

Mac OS X with Homebrew:

    brew install rtl-sdr cmake pkg-config

### CMake

Get the `rtl_433` git repository if needed:

    git clone https://github.com/merbanan/rtl_433.git

Installation using CMake:

    cd rtl_433/
    mkdir build
    cd build
    cmake ..
    make
    make install

Use CMake with `-DENABLE_SOAPYSDR=ON` (default: `AUTO`) to require SoapySDR (e.g. with Debian needs the package `libsoapysdr-dev`), use `-DENABLE_RTLSDR=OFF` (default: `ON`) to disable RTL-SDR if needed.
E.g. use:

    cmake -DENABLE_SOAPYSDR=ON ..

::: warning
If you experience trouble with SoapySDR when compiling or running: you likely mixed version 0.7 and version 0.8 headers and libs.
Purge all SoapySDR packages and source installation from /usr/local.
Then install only from packages (version 0.7) or only from source (version 0.8).
:::

## Windows

### Visual Studio 2017

You need [PothosSDR](https://downloads.myriadrf.org/builds/PothosSDR/) installed to get RTL-SDR and SoapySDR libraries.
Any recent version should work, e.g. [2021.07.25-vc16](https://downloads.myriadrf.org/builds/PothosSDR/PothosSDR-2021.07.25-vc16-x64.exe).

When installing PothosSDR choose "Add PothosSDR to the system PATH for the current user".

For TLS support (mqtts and influxs) you need OpenSSL installed.
E.g. [install Chocolatey](https://chocolatey.org/install) then open a Command Prompt and

    choco install openssl

Clone the project, e.g. open Visual Studio, change to "Team Explorer" > "Projects" > "Manage Connections" > "Clone"
and enter `https://github.com/merbanan/rtl_433.git`

If you want to change options, in the menu select "CMake" > "Change CMake Settings" > "rtl433", select e.g. "x64-Release", change e.g.

    "buildRoot": "${workspaceRoot}\\build",
    "installRoot": "${workspaceRoot}\\install",

To start a build use in the menu e.g. "CMake" > "Build all"

Or build at the Command Prompt without opening Visual Studio. Clone rtl_433 sources, then

    cd rtl_433
    mkdir build
    cd build
    cmake -G "Visual Studio 15 2017 Win64" ..
    cmake --build .

### MinGW-w64

You'll probably want librtlsdr and libusb.

libusb has prebuilt binaries for windows,
librtlsdr needs to be built (or extracted from the PothosSDR installer)

#### librtlsdr

taken and adapted from here: https://www.onetransistor.eu/2017/03/compile-librtlsdr-windows-mingw.html

* install [MinGW-w64](https://mingw-w64.org/) and [CMake](https://cmake.org/)
    * it's easiest if you select the option to include CMake in your path, otherwise you'll need to do this manually
* download the libusb binaries from https://sourceforge.net/projects/libusb/files/libusb-1.0/ or from https://libusb.info/
    * take the latest release and then download the .7z file, the other file contains the sources (or 'windows binaries' on the .info website)
* extract the archive and open the extracted folder
* copy the contents of the include folder to `<mingw_installation_folder>/include`
* copy the `mingw64/dll/libusb-1.0.dll.a` file to `<mingw_installation_folder>/lib
* copy the `mingw64/dll/libusb-1.0.dll` file to `<mingw_installation_folder>/bin`
* download the source code of librtlsdr https://github.com/steve-m/librtlsdr
* go into the librtlsdr folder
* open CMakeLists.txt with an editor that knows unix line endings
* go to `# Find build dependencies` (around line 65) and comment/remove the line with `find_package(Threads)`
* add the following lines instead:

```
SET(CMAKE_THREAD_LIBS_INIT "-lpthread")
SET(CMAKE_HAVE_THREADS_LIBRARY 1)
SET(Threads_FOUND TRUE)
```

* go into the cmake/modules folder and open FindLibUSB.cmake with a text editor
* find the lines with the following text in them

```
/usr/include/libusb-1.0
/usr/include
/usr/local/include
```

* add some extra lines to point to the MinGW include folder where you extracted libusb-1.0, making it look like this
    * take note of the "" around the folder names, these are needed when there are spaces in the folder name
    * you'll need to find out the exact paths for your system

```
/usr/include/libusb-1.0
/usr/include
/usr/local/include
"C:/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include"
"C:/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/include/libusb-1.0"
```

* open a MinGW terminal in the librtlsdr folder
* create build folder and go into it: `mkdir build && cd build`
* generate makefiles for MinGW: `cmake -G "MinGW Makefiles" ..`
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
* start a MinGW terminal and run `mingw32-make` to build
    * when something in the tests folder doesn't build, you can disable it by commenting out `add_subdirectory(tests)` in the CMakeLists.txt file in the source folder of rtl_433
* rtl_433.exe should be built now
* you need to place it in the same folder as librtlsdr.dll and libusb-1.0.dll (you should have seen both of them by now)
* good luck!

If your system is missing or you find these steps are outdated please PR an update or open an issue.
