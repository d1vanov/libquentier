cmake_minimum_required(VERSION 3.10.2)

set(MOCKS_NAME ${LIBRARY_NAME}_testing_mocks)
add_library(${MOCKS_NAME} STATIC)

if(POLICY CMP0076)
  cmake_policy(SET CMP0076 NEW)
endif()

target_sources(${MOCKS_NAME}
  PUBLIC
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockIAuthenticator.h
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockISyncConflictResolver.h
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockISyncStateStorage.h
    MockIAccountLimitsProvider.h
    MockIAuthenticationInfoProvider.h
    MockIDurableNotesProcessor.h
    MockIDurableResourcesProcessor.h
    MockILinkedNotebooksProcessor.h
    MockINotebooksProcessor.h
    MockINoteFullDataDownloader.h
    MockINoteStoreFactory.h
    MockINotesProcessor.h
    MockIProtocolVersionChecker.h
    MockIResourceFullDataDownloader.h
    MockIResourcesProcessor.h
    MockISavedSearchesProcessor.h
    MockITagsProcessor.h
    MockISimpleNotebookSyncConflictResolver.h
    MockISimpleNoteSyncConflictResolver.h
    MockISimpleSavedSearchSyncConflictResolver.h
    MockISimpleTagSyncConflictResolver.h
    MockISyncChunksDownloader.h
    MockISyncChunksProvider.h
    MockISyncChunksStorage.h
    MockIUserInfoProvider.h
    qevercloud/services/MockINoteStore.h
    qevercloud/services/MockIUserStore.h
  PRIVATE
    CheckBuild.cpp
)

target_link_libraries(${MOCKS_NAME}
  PUBLIC
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
  PRIVATE
    ${LIBNAME}
)

target_include_directories(${MOCKS_NAME}
  PUBLIC
    ${GMOCK_INCLUDE_DIRS}
)
