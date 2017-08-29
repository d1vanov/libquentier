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

TODO: continue from here, describe the available building options + continue with translation and installation steps
