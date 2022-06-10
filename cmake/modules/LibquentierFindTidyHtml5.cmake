find_path(TIDY_HTML5_INCLUDE_DIRS
  NAMES tidy.h
  PATHS ${TIDY_HTML5_ROOT} ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES include include/tidy)

if(NOT TIDY_HTML5_INCLUDE_DIRS)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library")
endif()

if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIRS}/tidyenum.h)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library: tidyenum.h is missing; found include dir is ${TIDY_HTML5_INCLUDE_DIRS}")
endif()

if(NOT EXISTS ${TIDY_HTML5_INCLUDE_DIRS}/tidyplatform.h)
  message(FATAL_ERROR "Can't find development headers for tidy-html5 library: tidyplatform.h is missing; found include dir is ${TIDY_HTML5_INCLUDE_DIRS}")
endif()

if(NOT TIDY_HTML5_LIBRARIES)
  if(MSVC)
    find_library(TIDY_HTML5_LIBRARIES
      NAMES
      tidys.lib
      PATHS ${TIDY_HTML5_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib)

    if(NOT TIDY_HTML5_LIBRARIES)
      message(FATAL_ERROR "Can't find tidy-html5 import library")
    endif()

    find_path(TIDY_HTML5_DLL
      NAMES tidy.dll
      PATHS ${TIDY_HTML5_ROOT} ${CMAKE_PREFIX_PATH}
      PATH_SUFFIXES bin)

    if(NOT TIDY_HTML5_DLL)
      message(FATAL_ERROR "Can't find tidy-html5 dll")
    endif()
  else()
    find_library(TIDY_HTML5_LIBRARIES
      NAMES
      libtidy.so libtidy.dylib
      PATHS ${TIDY_HTML5_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib)

    if(NOT TIDY_HTML5_LIBRARIES)
      message(FATAL_ERROR "Can't find tidy-html5 shared library")
    endif()
  endif()
endif()

if(NOT TARGET TidyHtml5::TidyHtml5)
  add_library(TidyHtml5::TidyHtml5 SHARED IMPORTED)
  set_target_properties(TidyHtml5::TidyHtml5 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${TIDY_HTML5_INCLUDE_DIRS}")
  if(MSVC)
    set_target_properties(TidyHtml5::TidyHtml5 PROPERTIES
      IMPORTED_LOCATION "${TIDY_HTML5_DLL}")
    set_target_properties(TidyHtml5::TidyHtml5 PROPERTIES
      IMPORTED_IMPLIB "${TIDY_HTML5_LIBRARIES}")
  else()
    set_target_properties(TidyHtml5::TidyHtml5 PROPERTIES
      IMPORTED_LOCATION "${TIDY_HTML5_LIBRARIES}")
  endif()
endif()

get_filename_component(_TIDY_HTML5_LIB_DIR "${TIDY_HTML5_LIBRARIES}" DIRECTORY)
get_filename_component(_TIDY_HTML5_LIB_NAME "${TIDY_HTML5_LIBRARIES}" NAME)
message(STATUS "Found tidy-html5 library: ${_TIDY_HTML5_LIB_DIR}/${_TIDY_HTML5_LIB_NAME}")
