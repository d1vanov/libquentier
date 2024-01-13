cmake_minimum_required(VERSION 3.16.3)

set(TESTS_NAME "${LIBNAME}_synchronization_integrational_tests")
set(TESTS_LIB_NAME "${TESTS_NAME}_lib")

add_library(${TESTS_LIB_NAME} STATIC 
  FakeAuthenticator.cpp
  FakeAuthenticator.h
  FakeKeychainService.cpp
  FakeKeychainService.h
  FakeSyncStateStorage.cpp
  FakeSyncStateStorage.h
  NoteStoreServer.cpp
  NoteStoreServer.h
  Setup.cpp
  Setup.h
  StopSynchronizationErrorTrigger.h
  SyncEventsCollector.cpp
  SyncEventsCollector.h
  TestData.h
  TestScenarioData.h
  TestScenarios.h
  TestRunner.cpp
  TestRunner.h
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
  note_store/Tags.h
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
    Qt${LIBQUENTIER_QT_VERSION}::Test)

add_executable(${TESTS_NAME}
  main.cpp)

target_link_libraries(${TESTS_NAME} ${TESTS_LIB_NAME} ${LIBNAME})

add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME})
