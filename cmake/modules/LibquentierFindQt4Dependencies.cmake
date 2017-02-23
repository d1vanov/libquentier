if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows" AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  find_package(Qt4 COMPONENTS QTCORE QTGUI QTNETWORK QTWEBKIT QTXML QTSQL QTTEST QTDBUS QUIET REQUIRED)
else()
  find_package(Qt4 COMPONENTS QTCORE QTGUI QTNETWORK QTWEBKIT QTXML QTSQL QTTEST QUIET REQUIRED)
endif()
include(${QT_USE_FILE})

# Workaround what seems to be a CMake 3.x bug with Qt4 libraries
list(FIND QT_LIBRARIES "${QT_QTGUI_LIBRARY}" HasGui)
if(HasGui EQUAL -1)
  list(APPEND QT_LIBRARIES ${QT_QTGUI_LIBRARY})
endif()
