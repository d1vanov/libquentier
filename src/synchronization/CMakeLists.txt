cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_synchronization)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  Fwd.h
  SynchronizationMode.h
  conflict_resolvers/Factory.cpp
  conflict_resolvers/Factory.h
  conflict_resolvers/ISimpleNotebookSyncConflictResolver.h
  conflict_resolvers/ISimpleNoteSyncConflictResolver.h
  conflict_resolvers/ISimpleSavedSearchSyncConflictResolver.h
  conflict_resolvers/ISimpleTagSyncConflictResolver.h
  conflict_resolvers/SimpleGenericSyncConflictResolver.h
  conflict_resolvers/SimpleNotebookSyncConflictResolver.cpp
  conflict_resolvers/SimpleNotebookSyncConflictResolver.h
  conflict_resolvers/SimpleNoteSyncConflictResolver.cpp
  conflict_resolvers/SimpleNoteSyncConflictResolver.h
  conflict_resolvers/SimpleSavedSearchSyncConflictResolver.cpp
  conflict_resolvers/SimpleSavedSearchSyncConflictResolver.h
  conflict_resolvers/SimpleSyncConflictResolver.cpp
  conflict_resolvers/SimpleSyncConflictResolver.h
  conflict_resolvers/SimpleTagSyncConflictResolver.cpp
  conflict_resolvers/SimpleTagSyncConflictResolver.h
  processors/INotebooksProcessor.h
  processors/ISavedSearchesProcessor.h
  processors/NotebooksProcessor.cpp
  processors/NotebooksProcessor.h
  processors/SavedSearchesProcessor.cpp
  processors/SavedSearchesProcessor.h
  processors/Utils.h
  sync_chunks/ISyncChunksDownloader.h
  sync_chunks/ISyncChunksProvider.h
  sync_chunks/ISyncChunksStorage.h
  sync_chunks/SyncChunksDownloader.cpp
  sync_chunks/SyncChunksDownloader.h
  sync_chunks/SyncChunksProvider.cpp
  sync_chunks/SyncChunksProvider.h
  sync_chunks/SyncChunksStorage.cpp
  sync_chunks/SyncChunksStorage.h
  sync_chunks/Utils.cpp
  sync_chunks/Utils.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_threading
  Qt5::Core)

target_compile_definitions(${LIBRARY_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
