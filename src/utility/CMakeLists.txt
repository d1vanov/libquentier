cmake_minimum_required(VERSION 3.16.3)

set(LIBRARY_NAME ${LIBNAME}_utility)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  keychain/CompositeKeychainService.cpp
  keychain/CompositeKeychainService.h
  keychain/MigratingKeychainService.cpp
  keychain/MigratingKeychainService.h
  keychain/ObfuscatingKeychainService.cpp
  keychain/ObfuscatingKeychainService.h
  keychain/QtKeychainService.cpp
  keychain/QtKeychainService.h
  keychain/Utils.cpp
  keychain/Utils.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_threading
  Qt${LIBQUENTIER_QT_VERSION}::Core
  Qt${LIBQUENTIER_QT_VERSION}Keychain::Qt${LIBQUENTIER_QT_VERSION}Keychain)

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
