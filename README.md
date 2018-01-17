libquentier
===========

**Set of Qt/C++ APIs for feature rich desktop clients for Evernote service**

Travis CI (Linux, OS X): [![Build Status](https://travis-ci.org/d1vanov/libquentier.svg?branch=master)](https://travis-ci.org/d1vanov/libquentier)

AppVeyor CI (Windows): [![Build status](https://ci.appveyor.com/api/projects/status/24k9pidhi8g8slb0/branch/master?svg=true)](https://ci.appveyor.com/project/d1vanov/libquentier/branch/master)

## What's this

This library presents a set of Qt/C++ APIs useful for applications working as feature rich desktop clients for Evernote service.
The most important and useful components of the library are the following:
* Local storage - persistence of data downloaded from Evernote service in a local SQLite database
* Synchronization - the logics of exchanging new and/or modified data with Evernote service
* Note editor - the UI component capable for notes displaying and editing

The library is based on the lower level functionality provided by [QEverCloud](https://github.com/d1vanov/QEverCloud) library.
It also serves as the functional core of [Quentier](https://github.com/d1vanov/quentier) application.

### WARNING: libquentier is in alpha state right now, neither API nor ABI can be considered stable yet!

## How to build/install

Please see the [building/installation guide](INSTALL.md).

## How to contribute

Please see the [contribution guide](CONTRIBUTING.md) for detailed info.
