cmake_minimum_required(VERSION 3.10.2)

add_subdirectory(mocks)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  main.cpp
  AuthenticationInfoProviderTest.cpp
  DownloaderTest.cpp
  DurableNotesProcessorTest.cpp
  DurableResourcesProcessorTest.cpp
  LinkedNotebooksProcessorTest.cpp
  NotebooksProcessorTest.cpp
  NoteFullDataDownloaderTest.cpp
  NotesProcessorTest.cpp
  ResourceFullDataDownloaderTest.cpp
  ResourcesProcessorTest.cpp
  SavedSearchesProcessorTest.cpp
  SimpleNotebookSyncConflictResolverTest.cpp
  SimpleNoteSyncConflictResolverTest.cpp
  SimpleSavedSearchSyncConflictResolverTest.cpp
  SimpleSyncConflictResolverTest.cpp
  SimpleTagSyncConflictResolverTest.cpp
  SyncChunksDownloaderTest.cpp
  SyncChunksProviderTest.cpp
  SyncChunksStorageTest.cpp
  TagsProcessorTest.cpp
  UserInfoProviderTest.cpp)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}_private
    ${LIBNAME}
    ${LIBRARY_NAME}
    ${LIBRARY_NAME}_testing_mocks
    ${GMOCK_LIBRARIES}
    GTest::GTest
    Qt5::Core)

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
