find_package(Backtrace QUIET)
if(Backtrace_FOUND)
  message(STATUS "Found libbacktrace")
  include_directories(${Backtrace_INCLUDE_DIRS})
endif()
