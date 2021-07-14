cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_local_storage_sql)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  ConnectionPool.cpp
  ConnectionPool.h
  ErrorHandling.h
  LinkedNotebooksHandler.cpp
  LinkedNotebooksHandler.h
  NotebooksHandler.cpp
  NotebooksHandler.h
  Notifier.cpp
  Notifier.h
  NotifierProxy.cpp
  NotifierProxy.h
  ResourcesHandler.cpp
  ResourcesHandler.h
  TablesInitializer.cpp
  TablesInitializer.h
  TagsHandler.cpp
  TagsHandler.h
  Tasks.h
  Transaction.cpp
  Transaction.h
  TypeChecks.cpp
  TypeChecks.h
  VersionHandler.cpp
  VersionHandler.h
  UsersHandler.cpp
  UsersHandler.h
  patches/Patch1To2.cpp
  patches/Patch1To2.h
  patches/Patch2To3.cpp
  patches/Patch2To3.h
  patches/PatchBase.cpp
  patches/PatchBase.h
  patches/PatchUtils.cpp
  patches/PatchUtils.h
  utils/Common.h
  utils/FillFromSqlRecordUtils.cpp
  utils/FillFromSqlRecordUtils.h
  utils/ListFromDatabaseUtils.h
  utils/ListFromDatabaseUtils.cpp
  utils/NotebookUtils.cpp
  utils/NotebookUtils.h
  utils/PutToDatabaseUtils.cpp
  utils/PutToDatabaseUtils.h
  utils/RemoveFromDatabaseUtils.cpp
  utils/RemoveFromDatabaseUtils.h
  utils/ResourceDataFilesUtils.cpp
  utils/ResourceDataFilesUtils.h
  utils/ResourceUtils.cpp
  utils/ResourceUtils.h
  utils/SqlUtils.cpp
  utils/SqlUtils.h
  utils/TagUtils.cpp
  utils/TagUtils.h)

target_link_libraries(${LIBRARY_NAME}
  PUBLIC
  ${LIBNAME}_private
  ${LIBNAME}_utility
  Qt5::Core
  Qt5::Sql)

target_compile_definitions(${LIBRARY_NAME}
  PRIVATE
  "-DBUILDING_LIBQUENTIER_STATIC_LIBRARY")

if(GMOCK_FOUND AND GTEST_FOUND)
  add_subdirectory(tests)
endif()
