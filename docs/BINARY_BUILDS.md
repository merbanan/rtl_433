# Binary Builds

First check to see if your distribution already has a recent enough version packaged for you.
E.g. check [Repology](https://repology.org/project/rtl-433/versions) for a quick overview.

We currently provide 18 [binary builds](https://github.com/merbanan/rtl_433/releases) for different OS, Platform, Version and Features.
This is intended to quickly test out `rtl_433` or update to the newest version.
Due to library dependencies and versions we can't guarantee our binaries to work
(safe to try though, the worst is a "executable not supported" or "library is missing" message).

Let us know in [with a comment](https://github.com/merbanan/rtl_433/issues/2859) if a binary unexpectedly does work or does not work.

OS and Platform:
- Windows: x32 and x64
- MacOS: x86_64/Intel and arm64/M1
- Linux: x86_64/amd64, arm64 (Raspberry Pi OS 64-bit), and armhf (Raspberry Pi OS 64-bit)

Version (only Linux):
- Variant for OpenSSL 1.1: Ubuntu 20.04 focal, Debian 11 Bullseye, Raspberry Pi OS Legacy
- Variant for OpenSSL 3: Ubuntu 22.04 jammy, Debian 12 Bookworm, Raspberry Pi OS

Features:
- with only rtlsdr
- with rtlsdr and SoapySDR

## Choosing a binary

Without SoapySDR:
- `rtl_433-rtlsdr-MacOS-arm64.zip`: MacOS-14 for arm64/M1
- `rtl_433-rtlsdr-MacOS-x86_64.zip`: MacOS-12 for x86_64/Intel
- `rtl_433-rtlsdr-openssl11-Linux-amd64.zip`: Linux for x86_64/amd64 with OpenSSL 1.1
- `rtl_433-rtlsdr-openssl11-Linux-arm64.zip`: Linux for aarch64/arm64 with OpenSSL 1.1
- `rtl_433-rtlsdr-openssl11-Linux-armhf.zip`: Linux for armhf with OpenSSL 1.1
- `rtl_433-rtlsdr-openssl3-Linux-amd64.zip`: Linux for x86_64/amd64 with OpenSSL 3
- `rtl_433-rtlsdr-openssl3-Linux-arm64.zip`: Linux for aarch64/arm64 with OpenSSL 3
- `rtl_433-rtlsdr-openssl3-Linux-armhf.zip`: Linux for armhf with OpenSSL 3

With SoapySDR:
- `rtl_433-soapysdr-MacOS-arm64.zip`: MacOS-arm64
- `rtl_433-soapysdr-MacOS-x86_64.zip`: MacOS-x86_64
- `rtl_433-soapysdr-openssl11-Linux-amd64.zip`: Linux for x86_64/amd64 with OpenSSL 1.1
- `rtl_433-soapysdr-openssl11-Linux-arm64.zip`: Linux for aarch64/arm64 with OpenSSL 1.1
- `rtl_433-soapysdr-openssl11-Linux-armhf.zip`: Linux for armhf with OpenSSL 1.1
- `rtl_433-soapysdr-openssl3-Linux-amd64.zip`: Linux for x86_64/amd64 with OpenSSL 3
- `rtl_433-soapysdr-openssl3-Linux-arm64.zip`: Linux for aarch64/arm64 with OpenSSL 3
- `rtl_433-soapysdr-openssl3-Linux-armhf.zip`: Linux for armhf with OpenSSL 3


## Easy Install

Easiest install would be to first install a distribution provided `rtl_433` package for the dependencies,
then use one of these binaries instead.

## Install

Otherwise you need to install libusb, openssl (1.1 or 3), librtlsdr, and optionally SoapySDR (plus driver modules).

### MacOS

After unpacking the binary you need to clear the file attributes:
```
xattr -c rtl_433
```
Note that `com.apple.quarantine` attributes are a useful safety feature and you should only perform this with genuine downloads from trusted sources.

::: warning
Note that [Homebrew](https://formulae.brew.sh/formula/librtlsdr) uses librtlsdr version 2.0 (with rtl-sdr blog v4 support)
while [MacPorts](https://ports.macports.org/port/rtl-sdr/details/) uses version 0.6
but those are compatible.
You'll need to update the binary to run on MacPorts:
```
install_name_tool -change @rpath/librtlsdr.2.dylib @rpath/librtlsdr.0.dylib rtl_433
```
:::

#### MacPorts

(s.a. the [MacPorts port](https://ports.macports.org/port/rtl_433/))
```
sudo port install libusb openssl3 rtl-sdr
```
optionally add
```
sudo port install SoapySDR
```

#### HomeBrew

(s.a. the [Homebrew Formula](https://formulae.brew.sh/formula/rtl_433))
```
brew install libusb
brew install openssl@3
brew install librtlsdr
```
optionally add
```
brew install soapysdr
```

### Linux

On Debian, Ubuntu, and Raspberry Pi OS (and similar Debian-based OS supporting the `apt` package manager):

```
sudo apt-get install -y rtl-sdr openssl soapysdr-tools
```

Or with out any tools, just the libs for Bullseye / Focal
```
sudo apt-get install -y librtlsdr0 libssl1.1 libsoapysdr0.7
```

Similar for Bookworm / Jammy
```
sudo apt-get install -y librtlsdr0 libssl3 libsoapysdr0.8
```
