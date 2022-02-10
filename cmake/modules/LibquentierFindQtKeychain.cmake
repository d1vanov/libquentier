find_package(Qt5Keychain QUIET REQUIRED)

include_directories(${QTKEYCHAIN_INCLUDE_DIRS})

get_property(QTKEYCHAIN_LIBRARY_LOCATION TARGET Qt5Keychain::Qt5Keychain PROPERTY LOCATION)
message(STATUS "Found QtKeychain library: ${QTKEYCHAIN_LIBRARY_LOCATION}")

get_filename_component(QTKEYCHAIN_LIB_DIR "${QTKEYCHAIN_LIBRARY_LOCATION}" PATH)
