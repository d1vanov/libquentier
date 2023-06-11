cmake_minimum_required(VERSION 3.10.2)

set(TESTS_NAME "${LIBNAME}_synchronization_integrational_tests")
set(TESTS_LIB_NAME "${TESTS_NAME}_lib")

add_library(${TESTS_LIB_NAME} STATIC 
  FakeAuthenticator.cpp
  FakeAuthenticator.h
  FakeKeychainService.cpp
  FakeKeychainService.h
  UserStoreServer.cpp
  UserStoreServer.h
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
