**Building and installation guide**

Libquentier is intended to be built and shipped as s shared i.e. dynamically linked library. Dll export/import symbols necessary for Windows platform are supported.

## Compatibility

Libquentier works on Linux, OS X / macOS and Windows. It can be built with virtually any version of Qt framework,
starting from Qt 4.8.6 and up to the latest and greatest Qt 5.x. The major part of libquentier is written in C++98 style
with a few features of C++11 standard which are supported by older compilers. As a result, libquentier can be built
by as old compilers as gcc-4.5, Visual C++ 2010.

## Dependencies

Dependencies include the following Qt components:
 * For Qt4: QtCore, QtGui, QtNetwork, QtXml, QtSql, QtTest + QtDBus on Linux platform only + optionally QtWebKit
 * For Qt5: Qt5Core, Qt5Gui, Qt5Widgets, Qt5Network, Qt5PrintSupport, Qt5Xml, Qt5Sql, Qt5Test, Qt5LinguistTools + Qt5DBus on Linux platform only + optionally either Qt5WebKit and Qt5WebKitWidgets or Qt5WebEngine (and Qt5WebEngineCore for Qt >= 5.6), Qt5WebEngineWidgets, Qt5WebSockets and Qt5WebChannel

The dependency on QtWebKit or QtWebEngine for Qt4 and Qt5 is enabled by default but can be disabled by passing special arguments to `CMake`, see the details below.

Non-Qt dependendencies of libquentier are the following:
 * libxml2 - for validation of Evernote notes ENML againts the DTD
 * OpenSSL - for encryption and decryption of note fragments. Note that OpenSSL version shipped with OS X / macOS by Apple doesn't contain the required encryption/decryption API and is therefore not suitable for libquentier - you would encounter build errors if you try to use that OpenSSL version. The OpenSSL from homebrew or macports would be suitable.
 * Boost (some header-only libraries)
 * libhunspell
 * [QtKeychain](https://github.com/frankosterfeld/qtkeychain)
 * [QEverCloud](https://github.com/d1vanov/QEverCloud)
 * libtidy5 from [tidy-html5](https://github.com/htacg/tidy-html5). Note that the old libtidy 0.99 shipped with many Linux distros won't be suitable!
 * For Qt4 builds only: [qt4-mimetypes](https://github.com/d1vanov/qt4-mimetypes)
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

On Windows the `cmake` step is usually more convenient to do using GUI version of `CMake`.
	
If you installed libquentier's dependencies into non-standard locations on your Linux or OS X / macOS system, the `cmake` step
from the above list might fail to find some library. You can give `CMake` some hints where to find the dependencies:

For Qt4:
```
cmake -Dqt4-mimetypes_DIR=<...path to qt4-mimetypes installation folder...>/lib/cmake/qt4-mimetypes \
      -DLIBXML2_INCLUDE_DIR=<...path to libxml2 include directory...> \
      -DLIBXML2_LIBRARIES=<...path to libxml2 library...> \
      -DOPENSSL_ROOT_DIR=<...path to the OpenSSL installation prefix...> \
      -DBOOST_ROOT=<...path to boost installation prefix...> \
      -DQtKeychain_DIR=<...path to QtKeychain cmake dir within the installation prefix...> \
      -DQEverCloud-qt4_DIR=<...path to Qt4 QEverCloud installation...>/lib/cmake/QEverCloud-qt4 \
      -DCMAKE_INCLUDE_PATH=<...path to tidy-html5 include directory...> \
      -DCMAKE_LIBRARY_PATH=<...path to tidy-html5 libraries directory...> \
      -DCMAKE_INSTALL_PREFIX=<...where to install the built library...> ../
```
For Qt5:
```
cmake -DLIBXML2_INCLUDE_DIR=<...path to libxml2 include directory...> \
      -DLIBXML2_LIBRARIES=<...path to libxml2 library...> \
      -DOPENSSL_ROOT_DIR=<...path to the OpenSSL installation prefix...> \
      -DBOOST_ROOT=<...path to boost installation prefix...> \
      -DQt5Keychain_DIR=<...path to QtKeychain cmake dir within the installation prefix...> \
      -DQEverCloud-qt5_DIR=<...path to Qt5 QEverCloud installation...>/lib/cmake/QEverCloud-qt5 \
      -DCMAKE_INCLUDE_PATH=<...path to tidy-html5 include directory...> \
      -DCMAKE_LIBRARY_PATH=<...path to tidy-html5 libraries directory...> \
      -DCMAKE_INSTALL_PREFIX=<...where to install the built app...> ../
```

As mentioned above, some Qt modules are listed as optional dependencies. These modules are required by default (i.e. the `cmake` step would fail if either of these modules is not found) but the build can be configured to exclude them as well as the pieces of libquentier's functionality for which these modules are required.
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

One other related option controls which of two Qt's web backends to use with Qt5: by default `QtWebEngine` is searched for for Qt >= 5.5. But one can force the use of `QtWebKit` instead of `QtWebEngine` with `CMake` option `USE_QT5_WEBKIT`:
```
cmake -DUSE_QT5_WEBKIT=YES <...>
```

The Qt version being searched by default is Qt4. If you have both Qt4 and Qt5 installed and want to force the use of Qt5, use `USE_QT5` `CMake` option:
```
cmake -DUSE_QT5=YES <...>
```

If you don't have the Qt4 installation but only Qt5, there's no need to use this option, the Qt5 installation would be found automatically given that it is installed into one of standard locations on your system. Otherwise you'd need to point `CMake` to your Qt installation with `CMAKE_PREFIX_PATH` option:
```
cmake -DCMAKE_PREFIX_PATH=<...path to Qt5 installation...> <...>
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
