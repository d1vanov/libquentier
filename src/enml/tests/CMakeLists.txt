cmake_minimum_required(VERSION 3.16.3)

set(TESTS_NAME ${LIBRARY_NAME}_unit_tests)

if(BUILD_WITH_QT6)
  qt6_add_resources(${TESTS_NAME}_RESOURCES_RCC ../../tests/resources/test_resources.qrc)
else()
  qt5_add_resources(${TESTS_NAME}_RESOURCES_RCC ../../tests/resources/test_resources.qrc)
endif()

add_executable(${TESTS_NAME}
  main.cpp
  ConverterTest.cpp
  EnexConverterTest.cpp
  ${${TESTS_NAME}_RESOURCES_RCC}
)

target_link_libraries(${TESTS_NAME}
  PRIVATE
    ${LIBNAME}
    ${LIBRARY_NAME}
    GTest::GTest
    Qt${LIBQUENTIER_QT_VERSION}::Core)

add_test(${TESTS_NAME} ${TESTS_NAME}) 

include(GoogleTest)

gtest_discover_tests(${TESTS_NAME})
