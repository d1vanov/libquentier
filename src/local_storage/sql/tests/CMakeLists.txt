cmake_minimum_required(VERSION 3.10.2)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  ConnectionPoolTest.cpp
  main.cpp)

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
