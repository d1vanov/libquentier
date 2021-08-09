cmake_minimum_required(VERSION 3.10.2)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)
set(TESTS_RESOURCES_NAME ${TESTS_NAME}_resources)

qt5_add_resources(${TESTS_RESOURCES_NAME}
  tests.qrc)

add_executable(${TESTS_NAME}
  ConnectionPoolTest.cpp
  LinkedNotebooksHandlerTest.cpp
  main.cpp
  NotebooksHandlerTest.cpp
  Patch1To2Test.cpp
  Patch2To3Test.cpp
  PatchUtilsTest.cpp
  ResourcesHandlerTest.cpp
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
    ${GMOCK_LIBRARIES}
    GTest::GTest
    Qt5::Core
    Qt5::Sql)

target_compile_definitions(${TESTS_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
