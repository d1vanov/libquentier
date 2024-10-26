cmake_minimum_required(VERSION 3.16.3)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  main.cpp
  FactoryTest.cpp
  FutureWhenAllTest.cpp
  MapFutureProgressTest.cpp
  TrackedTaskTest.cpp)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
    GTest::GTest)

add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME})
