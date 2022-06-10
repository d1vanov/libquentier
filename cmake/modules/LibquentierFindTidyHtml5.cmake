set(TIDY_HTML5_FOUND FALSE)

find_path(TIDY_HTML5_INCLUDE_DIR
  NAMES tidy.h
  PATHS ${TIDY_HTML5_ROOT} ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES include include/tidy)

if(NOT TIDY_HTML5_INCLUDE_DIR)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library")
endif()

if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIR}/tidyenum.h)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library: tidyenum.h is missing; found include dir is ${TIDY_HTML5_INCLUDE_DIR}")
endif()

if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIR}/tidyplatform.h)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library: tidyplatform.h is missing; found include dir is ${TIDY_HTML5_INCLUDE_DIR}")
endif()

if(MSVC)
  find_library(TIDY_HTML5_IMPORT_LIBRARY
    NAMES
    tidys.lib
    PATHS ${TIDY_HTML5_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib)

  if(NOT TIDY_HTML5_IMPORT_LIBRARY)
    message(FATAL_ERROR "Can't find tidy-html5 import library")
  endif()

  get_filename_component(TIDY_HTML5_LIB_DIR "${TIDY_HTML5_IMPORT_LIBRARY}" DIRECTORY)
  get_filename_component(TIDY_HTML5_LIB_NAME "${TIDY_HTML5_IMPORT_LIBRARY}" NAME)
else()
  find_library(TIDY_HTML5_SHARED_LIBRARY
    NAMES
    libtidy.so libtidy.dylib
    PATHS ${TIDY_HTML5_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib)

  if(NOT TIDY_HTML5_SHARED_LIBRARY)
    message(FATAL_ERROR "Can't find tidy-html5 shared library")
  endif()

  get_filename_component(TIDY_HTML5_LIB_DIR "${TIDY_HTML5_SHARED_LIBRARY}" DIRECTORY)
  get_filename_component(TIDY_HTML5_LIB_NAME "${TIDY_HTML5_SHARED_LIBRARY}" NAME)
endif()

add_library(tidy_html5 SHARED IMPORTED)
if(MSVC)
  set_target_properties(tidy_html5 PROPERTIES
    IMPORTED_IMPLIB ${TIDY_HTML5_IMPORT_LIBRARY}
    IMPORTED_LOCATION ${TIDY_HTML5_IMPORT_LIBRARY}
  )
else()
  set_target_properties(tidy_html5 PROPERTIES
    IMPORTED_LOCATION ${TIDY_HTML5_SHARED_LIBRARY}
  )
endif()

set(TIDY_HTML5_LIBRARIES "tidy_html5")

message(STATUS "Found tidy-html5 library: ${TIDY_HTML5_LIB_DIR}/${TIDY_HTML5_LIB_NAME}")
include_directories(SYSTEM ${TIDY_HTML5_INCLUDE_DIR})

set(TIDY_HTML5_FOUND TRUE)
