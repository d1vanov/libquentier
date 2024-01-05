if(CMAKE_COMPILER_IS_GNUCXX)
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  message(STATUS "Using GNU C++ compiler, version ${GCC_VERSION}")
  set(CMAKE_CXX_FLAGS "-Wno-uninitialized -fvisibility=hidden -fPIC -rdynamic -ldl ${CMAKE_CXX_FLAGS}")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang") # NOTE: MATCHES is required, STREQUAL leads to problems with AppleClang
  execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE CLANG_VERSION)
  message(STATUS "Using LLVM/Clang C++ compiler, version info: ${CLANG_VERSION}")
  set(CMAKE_CXX_FLAGS "-Wno-uninitialized -Wno-null-conversion -Wno-format -Wno-deprecated ${CMAKE_CXX_FLAGS}")
  if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    set(CMAKE_CXX_FLAGS "-fPIC ${CMAKE_CXX_FLAGS}")
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS "-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS /bigobj ${CMAKE_CXX_FLAGS}")
  add_definitions("-DUNICODE -D_UNICODE")
endif()
