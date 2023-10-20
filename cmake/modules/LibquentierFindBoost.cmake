find_package(Boost 1.71.0 REQUIRED)
if(Boost_FOUND)
  include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
endif()
