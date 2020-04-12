set(TIDY_HTML5_FOUND FALSE)

if(NOT TIDY_HTML5_INCLUDE_DIR AND NOT TIDY_HTML5_LIBRARIES)
  find_path(TIDY_HTML5_INCLUDE_DIR
    NAMES tidy.h
    PATHS ${TIDY_HTML5_ROOT} ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include include/tidy)

  if(NOT TIDY_HTML5_INCLUDE_DIR)
    message(FATAL_ERROR "Can't find development headers for tidy-html5 library")
  endif()

  if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIR}/tidyenum.h)
    message(FATAL_ERROR "Can't find development headers for tidy-html5 library (tidyenum.h is missing)")
  endif()

  if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIR}/tidyplatform.h)
    message(FATAL_ERROR "Can't find development headers for tidy-html5 library (tidyplatform.h is missing)")
  endif()

  find_library(TIDY_HTML5_LIBRARIES
    NAMES
    libtidy5.so libtidy5.dylib libtidy5.a libtidy5.dll.a libtidy5.lib libtidy5s.lib
    tidy5.so tidy5.dylib tidy5.a tidy5.dll.a tidy5.lib tidy5s.lib
    libtidy.so libtidy.dylib libtidy.a libtidy.dll.a libtidy.lib libtidys.lib
    tidy.so tidy.dylib tidy.a tidy.dll.a tidy.lib tidys.lib
    PATHS ${TIDY_HTML5_ROOT} ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib)

  if(NOT TIDY_HTML5_LIBRARIES)
    message(FATAL_ERROR "Can't find tidy-html5 library")
  endif()
endif()

message(STATUS "Found tidy-html5 library: ${TIDY_HTML5_LIBRARIES}")
include_directories(${TIDY_HTML5_INCLUDE_DIR})

get_filename_component(TIDY_HTML5_LIB_DIR "${TIDY_HTML5_LIBRARIES}" PATH)
set(TIDY_HTML5_FOUND TRUE)
