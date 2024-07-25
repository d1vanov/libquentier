cmake_minimum_required(VERSION 3.16.3)

add_subdirectory(mocks)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  main.cpp
  AccountSynchronizerFactoryTest.cpp
  AccountSynchronizerTest.cpp
  AuthenticationInfoProviderTest.cpp
  DownloaderTest.cpp
  DurableNotesProcessorTest.cpp
  DurableResourcesProcessorTest.cpp
  ExceptionUtilsTest.cpp
  FullSyncStaleDataExpungerTest.cpp
  InkNoteImageDownloaderFactoryTest.cpp
  LinkedNotebookFinderTest.cpp
  LinkedNotebooksProcessorTest.cpp
  LinkedNotebookTagsCleanerTest.cpp
  NotebookFinderTest.cpp
  NotebooksProcessorTest.cpp
  NoteFullDataDownloaderTest.cpp
  NoteStoreProviderTest.cpp
  NoteThumbnailDownloaderFactoryTest.cpp
  NotesProcessorTest.cpp
  ProtocolVersionCheckerTest.cpp
  ResourceFullDataDownloaderTest.cpp
  ResourcesProcessorTest.cpp
  SavedSearchesProcessorTest.cpp
  SenderTest.cpp
  SimpleNotebookSyncConflictResolverTest.cpp
  SimpleNoteSyncConflictResolverTest.cpp
  SimpleSavedSearchSyncConflictResolverTest.cpp
  SimpleSyncConflictResolverTest.cpp
  SimpleTagSyncConflictResolverTest.cpp
  SyncChunksDownloaderTest.cpp
  SyncChunksProviderTest.cpp
  SyncChunksStorageTest.cpp
  SynchronizerTest.cpp
  TagsProcessorTest.cpp
  UserInfoProviderTest.cpp
  Utils.cpp
  Utils.h
  serialization/json/AuthenticationInfoJsonSerializationTest.cpp
  serialization/json/DownloadNotesStatusJsonSerializationTest.cpp
  serialization/json/DownloadResourcesStatusJsonSerializationTest.cpp
  serialization/json/SendStatusTest.cpp
  serialization/json/SerializationUtilsTest.cpp
  serialization/json/SyncChunksDataCountersTest.cpp
  serialization/json/SyncResultTest.cpp
  serialization/json/SyncStateTest.cpp
)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}_private
    ${LIBRARY_NAME}
    ${LIBRARY_NAME}_testing_mocks
    ${LIBNAME}
    ${LIBNAME}_local_storage_sql
    ${LIBNAME}_synchronization
    ${GMOCK_LIBRARIES}
    GTest::GTest
    Qt${LIBQUENTIER_QT_VERSION}::Core)

add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME})

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
