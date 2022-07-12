cmake_minimum_required(VERSION 3.10.2)

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
  keychain/QtKeychainWrapper.cpp
  keychain/QtKeychainWrapper.h
  keychain/Utils.cpp
  keychain/Utils.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_threading
  Qt5::Core)

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
