libquentier
===========

**Set of Qt/C++ APIs for feature rich desktop clients for Evernote service**

## What's this

This library presents a set of Qt/C++ APIs useful for applications working as feature rich desktop clients for Evernote service.
The most important and useful components of the library are the following:
* Local storage - persistence of data downloaded from Evernote service in a local SQLite database
* Synchronization - the logics of exchanging new and/or modified data with Evernote service
* Note editor - the UI component capable for notes displaying and editing

The library is based on the lower level functionality provided by [QEverCloud](https://github.com/d1vanov/QEverCloud) library.

## Compatibility

The library can be built with both Qt4 and Qt5 versions of the framework. The officially supported Qt versions are Qt 4.8.6+ on Linux and Windows
and Qt 5.2.1+ on Linux, Windows and OS X / macOS.

### C++11 features

The major part of the library code is written in C++98 style while also using a few features of C++11 standard supported by some older compilers. In particular, compilers as old as gcc-4.5 and Visual C++ 2010 are capable of building the library.

### Note editor backends

The note editor component of the library currently uses two different backends for Qt4 and Qt5 branches: `QtWebKit` for the former one and either `QtWebKit` or `QtWebEngine` for the latter one.
