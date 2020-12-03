if("${LIBQUENTIER_FIND_PACKAGE_ARG}" STREQUAL "")
  set(LIBQUENTIER_FIND_PACKAGE_ARG "QUIET")
else()
  set(LIBQUENTIER_FIND_PACKAGE_ARG "")
endif()

LibquentierFindPackageWrapper(QEverCloud-qt5 ${LIBQUENTIER_FIND_PACKAGE_ARG})
LibquentierFindPackageWrapper(Qt5Keychain ${LIBQUENTIER_FIND_PACKAGE_ARG})
LibquentierFindPackageWrapper(LibXml2 ${LIBQUENTIER_FIND_PACKAGE_ARG})
LibquentierFindPackageWrapper(OpenSSL ${LIBQUENTIER_FIND_PACKAGE_ARG})

add_library(Libxml2::Libxml2 SHARED IMPORTED)
if(MSVC)
  set_target_properties(Libxml2::Libxml2 PROPERTIES
    IMPORTED_IMPLIB ${LIBXML2_LIBRARIES}
  )
else()
  set_target_properties(Libxml2::Libxml2 PROPERTIES
    IMPORTED_LOCATION ${LIBXML2_LIBRARIES}
  )
endif()
