cmake_minimum_required(VERSION 3.16.3)

add_subdirectory(mocks)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)
set(TESTS_RESOURCES_NAME ${TESTS_NAME}_resources)

if(BUILD_WITH_QT6)
  qt6_add_resources(${TESTS_RESOURCES_NAME}
    tests.qrc)
else()
  qt5_add_resources(${TESTS_RESOURCES_NAME}
    tests.qrc)
endif()

add_executable(${TESTS_NAME}
  ConnectionPoolTest.cpp
  LinkedNotebooksHandlerTest.cpp
  LocalStorageTest.cpp
  main.cpp
  NotebooksHandlerTest.cpp
  NotesHandlerTest.cpp
  Patch1To2Test.cpp
  Patch2To3Test.cpp
  PatchUtilsTest.cpp
  ResourcesHandlerTest.cpp
  SavedSearchesHandlerTest.cpp
  SynchronizationInfoHandlerTest.cpp
  TablesInitializerTest.cpp
  TagsHandlerTest.cpp
  VersionHandlerTest.cpp
  UsersHandlerTest.cpp
  Utils.cpp
  Utils.h
  ${${TESTS_RESOURCES_NAME}})

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}_interface
    ${LIBNAME}
    ${LIBRARY_NAME}
    ${LIBRARY_NAME}_testing_mocks
    ${GMOCK_LIBRARIES}
    GTest::GTest
    Qt${LIBQUENTIER_QT_VERSION}::Core
    Qt${LIBQUENTIER_QT_VERSION}::Sql)

add_test(${TESTS_NAME} ${TESTS_NAME})

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
