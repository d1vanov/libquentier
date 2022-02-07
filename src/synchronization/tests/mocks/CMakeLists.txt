cmake_minimum_required(VERSION 3.10.2)

set(MOCKS_NAME ${LIBRARY_NAME}_testing_mocks)
add_library(${MOCKS_NAME} STATIC)

if(POLICY CMP0076)
  cmake_policy(SET CMP0076 NEW)
endif()

target_sources(${MOCKS_NAME}
  PUBLIC
    MockISimpleNotebookSyncConflictResolver.h
    MockISimpleNoteSyncConflictResolver.h
    MockISimpleSavedSearchSyncConflictResolver.h
    MockISimpleTagSyncConflictResolver.h
    MockISyncChunksDownloader.h
  PRIVATE
    CheckBuild.cpp
)

target_link_libraries(${MOCKS_NAME}
  PUBLIC
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
)
