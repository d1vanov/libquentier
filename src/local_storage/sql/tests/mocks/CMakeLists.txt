cmake_minimum_required(VERSION 3.10.2)

set(MOCKS_NAME ${LIBRARY_NAME}_testing_mocks)
add_library(${MOCKS_NAME} STATIC)

target_sources(${MOCKS_NAME}
  PUBLIC
    MockILinkedNotebooksHandler.h
    MockINotebooksHandler.h
  PRIVATE
    CheckBuild.cpp
)

target_link_libraries(${MOCKS_NAME}
  PUBLIC
    ${LIBRARY_NAME}
    ${GMOCK_LIBRARIES}
)
