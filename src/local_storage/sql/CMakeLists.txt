cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_local_storage_sql)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  ConnectionPool.cpp
  ConnectionPool.h
  ErrorHandling.h
  TablesInitializer.cpp
  TablesInitializer.h
  VersionHandler.cpp
  VersionHandler.h
  patches/Patch1To2.cpp
  patches/Patch1To2.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_utility
  Qt5::Core
  Qt5::Sql)

target_compile_definitions(${LIBRARY_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
