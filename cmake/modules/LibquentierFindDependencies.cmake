set(_LIBQUENTIER_QT_COMPONENTS
  Core
  Gui
  LinguistTools
  Network
  PrintSupport
  Sql
  Test
  Xml
  Widgets)
if(LIBQUENTIER_HAS_NOTE_EDITOR)
  list(APPEND _LIBQUENTIER_QT_COMPONENTS
    WebChannel
    WebEngineCore
    WebEngineWidgets
    WebSockets
  )
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows" AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  list(APPEND _LIBQUENTIER_QT_COMPONENTS
    DBus
  )
endif()

if(LIBQUENTIER_USE_QT6)
  if(LIBQUENTIER_HAS_NOTE_EDITOR)
    set(_LIBQUENTIER_MIN_QT_VERSION 6.4.0)
  else()
    set(_LIBQUENTIER_MIN_QT_VERSION 6.0.0)
  endif()
  find_package(Qt6 ${_LIBQUENTIER_MIN_QT_VERSION} COMPONENTS ${_LIBQUENTIER_QT_COMPONENTS} ${LIBQUENTIER_FIND_DEPS_ARGS})
  if(NOT "QUIET" IN_LIST LIBQUENTIER_FIND_DEPS_ARGS)
    message(STATUS "Found Qt6 installation, version ${Qt6Core_VERSION}")
  endif()

  find_package(QEverCloud-qt6 ${LIBQUENTIER_FIND_DEPS_ARGS})
  find_package(Qt6Keychain ${LIBQUENTIER_FIND_DEPS_ARGS})
else()
  find_package(Qt5 5.12.0 COMPONENTS ${_LIBQUENTIER_QT_COMPONENTS} ${LIBQUENTIER_FIND_DEPS_ARGS})
  if(NOT "QUIET" IN_LIST LIBQUENTIER_FIND_DEPS_ARGS)
    message(STATUS "Found Qt5 installation, version ${Qt5Core_VERSION}")
  endif()

  find_package(QEverCloud-qt5 ${LIBQUENTIER_FIND_DEPS_ARGS})
  find_package(Qt5Keychain ${LIBQUENTIER_FIND_DEPS_ARGS})
  if(NOT "QUIET" IN_LIST LIBQUENTIER_FIND_DEPS_ARGS)
    get_property(QTKEYCHAIN_LIBRARY_LOCATION TARGET ${QTKEYCHAIN_LIBRARIES} PROPERTY LOCATION)
    message(STATUS "Found QtKeychain library: ${QTKEYCHAIN_LIBRARY_LOCATION}")
  endif()
  if(NOT TARGET Qt5Keychain::Qt5Keychain)
    add_library(Qt5Keychain::Qt5Keychain INTERFACE IMPORTED)
    set_target_properties(Qt5Keychain::Qt5Keychain PROPERTIES INTERFACE_LINK_LIBRARIES ${QTKEYCHAIN_LIBRARIES})
    target_include_directories(Qt5Keychain::Qt5Keychain INTERFACE ${QTKEYCHAIN_INCLUDE_DIRS})
    if(NOT "QUIET" IN_LIST LIBQUENTIER_FIND_DEPS_ARGS)
      message(STATUS "QtKeychain library include dirs: ${QTKEYCHAIN_INCLUDE_DIRS}")
    endif()
  endif()
endif()

find_package(LibXml2 ${LIBQUENTIER_FIND_DEPS_ARGS})
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

find_package(OpenSSL ${LIBQUENTIER_FIND_DEPS_ARGS})
if (${OPENSSL_VERSION} VERSION_LESS "1.1.1")
  message(FATAL "OpenSSL >= 1.1.1 is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/LibquentierFindTidyHtml5.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/LibquentierFindHunspell.cmake")
