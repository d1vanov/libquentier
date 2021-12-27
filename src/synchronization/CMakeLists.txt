cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_synchronization)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  Fwd.h
  ISimpleNotebookSyncConflictResolver.h
  SimpleNotebookSyncConflictResolver.cpp
  SimpleNotebookSyncConflictResolver.h
  SimpleSyncConflictResolver.cpp
  SimpleSyncConflictResolver.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_utility
  Qt5::Core)

target_compile_definitions(${LIBRARY_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")
