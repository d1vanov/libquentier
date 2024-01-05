cmake_minimum_required(VERSION 3.16.3)

set(LIBRARY_NAME ${LIBNAME}_threading)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  main.cpp)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_interface
  Qt${LIBQUENTIER_QT_VERSION}::Core)

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
