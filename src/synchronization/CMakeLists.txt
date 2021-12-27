cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_synchronization)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  Fwd.h
  conflict_resolvers/ISimpleNotebookSyncConflictResolver.h
  conflict_resolvers/ISimpleSavedSearchSyncConflictResolver.h
  conflict_resolvers/SimpleNotebookSyncConflictResolver.cpp
  conflict_resolvers/SimpleNotebookSyncConflictResolver.h
  conflict_resolvers/SimpleSavedSearchSyncConflictResolver.cpp
  conflict_resolvers/SimpleSavedSearchSyncConflictResolver.h
  conflict_resolvers/SimpleSyncConflictResolver.cpp
  conflict_resolvers/SimpleSyncConflictResolver.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_utility
  Qt5::Core)

target_compile_definitions(${LIBRARY_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")
