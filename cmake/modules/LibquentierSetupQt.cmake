set(QT_COMPONENTS
  Core
  Gui
  LinguistTools
  Network
  PrintSupport
  Sql
  Test
  Xml
  Widgets)
if(BUILD_WITH_NOTE_EDITOR)
  list(APPEND QT_COMPONENTS
    WebChannel
    WebEngineCore
    WebEngineWidgets
    WebSockets
  )
endif()

if(BUILD_WITH_QT6)
  if(BUILD_WITH_NOTE_EDITOR)
    set(MIN_QT_VERSION 6.4.0)
  else()
    set(MIN_QT_VERSION 6.0.0)
  endif()
  find_package(Qt6 6.4.0 COMPONENTS ${QT_COMPONENTS})
  message(STATUS "Found Qt6 installation, version ${Qt6Core_VERSION}")
else()
  find_package(Qt5 5.12.0 COMPONENTS ${QT_COMPONENTS})
  message(STATUS "Found Qt5 installation, version ${Qt5Core_VERSION}")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR "ON")
