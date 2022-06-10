find_package(LibXml2 REQUIRED)
if(LIBXML2_FOUND)
  include_directories(SYSTEM ${LIBXML2_INCLUDE_DIR})
  get_filename_component(LIBXML2_LIB_DIR "${LIBXML2_LIBRARIES}" PATH)

  add_library(Libxml2::Libxml2 SHARED IMPORTED)
  set_target_properties(Libxml2::Libxml2 PROPERTIES
    IMPORTED_LOCATION ${LIBXML2_LIBRARIES}
  )
  if(MSVC)
    set_target_properties(Libxml2::Libxml2 PROPERTIES
      IMPORTED_IMPLIB ${LIBXML2_LIBRARIES}
    )
  endif()
endif()
