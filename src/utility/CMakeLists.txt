cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_utility)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  Threading.cpp
  Threading.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  Qt5::Core)
