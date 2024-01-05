cmake_minimum_required(VERSION 3.16.3)

add_subdirectory(mocks)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

add_executable(${TESTS_NAME}
  main.cpp
  CompositeKeychainServiceTest.cpp
  MigratingKeychainServiceTest.cpp
  ObfuscatingKeychainServiceTest.cpp
)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}
    ${LIBRARY_NAME}
    ${LIBRARY_NAME}_testing_mocks
    ${QTKEYCHAIN_LIBRARIES}
    GTest::GTest
    Qt${LIBQUENTIER_QT_VERSION}::Core)

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
