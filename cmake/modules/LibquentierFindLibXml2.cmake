find_package(LibXml2 REQUIRED)
if(LIBXML2_FOUND)
  include_directories(${LIBXML2_INCLUDE_DIR})
  get_filename_component(LIBXML2_LIB_DIR "${LIBXML2_LIBRARIES}" PATH)

  add_library(Libxml2 SHARED IMPORTED)
  if(MSVC)
    set_target_properties(Libxml2 PROPERTIES
      IMPORTED_IMPLIB ${LIBXML2_LIBRARIES}
    )
  else()
    set_target_properties(Libxml2 PROPERTIES
      IMPORTED_LOCATION ${LIBXML2_LIBRARIES}
    )
  endif()
endif()
