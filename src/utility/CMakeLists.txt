cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_utility)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  Qt5FutureHelpers.h
  Qt5Promise.h
  QtFutureContinuations.h
  QtFutureWatcherUtils.h
  Threading.cpp
  Threading.h
  ThreadingUtils.cpp
  ThreadingUtils.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_interface
  Qt5::Core)
