**Building and installation guide**

Libquentier is intended to be built and shipped as a shared i.e. dynamically linked library. Dll export/import symbols necessary for Windows platform are supported.

## Downloads

Prebuilt versions of the library can be downloaded from the following locations:

 * Stable version:
   * Windows binaries:
     * [MSVC 2015 32 bit Qt 5.13](https://github.com/d1vanov/libquentier/releases/download/continuous-master/libquentier-windows-qt513-VS2015_x86.zip)
     * [MSVC 2017 64 bit Qt 5.13](https://github.com/d1vanov/libquentier/releases/download/continuous-master/libquentier-windows-qt513-VS2017_x64.zip)
   * [Mac binary](https://github.com/d1vanov/libquentier/releases/download/continuous-master/libquentier_mac_x86_64.zip) built with latest Qt from Homebrew
   * [Linux binary](https://github.com/d1vanov/libquentier/releases/download/continuous-master/libquentier_linux_qt_5123_x86_64.zip) built on Ubuntu 14.04 with Qt 5.9.2
 * Unstable version:
   * Windows binaries:
     * [MSVC 2015 32 bit Qt 5.13](https://github.com/d1vanov/libquentier/releases/download/continuous-development/libquentier-windows-qt513-VS2015_x86.zip)
     * [MSVC 2017 64 bit Qt 5.13](https://github.com/d1vanov/libquentier/releases/download/continuous-development/libquentier-windows-qt513-VS2017_x64.zip)
   * [Mac binary](https://github.com/d1vanov/libquentier/releases/download/continuous-development/libquentier_mac_x86_64.zip) built with latest Qt from Homebrew
   * [Linux binary](https://github.com/d1vanov/libquentier/releases/download/continuous-development/libquentier_linux_qt_5123_x86_64.zip) built on Ubuntu 14.04 with Qt 5.9.2

There are also repositories from which libquentier can be installed conveniently for several Linux distributions:

 * Stable version:
   * See [this page](https://software.opensuse.org//download.html?project=home%3Ad1vanov%3Aquentier-master&package=libquentier) for Fedora 26, Fedora 27, OpenSUSE Leap 42.3, OpenSUSE Tumbleweed, OpenSUSE Leap 15.0 and Arch Linux repositories
   * See [this page](https://software.opensuse.org//download.html?project=home%3Ad1vanov%3Aquentier-master&package=libqt5quentier0) for Debian 9.0 repositories
   * See [this PPA](https://launchpad.net/~d1vanov/+archive/ubuntu/quentier-stable) for repositories of multiple Ubuntu versions
 * Unstable version:
   * See [this page](https://software.opensuse.org//download.html?project=home%3Ad1vanov%3Aquentier-development&package=libquentier) for Fedora 26, Fedora 27, OpenSUSE Leap 42.3, OpenSUSE Tumbleweed, OpenSUSE Leap 15.0 and Arch Linux repositories
   * See [this page](https://software.opensuse.org//download.html?project=home%3Ad1vanov%3Aquentier-development&package=libqt5quentier0) for Debian 9.0 repositories
   * See [this PPA](https://launchpad.net/~d1vanov/+archive/ubuntu/quentier-development) for repositories of multiple Ubuntu versions

Note that you need to pick **either** stable or unstable version and not intermix the two! Stable version corresponds to latest version from `master` branch, unstable version corresponds to `development` branch.

## Compatibility

Libquentier works on Linux, OS X / macOS and Windows. It can be built Qt framework starting from 5.5.1,
and up to the latest and greatest Qt 5.x. The major part of libquentier is written in C++14 standard.
Libquentier should be easy to build on any recent enough Linux distro. The oldest supported distro
is considered to be the oldest LTS Ubuntu version.

Even though libquentier is cross-platform, most development and testing currently occurs on Linux
so things might occasionally break on Windows and macOS platforms.

## Dependencies

Dependencies include the following Qt modules:
 * For Qt5: Qt5Core, Qt5Gui, Qt5Widgets, Qt5Network, Qt5PrintSupport, Qt5Xml, Qt5Sql, Qt5Test, Qt5LinguistTools + Qt5DBus on Linux platform only + optionally either Qt5WebKit and Qt5WebKitWidgets or Qt5WebEngine (and Qt5WebEngineCore for Qt >= 5.6), Qt5WebEngineWidgets, Qt5WebSockets and Qt5WebChannel

The dependency on QtWebKit or QtWebEngine for Qt4 and Qt5 is enabled by default but can be disabled by passing special arguments to `CMake`, see the details below.

Non-Qt dependendencies of libquentier are the following:
 * libxml2 - for validation of Evernote notes ENML against the DTD
 * OpenSSL - for encryption and decryption of note fragments. Note that OpenSSL version shipped with macOS by Apple doesn't contain the required encryption/decryption API and is therefore not suitable for libquentier - you would encounter build errors if you try to use that OpenSSL version. The OpenSSL from homebrew or macports would be suitable.
 * Boost (some header-only libraries)
 * libhunspell
 * [QtKeychain](https://github.com/frankosterfeld/qtkeychain)
 * [QEverCloud](https://github.com/d1vanov/QEverCloud)
 * libtidy5 from [tidy-html5](https://github.com/htacg/tidy-html5). Note that the old libtidy 0.99 shipped with some older Linux distros won't be suitable!
 * Optionally: Doxygen (for automatic generation of documentation)

Although it is theoretically possible to use different Qt versions to build libquentier and its dependencies, it is highly
non-recommended as it can cause all sort of building and/or runtime issues.

## Building

Libquentier uses [CMake](https://cmake.org) meta-build system to find all the necessary libraries and to generate makefiles
or IDE project files. Prior to building Quentier you should build and install all of its dependencies listed above.

Here are the basic steps of building Quentier (on Linux and OS X / macOS):
```
cd <...path to cloned libquentier repository...>
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=<...where to install the built library...> ../
make
make install
```

On Windows the `cmake` step is usually more convenient to do using GUI version of `CMake`. For build instead of `make`
it is convenient to call `cmake --build .` command; if target specification is required, it can be done like this:
`cmake --build . --target install`.
	
If you installed libquentier's dependencies into non-standard locations on your Linux or OS X / macOS system, the `cmake` step
from the above list might fail to find some library. You can give `CMake` some hints where to find the dependencies:
```
cmake -DLIBXML2_INCLUDE_DIR=<...path to libxml2 include directory...> \
      -DLIBXML2_LIBRARIES=<...path to libxml2 library...> \
      -DOPENSSL_ROOT_DIR=<...path to the OpenSSL installation prefix...> \
      -DBOOST_ROOT=<...path to boost installation prefix...> \
      -DQt5Keychain_DIR=<...path to QtKeychain cmake dir within the installation prefix...> \
      -DQEverCloud-qt5_DIR=<...path to Qt5 QEverCloud installation...>/lib/cmake/QEverCloud-qt5 \
      -DTIDY_HTML5_INCLUDE_PATH=<...path to tidy-html5 include directory...> \
      -DTIDY_HTML5_LIB=<...path to tidy-html5 library...> \
      -DCMAKE_INSTALL_PREFIX=<...where to install the built app...> ../
```

As mentioned above, some Qt modules are listed as optional dependencies. These modules are required by default (i.e. `cmake` step would fail if either of these modules is not found) but the build can be configured to exclude them as well as the pieces of libquentier's functionality for which these modules are required.
These pieces of functionality include:
 * Note editor - the widget encapsulating all the details of presenting the editable note with ENML-formatted text and resources (attachments)
 * OAuth authentication - the library's built-in implementation of `IAuthenticationManager` interface using `QEverCloud`'s authentication facilities

The `CMake` options allowing to configure the build to omit the listed libquentier's components and thus the need for `QtWebKit` or `QtWebEngine` are the following:
 * `BUILD_WITH_NOTE_EDITOR`
 * `BUILD_WITH_AUTHENTICATION_MANAGER`

In order to force the build without `QtWebKit` or `QtWebEngine`, set both options to `NO`:
```
cmake -DBUILD_WITH_NOTE_EDITOR=NO -DBUILD_WITH_AUTHENTICATION_MANAGER=NO <...>
```

One other related option controls which of two Qt's web backends to use with Qt5: by default `QtWebEngine` is searched for Qt >= 5.5. But one can force the use of `QtWebKit` instead of `QtWebEngine` with `CMake` option `USE_QT5_WEBKIT`:
```
cmake -DUSE_QT5_WEBKIT=YES <...>
```

If you want to point `CMake` to some particular Qt installation in non-standard location, you can do it with `CMAKE_PREFIX_PATH` option:
```
cmake -DCMAKE_PREFIX_PATH=<...path to Qt installation...> <...>
```

### Running tests

Libquentier comes with a set of self tests which you are encouraged to run if you build libquentier yourself. Obviously,
if tests don't pass, something is wrong. The tests can be run by making `test` or `check` target - the latter one
provides more explicit output than the former one. So the available options to run the tests are as follows:
 * `make test`
 * `make check`
 * `cmake --build . target test`
 * `cmake --build . target check`

### Clang-tidy usage

[Clang-tidy](https://clang.llvm.org/extra/clang-tidy) is a clang based "linter" tool for C++ code. Usage of clang-tidy is supported in libquentier project provided that `clang-tidy` binary can be found in your `PATH` environment variable:
 * There is a configuration file [.clang-tidy](.clang-tidy) for running clang-tidy over libquentier's codebase
 * There is a build target `clang-tidy` which allows one to run `clang-tidy` over the entire libquentier's codebase at once. It might be rather slow though.
 * `CMake` of versions >= 3.7.2 have [built-in support](https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html) for running clang-tidy along with the compiler when compiling code. In order to set up this way of `clang-tidy` usage pass the following options to `CMake`:
```
cmake -DCMAKE_C_CLANG_TIDY=<path to clang-tidy> -DCMAKE_CXX_CLANG_TIDY=<path to clang-tidy> <...>
```

### Translation

Note that files required for libquentier's translation are not built by default along with the library itself. In order to build
the translation files one needs to execute two additional commands:
```
make lupdate
make lrelease
```
The first one runs Qt's `lupdate` utility which extracts the strings requiring translation from the source code and updates
the `.ts` files containing both non-localized and localized text. The second command runs Qt's `lrelease` utility which
converts `.ts` files into installable `.qm` files. If during installation `.qm` files are present within the build directory,
they would be installed along with the library.

## Installation

The last step of building as written above is `make install` i.e. the installation of the built libquentier library. It is important
to actually install the library as some of its public headers contain code generated automatically during `cmake` step. These files
need to be installed in order to be visible to the client code along with other headers.
