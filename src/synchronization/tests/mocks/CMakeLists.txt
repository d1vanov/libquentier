cmake_minimum_required(VERSION 3.10.2)

set(MOCKS_NAME ${LIBRARY_NAME}_testing_mocks)
add_library(${MOCKS_NAME} STATIC)

if(POLICY CMP0076)
  cmake_policy(SET CMP0076 NEW)
endif()

target_sources(${MOCKS_NAME}
  PUBLIC
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockIAuthenticator.h
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockINoteStoreFactory.h
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockISyncConflictResolver.h
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/synchronization/tests/mocks/MockISyncStateStorage.h
    MockIAccountSyncPersistenceDirProvider.h
    MockIAccountSynchronizer.h
    MockIAccountSynchronizerFactory.h
    MockIAuthenticationInfoProvider.h
    MockIDownloader.h
    MockIDurableNotesProcessor.h
    MockIDurableResourcesProcessor.h
    MockIFullSyncStaleDataExpunger.h
    MockIInkNoteImageDownloaderFactory.h
    MockILinkedNotebookFinder.h
    MockILinkedNotebooksProcessor.h
    MockINotebooksProcessor.h
    MockINoteFullDataDownloader.h
    MockINoteStoreProvider.h
    MockINoteThumbnailDownloaderFactory.h
    MockINotesProcessor.h
    MockIProtocolVersionChecker.h
    MockIResourceFullDataDownloader.h
    MockIResourcesProcessor.h
    MockISavedSearchesProcessor.h
    MockISender.h
    MockISimpleNotebookSyncConflictResolver.h
    MockISimpleNoteSyncConflictResolver.h
    MockISimpleSavedSearchSyncConflictResolver.h
    MockISimpleTagSyncConflictResolver.h
    MockISyncChunksDownloader.h
    MockISyncChunksProvider.h
    MockISyncChunksStorage.h
    MockITagsProcessor.h
    MockIUserInfoProvider.h
    qevercloud/MockIInkNoteImageDownloader.h
    qevercloud/MockINoteThumbnailDownloader.h
    qevercloud/services/MockINoteStore.h
    qevercloud/services/MockIUserStore.h
  PRIVATE
    CheckBuild.cpp
)

target_link_libraries(${MOCKS_NAME}
  PUBLIC
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
    Qt${LIBQUENTIER_QT_VERSION}::Core
  PRIVATE
    ${LIBNAME}
)

target_include_directories(${MOCKS_NAME}
  PUBLIC
    ${GMOCK_INCLUDE_DIRS}
)

# NOTE: this property is set to prevent automoc when building this mock library
# because automoc works again when building the actual tests using this library
# and it results in multiple definitions build error
set_target_properties(${MOCKS_NAME} PROPERTIES AUTOMOC FALSE)
