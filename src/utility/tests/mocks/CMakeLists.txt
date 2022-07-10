cmake_minimum_required(VERSION 3.10.2)

set(MOCKS_NAME ${LIBNAME}_utility_testing_mocks)
add_library(${MOCKS_NAME} STATIC)

message(STATUS "MOCKS_NAME = ${MOCKS_NAME}")

if(POLICY CMP0076)
  cmake_policy(SET CMP0076 NEW)
endif()

target_sources(${MOCKS_NAME}
  PUBLIC
    ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/utility/tests/mocks/MockIKeychainService.h
  PRIVATE
    CheckBuild.cpp
)

target_link_libraries(${MOCKS_NAME}
  PUBLIC
    ${LIBNAME}_interface
    ${GMOCK_LIBRARIES}
)

target_include_directories(${MOCKS_NAME}
  PUBLIC
    ${GMOCK_INCLUDE_DIRS}
)

install(
  FILES ${LIBQUENTIER_SOURCE_ROOT_DIR}/headers/quentier/utility/tests/mocks/MockIKeychainService.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/utility/tests/mocks
)
