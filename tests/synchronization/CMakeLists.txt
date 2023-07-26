cmake_minimum_required(VERSION 3.10.2)

set(TESTS_NAME "${LIBNAME}_synchronization_integrational_tests")
set(TESTS_LIB_NAME "${TESTS_NAME}_lib")

add_library(${TESTS_LIB_NAME} STATIC 
  FakeAuthenticator.cpp
  FakeAuthenticator.h
  FakeKeychainService.cpp
  FakeKeychainService.h
  NoteStoreServer.cpp
  NoteStoreServer.h
  SyncEventsCollector.cpp
  SyncEventsCollector.h
  UserStoreServer.cpp
  UserStoreServer.h
  note_store/Checks.cpp
  note_store/Checks.h
  note_store/Compat.h
  note_store/LinkedNotebooks.h
  note_store/Notebooks.h
  note_store/Notes.h
  note_store/Resources.h
  note_store/SavedSearches.h
  note_store/Setup.cpp
  note_store/Setup.h
  note_store/Tags.h
  note_store/TestData.h
  utils/ExceptionUtils.cpp
  utils/ExceptionUtils.h
  utils/HttpRequestData.h
  utils/HttpRequestParser.cpp
  utils/HttpRequestParser.h
  utils/HttpUtils.cpp
  utils/HttpUtils.h)

target_link_libraries(${TESTS_LIB_NAME}
  PUBLIC
    ${LIBNAME}_interface
    ${QT_LIBRARIES}
    Qt5::Test)
