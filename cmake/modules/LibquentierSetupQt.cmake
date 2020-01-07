find_package(Qt5Core 5.5 REQUIRED)
message(STATUS "Found Qt5 installation, version ${Qt5Core_VERSION}")

find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5PrintSupport REQUIRED)

if(BUILD_WITH_NOTE_EDITOR OR BUILD_WITH_AUTHENTICATION_MANAGER)
  if(USE_QT5_WEBKIT)
    find_package(Qt5WebKit REQUIRED)
    find_package(Qt5WebKitWidgets REQUIRED)
  elseif(Qt5Core_VERSION VERSION_LESS "5.6.0")
    find_package(Qt5WebEngine REQUIRED)
    set(QUENTIER_USE_QT_WEB_ENGINE TRUE)
  else()
    find_package(Qt5WebEngineCore REQUIRED)
    set(QUENTIER_USE_QT_WEB_ENGINE TRUE)
  endif()
  if(NOT USE_QT5_WEBKIT)
    find_package(Qt5WebEngineWidgets REQUIRED)
    add_definitions(-DQUENTIER_USE_QT_WEB_ENGINE)
  endif()
endif()

find_package(Qt5Xml REQUIRED)
find_package(Qt5Sql REQUIRED)
find_package(Qt5Test REQUIRED)
find_package(Qt5LinguistTools REQUIRED)

list(APPEND QT_INCLUDES
  ${Qt5Core_INCLUDE_DIRS}
  ${Qt5Gui_INCLUDE_DIRS}
  ${Qt5Network_INCLUDE_DIRS}
  ${Qt5Widgets_INCLUDE_DIRS}
  ${Qt5PrintSupport_INCLUDE_DIRS}
  ${Qt5Xml_INCLUDE_DIRS}
  ${Qt5Sql_INCLUDE_DIRS}
  ${Qt5Test_INCLUDE_DIRS}
  ${Qt5LinguistTools_INCLUDE_DIRS})

list(APPEND QT_LIBRARIES
  ${Qt5Core_LIBRARIES}
  ${Qt5Gui_LIBRARIES}
  ${Qt5Network_LIBRARIES}
  ${Qt5Widgets_LIBRARIES}
  ${Qt5PrintSupport_LIBRARIES}
  ${Qt5Xml_LIBRARIES}
  ${Qt5Sql_LIBRARIES}
  ${Qt5Test_LIBRARIES}
  ${Qt5LinguistTools_LIBRARIES})

list(APPEND QT_DEFINITIONS
  ${Qt5Core_DEFINITIONS}
  ${Qt5Gui_DEFINITIONS}
  ${Qt5Network_DEFINITIONS}
  ${Qt5Widgets_DEFINITIONS}
  ${Qt5PrintSupport_DEFINITIONS}
  ${Qt5Xml_DEFINITIONS}
  ${Qt5Sql_DEFINITIONS}
  ${Qt5Test_DEFINITIONS}
  ${Qt5LinguistTools_DEFINITIONS})

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows" AND
    NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  find_package(Qt5DBus REQUIRED)
  list(APPEND QT_INCLUDES ${QT_INCLUDES} ${Qt5DBus_INCLUDE_DIRS})
  list(APPEND QT_LIBRARIES ${QT_LIBRARIES} ${Qt5DBus_LIBRARIES})
  list(APPEND QT_DEFINITIONS ${QT_DEFINITIONS} ${Qt5DBus_DEFINITIONS})
endif()

if(BUILD_WITH_NOTE_EDITOR OR BUILD_WITH_AUTHENTICATION_MANAGER)
  if(USE_QT5_WEBKIT)
    list(APPEND QT_INCLUDES
      ${QT_INCLUDES}
      ${Qt5WebKit_INCLUDE_DIRS}
      ${Qt5WebKitWidgets_INCLUDE_DIRS})

    list(APPEND QT_LIBRARIES
      ${QT_LIBRARIES}
      ${Qt5WebKit_LIBRARIES}
      ${Qt5WebKitWidgets_LIBRARIES})

    list(APPEND QT_DEFINITIONS
      ${QT_DEFINITIONS}
      ${Qt5WebKit_DEFINITIONS}
      ${Qt5WebKitWidgets_DEFINITIONS})
  else()
    find_package(Qt5WebSockets REQUIRED)
    find_package(Qt5WebChannel REQUIRED)

    list(APPEND QT_INCLUDES
      ${QT_INCLUDES}
      ${Qt5WebEngine_INCLUDE_DIRS}
      ${Qt5WebEngineWidgets_INCLUDE_DIRS}
      ${Qt5WebSockets_INCLUDE_DIRS}
      ${Qt5WebChannel_INCLUDE_DIRS})

    list(APPEND QT_LIBRARIES
      ${QT_LIBRARIES}
      ${Qt5WebEngine_LIBRARIES}
      ${Qt5WebEngineWidgets_LIBRARIES}
      ${Qt5WebSockets_LIBRARIES}
      ${Qt5WebChannel_LIBRARIES})

    list(APPEND QT_DEFINITIONS
      ${QT_DEFINITIONS}
      ${Qt5WebEngine_DEFINITIONS}
      ${Qt5WebEngineWidgets_DEFINITIONS}
      ${Qt5WebSockets_DEFINITIONS}
      ${Qt5WebChannel_DEFINITIONS})

    if(NOT Qt5WebEngineWidgets_VERSION VERSION_LESS "5.6.0")
      list(APPEND QT_INCLUDES ${QT_INCLUDES} ${Qt5WebEngineCore_INCLUDE_DIRS})
      list(APPEND QT_LIBRARIES ${QT_LIBRARIES} ${Qt5WebEngineCore_LIBRARIES})
      list(APPEND QT_DEFINITIONS ${QT_DEFINITIONS} ${Qt5WebEngineCore_DEFINITIONS})
    endif()
  endif()
endif()

macro(qt_add_translation)
  qt5_add_translation(${ARGN})
endmacro()
macro(qt_create_translation)
  qt5_create_translation(${ARGN})
endmacro()
macro(qt_add_resources)
  qt5_add_resources(${ARGN})
endmacro()
macro(qt_wrap_ui)
  qt5_wrap_ui(${ARGN})
endmacro()

list(REMOVE_DUPLICATES QT_INCLUDES)
list(REMOVE_DUPLICATES QT_LIBRARIES)
list(REMOVE_DUPLICATES QT_DEFINITIONS)

include_directories(SYSTEM "${QT_INCLUDES} ${SYSTEM}")
add_definitions(${QT_DEFINITIONS})

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR "ON")
