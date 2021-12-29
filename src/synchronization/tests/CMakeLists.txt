cmake_minimum_required(VERSION 3.10.2)

add_subdirectory(mocks)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  main.cpp
  SimpleNotebookSyncConflictResolverTest.cpp
  SimpleSyncConflictResolverTest.cpp)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}_private
    ${LIBNAME}
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
    GTest::GTest
    Qt5::Core)

target_compile_definitions(${TESTS_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
