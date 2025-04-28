cmake_minimum_required(VERSION 3.16.3)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/sanitizers ${CMAKE_MODULE_PATH})

include(LibquentierCMakePolicies)
SET_POLICIES()

project(libquentier VERSION 0.8.0)

set(PROJECT_VENDOR "Dmitry Ivanov")
set(PROJECT_COPYRIGHT_YEAR "2016-2024")
set(PROJECT_DOMAIN_FIRST "libquentier")
set(PROJECT_DOMAIN_SECOND "org")
set(PROJECT_DOMAIN "${PROJECT_DOMAIN_FIRST}.${PROJECT_DOMAIN_SECOND}")

option(BUILD_WITH_NOTE_EDITOR "Build libquentier with note editor UI component" ON)
option(BUILD_WITH_QT6 "Build with Qt6" OFF)

if(BUILD_WITH_QT6)
  set(LIBQUENTIER_QT_VERSION 6)
  set(LIBQUENTIER_USE_QT6 TRUE)
else()
  set(LIBQUENTIER_QT_VERSION 5)
  set(LIBQUENTIER_USE_QT6 FALSE)
endif()

set(LIBQUENTIER_FIND_DEPS_ARGS REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR "ON")

set(LIBQUENTIER_HAS_NOTE_EDITOR ${BUILD_WITH_NOTE_EDITOR})

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

include(LibquentierCompilerSettings)
include(LibquentierAdditionalCompilerWarnings)
include(LibquentierFindBoost)
include(LibquentierFindDependencies)
include(LibquentierUpdateTranslations)
include(LibquentierSetupDoxygen)

find_package(Sanitizers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(UNIX)
  set(CMAKE_STATIC_LIBRARY_PREFIX "")
endif()

set(LIBQUENTIER_SOURCE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

define_property(SOURCE
  PROPERTY skip_autoformat
  BRIEF_DOCS
  "Source files marked with this property should not be autoformatted"
  FULL_DOCS
  "skip_autoformat source property controls whether the source file "
  "should be not formatted automatically as a part of clang-format target")

if(BUILD_WITH_NOTE_EDITOR)
  set(NOTE_EDITOR_HEADERS
      headers/quentier/note_editor/NoteEditor.h
      headers/quentier/note_editor/INoteEditorBackend.h
      headers/quentier/note_editor/SpellChecker.h)
endif()

set(TYPES_HEADERS
    headers/quentier/types/Account.h
    headers/quentier/types/ErrorString.h
    headers/quentier/types/Fwd.h
    headers/quentier/types/NoteUtils.h
    headers/quentier/types/Result.h
    headers/quentier/types/ResourceRecognitionIndexItem.h
    headers/quentier/types/ResourceRecognitionIndices.h
    headers/quentier/types/ResourceUtils.h
    headers/quentier/types/Validation.h
    headers/quentier/types/RegisterMetatypes.h)

set(ENML_HEADERS
    headers/quentier/enml/Factory.h
    headers/quentier/enml/Fwd.h
    headers/quentier/enml/HtmlUtils.h
    headers/quentier/enml/IConverter.h
    headers/quentier/enml/IDecryptedTextCache.h
    headers/quentier/enml/IENMLTagsConverter.h
    headers/quentier/enml/IHtmlData.h)

set(ENML_CONVERSION_RULES_HEADERS
    headers/quentier/enml/conversion_rules/Factory.h
    headers/quentier/enml/conversion_rules/Fwd.h
    headers/quentier/enml/conversion_rules/ISkipRule.h
    headers/quentier/enml/conversion_rules/ISkipRuleBuilder.h
    headers/quentier/enml/conversion_rules/MatchMode.h)

set(LOCAL_STORAGE_HEADERS
    headers/quentier/local_storage/Factory.h
    headers/quentier/local_storage/Fwd.h
    headers/quentier/local_storage/ILocalStorage.h
    headers/quentier/local_storage/ILocalStorageNotifier.h
    headers/quentier/local_storage/IPatch.h
    headers/quentier/local_storage/LocalStorageOpenException.h
    headers/quentier/local_storage/LocalStorageOperationException.h
    headers/quentier/local_storage/NoteSearchQuery.h)

set(SYNCHRONIZATION_HEADERS
    headers/quentier/synchronization/Factory.h
    headers/quentier/synchronization/Fwd.h
    headers/quentier/synchronization/IAuthenticator.h
    headers/quentier/synchronization/INoteStoreFactory.h
    headers/quentier/synchronization/ISyncStateStorage.h
    headers/quentier/synchronization/ISyncConflictResolver.h
    headers/quentier/synchronization/ISyncEventsNotifier.h
    headers/quentier/synchronization/ISynchronizer.h
    headers/quentier/synchronization/IUserStoreFactory.h
)

set(SYNCHRONIZATION_TYPES_HEADERS
    headers/quentier/synchronization/types/Errors.h
    headers/quentier/synchronization/types/Fwd.h
    headers/quentier/synchronization/types/IAuthenticationInfo.h
    headers/quentier/synchronization/types/IAuthenticationInfoBuilder.h
    headers/quentier/synchronization/types/IDownloadNotesStatus.h
    headers/quentier/synchronization/types/IDownloadResourcesStatus.h
    headers/quentier/synchronization/types/ISendStatus.h
    headers/quentier/synchronization/types/ISyncChunksDataCounters.h
    headers/quentier/synchronization/types/ISyncOptions.h
    headers/quentier/synchronization/types/ISyncOptionsBuilder.h
    headers/quentier/synchronization/types/ISyncResult.h
    headers/quentier/synchronization/types/ISyncState.h
    headers/quentier/synchronization/types/ISyncStateBuilder.h)

set(SYNCHRONIZATION_TYPES_SERIALIZATION_JSON_HEADERS
    headers/quentier/synchronization/types/serialization/json/AuthenticationInfo.h
    headers/quentier/synchronization/types/serialization/json/DownloadNotesStatus.h
    headers/quentier/synchronization/types/serialization/json/DownloadResourcesStatus.h
    headers/quentier/synchronization/types/serialization/json/SendStatus.h
    headers/quentier/synchronization/types/serialization/json/SyncChunksDataCounters.h
    headers/quentier/synchronization/types/serialization/json/SyncResult.h
    headers/quentier/synchronization/types/serialization/json/SyncState.h)

set(THREADING_HEADERS
    headers/quentier/threading/Factory.h
    headers/quentier/threading/Future.h
    headers/quentier/threading/Fwd.h
    headers/quentier/threading/Post.h
    headers/quentier/threading/QtFutureContinuations.h
    headers/quentier/threading/QtFutureHelpers.h
    headers/quentier/threading/Runnable.h
    headers/quentier/threading/TrackedTask.h)

if(NOT BUILD_WITH_QT6)
  list(APPEND THREADING_HEADERS
    headers/quentier/threading/Qt5Promise.h)
endif()

set(LOGGING_HEADERS
    headers/quentier/logging/QuentierLogger.h)

set(EXCEPTION_HEADERS
    headers/quentier/exception/InvalidArgument.h
    headers/quentier/exception/IQuentierException.h
    headers/quentier/exception/OperationCanceled.h
    headers/quentier/exception/RuntimeError.h)

set(UTILITY_HEADERS
    headers/quentier/utility/ApplicationSettings.h
    headers/quentier/utility/Checks.h
    headers/quentier/utility/Compat.h
    headers/quentier/utility/DateTime.h
    headers/quentier/utility/EventLoopWithExitStatus.h
    headers/quentier/utility/Factory.h
    headers/quentier/utility/FileCopier.h
    headers/quentier/utility/FileIOProcessorAsync.h
    headers/quentier/utility/FileSystem.h
    headers/quentier/utility/FileSystemWatcher.h
    headers/quentier/utility/Fwd.h
    headers/quentier/utility/IEncryptor.h
    headers/quentier/utility/IKeychainService.h
    headers/quentier/utility/Initialize.h
    headers/quentier/utility/LRUCache.hpp
    headers/quentier/utility/MessageBox.h
    headers/quentier/utility/PlatformUtils.h
    headers/quentier/utility/Printable.h
    headers/quentier/utility/QuentierApplication.h
    headers/quentier/utility/QuentierUndoCommand.h
    headers/quentier/utility/ShortcutManager.h
    headers/quentier/utility/Size.h
    headers/quentier/utility/StandardPaths.h
    headers/quentier/utility/StringUtils.h
    headers/quentier/utility/SuppressWarnings.h
    headers/quentier/utility/SysInfo.h
    headers/quentier/utility/System.h
    headers/quentier/utility/TagSortByParentChildRelations.h
    headers/quentier/utility/UidGenerator.h
    headers/quentier/utility/Unreachable.h)

set(UTILITY_CANCELER_HEADERS
    headers/quentier/utility/cancelers/AnyOfCanceler.h
    headers/quentier/utility/cancelers/Fwd.h
    headers/quentier/utility/cancelers/FutureCanceler.h
    headers/quentier/utility/cancelers/ICanceler.h
    headers/quentier/utility/cancelers/ManualCanceler.h)

set(PUBLIC_HEADERS ${ENML_HEADERS})
list(APPEND PUBLIC_HEADERS ${ENML_CONVERSION_RULES_HEADERS})
list(APPEND PUBLIC_HEADERS ${EXCEPTION_HEADERS})
list(APPEND PUBLIC_HEADERS ${LOCAL_STORAGE_HEADERS})
list(APPEND PUBLIC_HEADERS ${LOGGING_HEADERS})
list(APPEND PUBLIC_HEADERS ${NOTE_EDITOR_HEADERS})
list(APPEND PUBLIC_HEADERS ${SYNCHRONIZATION_HEADERS})
list(APPEND PUBLIC_HEADERS ${SYNCHRONIZATION_TYPES_HEADERS})
list(APPEND PUBLIC_HEADERS ${SYNCHRONIZATION_TYPES_SERIALIZATION_JSON_HEADERS})
list(APPEND PUBLIC_HEADERS ${THREADING_HEADERS})
list(APPEND PUBLIC_HEADERS ${TYPES_HEADERS})
list(APPEND PUBLIC_HEADERS ${UTILITY_HEADERS})
list(APPEND PUBLIC_HEADERS ${UTILITY_CANCELER_HEADERS})

set(PRIVATE_HEADERS
    src/local_storage/NoteSearchQueryData.h
    src/logging/QuentierLogger_p.h
    src/types/data/AccountData.h
    src/types/data/ErrorStringData.h
    src/types/data/ResourceRecognitionIndexItemData.h
    src/types/data/ResourceRecognitionIndicesData.h
    src/enml/HtmlUtils.cpp
    src/enml/conversion_rules/Factory.cpp
    src/enml/conversion_rules/ISkipRule.cpp
    src/enml/conversion_rules/ISkipRuleBuilder.cpp
    src/enml/conversion_rules/MatchMode.cpp
    src/synchronization/INoteStoreFactory.cpp
    src/synchronization/ISyncEventsNotifier.cpp
    src/synchronization/IUserStoreFactory.cpp
    src/synchronization/SyncStateStorage.h
    src/synchronization/types/AuthenticationInfo.h
    src/synchronization/types/SyncChunksDataCounters.h
    src/synchronization/types/SyncOptions.h
    src/synchronization/types/SyncOptionsBuilder.h
    src/synchronization/types/SyncStateBuilder.h
    src/utility/tag_topological_sort/TagDirectedGraph.h
    src/utility/tag_topological_sort/TagDirectedGraphDepthFirstSearch.h
    src/utility/Encryptor.h
    src/utility/FileCopier_p.h
    src/utility/FileIOProcessorAsync_p.h
    src/utility/FileSystemWatcher_p.h
    src/utility/ShortcutManager_p.h
    src/utility/SysInfo_p.h
    src/utility/StringUtils_p.h)

if(BUILD_WITH_NOTE_EDITOR)
  list(APPEND PRIVATE_HEADERS
       src/note_editor/GenericResourceImageManager.h
       src/note_editor/NoteEditorSettingsNames.h
       src/note_editor/NoteEditorPage.h
       src/note_editor/NoteEditor_p.h
       src/note_editor/NoteEditorPrivateMacros.h
       src/note_editor/NoteEditorLocalStorageBroker.h
       src/note_editor/JavaScriptInOrderExecutor.h
       src/note_editor/ResourceDataInTemporaryFileStorageManager.h
       src/note_editor/ResourceInfo.h
       src/note_editor/SpellChecker_p.h
       src/note_editor/WebSocketClientWrapper.h
       src/note_editor/WebSocketTransport.h
       src/note_editor/dialogs/EncryptionDialog.h
       src/note_editor/dialogs/DecryptionDialog.h
       src/note_editor/dialogs/EditHyperlinkDialog.h
       src/note_editor/dialogs/RenameResourceDialog.h
       src/note_editor/delegates/JsResultCallbackFunctor.hpp
       src/note_editor/delegates/InsertHtmlDelegate.h
       src/note_editor/delegates/ImageResourceRotationDelegate.h
       src/note_editor/delegates/AddResourceDelegate.h
       src/note_editor/delegates/RemoveResourceDelegate.h
       src/note_editor/delegates/EncryptSelectedTextDelegate.h
       src/note_editor/delegates/DecryptEncryptedTextDelegate.h
       src/note_editor/delegates/AddHyperlinkToSelectedTextDelegate.h
       src/note_editor/delegates/EditHyperlinkDelegate.h
       src/note_editor/delegates/RemoveHyperlinkDelegate.h
       src/note_editor/delegates/RenameResourceDelegate.h
       src/note_editor/javascript_glue/ActionsWatcher.h
       src/note_editor/javascript_glue/EnCryptElementOnClickHandler.h
       src/note_editor/javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.h
       src/note_editor/javascript_glue/GenericResourceImageJavaScriptHandler.h
       src/note_editor/javascript_glue/HyperlinkClickJavaScriptHandler.h
       src/note_editor/javascript_glue/ResourceInfoJavaScriptHandler.h
       src/note_editor/javascript_glue/ResizableImageJavaScriptHandler.h
       src/note_editor/javascript_glue/TextCursorPositionJavaScriptHandler.h
       src/note_editor/javascript_glue/ContextMenuEventJavaScriptHandler.h
       src/note_editor/javascript_glue/PageMutationHandler.h
       src/note_editor/javascript_glue/SpellCheckerDynamicHelper.h
       src/note_editor/javascript_glue/ToDoCheckboxAutomaticInsertionHandler.h
       src/note_editor/javascript_glue/ToDoCheckboxOnClickHandler.h
       src/note_editor/javascript_glue/TableResizeJavaScriptHandler.h
       src/note_editor/javascript_glue/WebSocketWaiter.h
       src/note_editor/undo_stack/INoteEditorUndoCommand.h
       src/note_editor/undo_stack/AddResourceUndoCommand.h
       src/note_editor/undo_stack/RemoveResourceUndoCommand.h
       src/note_editor/undo_stack/UpdateResourceUndoCommand.h
       src/note_editor/undo_stack/RenameResourceUndoCommand.h
       src/note_editor/undo_stack/InsertHtmlUndoCommand.h
       src/note_editor/undo_stack/NoteEditorContentEditUndoCommand.h
       src/note_editor/undo_stack/EncryptDecryptUndoCommandInfo.h
       src/note_editor/undo_stack/EncryptUndoCommand.h
       src/note_editor/undo_stack/DecryptUndoCommand.h
       src/note_editor/undo_stack/AddHyperlinkUndoCommand.h
       src/note_editor/undo_stack/SourceCodeFormatUndoCommand.h
       src/note_editor/undo_stack/RemoveHyperlinkUndoCommand.h
       src/note_editor/undo_stack/EditHyperlinkUndoCommand.h
       src/note_editor/undo_stack/ToDoCheckboxUndoCommand.h
       src/note_editor/undo_stack/ToDoCheckboxAutomaticInsertionUndoCommand.h
       src/note_editor/undo_stack/ImageResourceRotationUndoCommand.h
       src/note_editor/undo_stack/ImageResizeUndoCommand.h
       src/note_editor/undo_stack/ReplaceUndoCommand.h
       src/note_editor/undo_stack/ReplaceAllUndoCommand.h
       src/note_editor/undo_stack/SpellCorrectionUndoCommand.h
       src/note_editor/undo_stack/SpellCheckIgnoreWordUndoCommand.h
       src/note_editor/undo_stack/SpellCheckAddToUserWordListUndoCommand.h
       src/note_editor/undo_stack/TableActionUndoCommand.h
       src/note_editor/undo_stack/HideDecryptedTextUndoCommand.h)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  list(APPEND PRIVATE_HEADERS
    src/utility/unix/linux/StackTrace.h)
  set_source_files_properties(
    src/utility/unix/linux/StackTrace.h
    PROPERTIES skip_autoformat ON)
endif()

set_source_files_properties(
  headers/quentier/utility/VersionInfo.h.in
  PROPERTIES skip_autoformat ON)

set(${PROJECT_NAME}_HEADERS ${PUBLIC_HEADERS} ${PRIVATE_HEADERS})

set(${PROJECT_NAME}_SOURCES
    src/enml/Factory.cpp
    src/enml/IConverter.cpp
    src/enml/IDecryptedTextCache.cpp
    src/enml/IENMLTagsConverter.cpp
    src/enml/IHtmlData.cpp
    src/exception/InvalidArgument.cpp
    src/exception/IQuentierException.cpp
    src/exception/OperationCanceled.cpp
    src/exception/RuntimeError.cpp
    src/local_storage/Factory.cpp
    src/local_storage/ILocalStorage.cpp
    src/local_storage/ILocalStorageNotifier.cpp
    src/local_storage/IPatch.cpp
	  src/local_storage/LocalStorageOpenException.cpp
	  src/local_storage/LocalStorageOperationException.cpp
    src/local_storage/NoteSearchQuery.cpp
    src/local_storage/NoteSearchQueryData.cpp
    src/logging/QuentierLogger.cpp
    src/logging/QuentierLogger_p.cpp
    src/synchronization/Factory.cpp
    src/synchronization/IAuthenticator.cpp
    src/synchronization/ISyncConflictResolver.cpp
    src/synchronization/ISynchronizer.cpp
    src/synchronization/ISyncStateStorage.cpp
    src/synchronization/SyncStateStorage.cpp
    src/synchronization/types/Errors.cpp
    src/synchronization/types/IAuthenticationInfoBuilder.cpp
    src/synchronization/types/IDownloadResourcesStatus.cpp
    src/synchronization/types/ISyncOptions.cpp
    src/synchronization/types/ISyncOptionsBuilder.cpp
    src/synchronization/types/ISyncStateBuilder.cpp
    src/synchronization/types/SyncChunksDataCounters.cpp
    src/synchronization/types/SyncOptions.cpp
    src/synchronization/types/SyncOptionsBuilder.cpp
    src/synchronization/types/SyncStateBuilder.cpp
    src/synchronization/types/serialization/json/AuthenticationInfo.cpp
    src/synchronization/types/serialization/json/DownloadNotesStatus.cpp
    src/synchronization/types/serialization/json/DownloadResourcesStatus.cpp
    src/synchronization/types/serialization/json/SendStatus.cpp
    src/synchronization/types/serialization/json/SyncChunksDataCounters.cpp
    src/synchronization/types/serialization/json/SyncResult.cpp
    src/synchronization/types/serialization/json/SyncState.cpp
    src/threading/Factory.cpp
    src/threading/Future.cpp
    src/threading/Runnable.cpp
    src/types/Account.cpp
    src/types/ErrorString.cpp
    src/types/NoteUtils.cpp
    src/types/ResourceRecognitionIndexItem.cpp
    src/types/ResourceRecognitionIndices.cpp
    src/types/ResourceUtils.cpp
    src/types/Validation.cpp
    src/types/RegisterMetatypes.cpp
    src/types/data/AccountData.cpp
    src/types/data/ResourceRecognitionIndexItemData.cpp
    src/types/data/ResourceRecognitionIndicesData.cpp
    src/utility/cancelers/AnyOfCanceler.cpp
    src/utility/cancelers/ManualCanceler.cpp
    src/utility/keychain/IKeychainService.cpp
    src/utility/tag_topological_sort/TagDirectedGraph.cpp
    src/utility/tag_topological_sort/TagDirectedGraphDepthFirstSearch.cpp
    src/utility/ApplicationSettings.cpp
    src/utility/Checks.cpp
    src/utility/DateTime.cpp
    src/utility/Initialize.cpp
    src/utility/IEncryptor.cpp
    src/utility/MessageBox.cpp
    src/utility/Encryptor.cpp
    src/utility/EventLoopWithExitStatus.cpp
    src/utility/Factory.cpp
    src/utility/FileCopier.cpp
    src/utility/FileCopier_p.cpp
    src/utility/FileIOProcessorAsync.cpp
    src/utility/FileIOProcessorAsync_p.cpp
    src/utility/FileSystem.cpp
    src/utility/FileSystemWatcher.cpp
    src/utility/FileSystemWatcher_p.cpp
    src/utility/PlatformUtils.cpp
    src/utility/Printable.cpp
    src/utility/QuentierApplication.cpp
    src/utility/QuentierUndoCommand.cpp
    src/utility/ShortcutManager.cpp
    src/utility/ShortcutManager_p.cpp
    src/utility/Size.cpp
    src/utility/StandardPaths.cpp
    src/utility/StringUtils.cpp
    src/utility/StringUtils_p.cpp
    src/utility/SysInfo.cpp
    src/utility/System.cpp
    src/utility/TagSortByParentChildRelations.cpp
    src/utility/UidGenerator.cpp
    src/utility/VersionInfo.cpp)

if(BUILD_WITH_NOTE_EDITOR)
  list(APPEND ${PROJECT_NAME}_SOURCES
       src/note_editor/GenericResourceImageManager.cpp
       src/note_editor/NoteEditorPage.cpp
       src/note_editor/NoteEditor.cpp
       src/note_editor/INoteEditorBackend.cpp
       src/note_editor/NoteEditor_p.cpp
       src/note_editor/NoteEditorLocalStorageBroker.cpp
       src/note_editor/JavaScriptInOrderExecutor.cpp
       src/note_editor/ResourceDataInTemporaryFileStorageManager.cpp
       src/note_editor/ResourceInfo.cpp
       src/note_editor/SpellChecker.cpp
       src/note_editor/SpellChecker_p.cpp
       src/note_editor/WebSocketClientWrapper.cpp
       src/note_editor/WebSocketTransport.cpp
       src/note_editor/dialogs/EncryptionDialog.cpp
       src/note_editor/dialogs/DecryptionDialog.cpp
       src/note_editor/dialogs/EditHyperlinkDialog.cpp
       src/note_editor/dialogs/RenameResourceDialog.cpp
       src/note_editor/delegates/InsertHtmlDelegate.cpp
       src/note_editor/delegates/ImageResourceRotationDelegate.cpp
       src/note_editor/delegates/AddResourceDelegate.cpp
       src/note_editor/delegates/RemoveResourceDelegate.cpp
       src/note_editor/delegates/EncryptSelectedTextDelegate.cpp
       src/note_editor/delegates/DecryptEncryptedTextDelegate.cpp
       src/note_editor/delegates/AddHyperlinkToSelectedTextDelegate.cpp
       src/note_editor/delegates/EditHyperlinkDelegate.cpp
       src/note_editor/delegates/RemoveHyperlinkDelegate.cpp
       src/note_editor/delegates/RenameResourceDelegate.cpp
       src/note_editor/javascript_glue/ActionsWatcher.cpp
       src/note_editor/javascript_glue/EnCryptElementOnClickHandler.cpp
       src/note_editor/javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.cpp
       src/note_editor/javascript_glue/GenericResourceImageJavaScriptHandler.cpp
       src/note_editor/javascript_glue/HyperlinkClickJavaScriptHandler.cpp
       src/note_editor/javascript_glue/ResourceInfoJavaScriptHandler.cpp
       src/note_editor/javascript_glue/ResizableImageJavaScriptHandler.cpp
       src/note_editor/javascript_glue/TextCursorPositionJavaScriptHandler.cpp
       src/note_editor/javascript_glue/ContextMenuEventJavaScriptHandler.cpp
       src/note_editor/javascript_glue/PageMutationHandler.cpp
       src/note_editor/javascript_glue/SpellCheckerDynamicHelper.cpp
       src/note_editor/javascript_glue/ToDoCheckboxAutomaticInsertionHandler.cpp
       src/note_editor/javascript_glue/ToDoCheckboxOnClickHandler.cpp
       src/note_editor/javascript_glue/TableResizeJavaScriptHandler.cpp
       src/note_editor/javascript_glue/WebSocketWaiter.cpp
       src/note_editor/undo_stack/INoteEditorUndoCommand.cpp
       src/note_editor/undo_stack/AddResourceUndoCommand.cpp
       src/note_editor/undo_stack/RemoveResourceUndoCommand.cpp
       src/note_editor/undo_stack/RenameResourceUndoCommand.cpp
       src/note_editor/undo_stack/UpdateResourceUndoCommand.cpp
       src/note_editor/undo_stack/InsertHtmlUndoCommand.cpp
       src/note_editor/undo_stack/NoteEditorContentEditUndoCommand.cpp
       src/note_editor/undo_stack/EncryptUndoCommand.cpp
       src/note_editor/undo_stack/DecryptUndoCommand.cpp
       src/note_editor/undo_stack/AddHyperlinkUndoCommand.cpp
       src/note_editor/undo_stack/SourceCodeFormatUndoCommand.cpp
       src/note_editor/undo_stack/RemoveHyperlinkUndoCommand.cpp
       src/note_editor/undo_stack/EditHyperlinkUndoCommand.cpp
       src/note_editor/undo_stack/ToDoCheckboxUndoCommand.cpp
       src/note_editor/undo_stack/ToDoCheckboxAutomaticInsertionUndoCommand.cpp
       src/note_editor/undo_stack/ImageResourceRotationUndoCommand.cpp
       src/note_editor/undo_stack/ImageResizeUndoCommand.cpp
       src/note_editor/undo_stack/ReplaceUndoCommand.cpp
       src/note_editor/undo_stack/ReplaceAllUndoCommand.cpp
       src/note_editor/undo_stack/SpellCorrectionUndoCommand.cpp
       src/note_editor/undo_stack/SpellCheckIgnoreWordUndoCommand.cpp
       src/note_editor/undo_stack/SpellCheckAddToUserWordListUndoCommand.cpp
       src/note_editor/undo_stack/TableActionUndoCommand.cpp
       src/note_editor/undo_stack/HideDecryptedTextUndoCommand.cpp)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/windows/SysInfoWindows.cpp)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/unix/SysInfoUnix.cpp)
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/unix/linux/StackTrace.cpp)
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/unix/linux/SysInfoLinux.cpp)
  set_source_files_properties(
    src/utility/unix/linux/StackTrace.cpp
    PROPERTIES skip_autoformat ON)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/unix/SysInfoUnix.cpp)
  list(APPEND ${PROJECT_NAME}_SOURCES src/utility/unix/darwin/SysInfoDarwin.cpp)
endif()

# generate version info header
if(BUILD_WITH_NOTE_EDITOR)
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_DEFINE "#define LIB_QUENTIER_HAS_NOTE_EDITOR 1")
else()
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_DEFINE "#define LIB_QUENTIER_HAS_NOTE_EDITOR 0")
endif()

set(LIB_QUENTIER_VERSION_MAJOR_DEFINE "#define LIB_QUENTIER_VERSION_MAJOR ${PROJECT_VERSION_MAJOR}")
set(LIB_QUENTIER_VERSION_MINOR_DEFINE "#define LIB_QUENTIER_VERSION_MINOR ${PROJECT_VERSION_MINOR}")
set(LIB_QUENTIER_VERSION_PATCH_DEFINE "#define LIB_QUENTIER_VERSION_PATCH ${PROJECT_VERSION_PATCH}")

if (NOT LIB_QUENTIER_BUILD_INFO)
  find_package(Git)
  if(GIT_FOUND)
    message(STATUS "Git found: ${GIT_EXECUTABLE}")

    # Get git branch
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE LIBQUENTIER_GIT_BRANCH
      RESULT_VARIABLE LIBQUENTIER_GIT_BRANCH_RETURN_CODE
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${LIBQUENTIER_GIT_BRANCH_RETURN_CODE}" STREQUAL "0")
      message(AUTHOR_WARNING "Failed to determine the current git branch, return code ${LIBQUENTIER_GIT_BRANCH_RETURN_CODE}")
      set(LIBQUENTIER_GIT_BRANCH "unknown branch")
    else()
      if(${LIBQUENTIER_GIT_BRANCH} STREQUAL "HEAD")
        # Can happen if running on detached HEAD, can happen in CI jobs; workaround: try to get the current branch from environment variables
        set(APPVEYOR_REPO_BRANCH "$ENV{APPVEYOR_REPO_BRANCH}")
        set(TRAVIS_BRANCH "$ENV{TRAVIS_BRANCH}")
        if(NOT "${APPVEYOR_REPO_BRANCH}" STREQUAL "")
          set(LIBQUENTIER_GIT_BRANCH "${APPVEYOR_REPO_BRANCH}")
        elseif(NOT "${TRAVIS_BRANCH}" STREQUAL "")
          set(LIBQUENTIER_GIT_BRANCH "${TRAVIS_BRANCH}")
        endif()
      endif()
      message(STATUS "Git branch: ${LIBQUENTIER_GIT_BRANCH}")
    endif()

    # Get last commit short hash
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE LIBQUENTIER_GIT_REVISION
      RESULT_VARIABLE LIBQUENTIER_GIT_REVISION_RETURN_CODE
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${LIBQUENTIER_GIT_REVISION_RETURN_CODE}" STREQUAL "0")
      message(AUTHOR_WARNING "Failed to determine the current git revision")
      set(LIBQUENTIER_GIT_REVISION "unknown revision")
    else()
      message(STATUS "Last commit short hash: ${LIBQUENTIER_GIT_REVISION}")
    endif()

    # Check for uncommitted changes
    execute_process(COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      RESULT_VARIABLE LIBQUENTIER_GIT_DIRTY_STATE)
    if(NOT "${LIBQUENTIER_GIT_DIRTY_STATE}" STREQUAL "0")
      set(LIBQUENTIER_GIT_REVISION "${LIBQUENTIER_GIT_REVISION}, with uncommitted changes")
    endif()

    set(LIB_QUENTIER_BUILD_INFO "#define LIB_QUENTIER_BUILD_INFO \"${LIBQUENTIER_GIT_BRANCH}, ${LIBQUENTIER_GIT_REVISION}\"")
  else()
    set(LIB_QUENTIER_BUILD_INFO "#define LIB_QUENTIER_BUILD_INFO \"unknown\"")
  endif()
else()
  set(LIB_QUENTIER_BUILD_INFO "#define LIB_QUENTIER_BUILD_INFO \"${LIB_QUENTIER_BUILD_INFO}\"")
endif()

configure_file(headers/quentier/utility/VersionInfo.h.in
               ${PROJECT_BINARY_DIR}/quentier/utility/VersionInfo.h @ONLY)

list(APPEND UTILITY_HEADERS ${PROJECT_BINARY_DIR}/quentier/utility/VersionInfo.h)
list(APPEND PUBLIC_HEADERS ${PROJECT_BINARY_DIR}/quentier/utility/VersionInfo.h)

set(RESOURCES
    src/enml/enml.qrc)

if(BUILD_WITH_NOTE_EDITOR)
  list(APPEND RESOURCES
       src/note_editor/images/underline.qrc
       src/note_editor/css/css.qrc
       src/note_editor/images/icons/checkbox_icons/checkbox_icons.qrc
       src/note_editor/images/icons/encrypted_area_icons/encrypted_area_icons.qrc
       src/note_editor/images/icons/generic_resource_icons/generic_resource_icons.qrc
       src/note_editor/javascript/jquery/jquery.qrc
       src/note_editor/javascript/colResizable/colResizable.qrc
       src/note_editor/javascript/debounce/debounce.qrc
       src/note_editor/javascript/scripts/scripts.qrc
       src/note_editor/javascript/hilitor/hilitor.qrc
       src/note_editor/javascript/rangy/rangy.qrc)
endif()

if(BUILD_WITH_QT6)
  qt6_add_resources(${PROJECT_NAME}_RESOURCES_RCC ${RESOURCES})
else()
  qt5_add_resources(${PROJECT_NAME}_RESOURCES_RCC ${RESOURCES})
endif()

if(BUILD_WITH_NOTE_EDITOR)
  set(FORMS
      src/note_editor/dialogs/DecryptionDialog.ui
      src/note_editor/dialogs/EncryptionDialog.ui
      src/note_editor/dialogs/EditHyperlinkDialog.ui
      src/note_editor/dialogs/RenameResourceDialog.ui)

  if(BUILD_WITH_QT6)
    qt_wrap_ui(${PROJECT_NAME}_FORMS_HEADERS ${FORMS})
  else()
    qt5_wrap_ui(${PROJECT_NAME}_FORMS_HEADERS ${FORMS})
  endif()
endif()

set(${PROJECT_NAME}_TR_SOURCES
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_SOURCES})

set(${PROJECT_NAME}_TR_FILES
    translations/libquentier_ru_RU.ts)

set(${PROJECT_NAME}_QM_FILES "")
update_translation("${${PROJECT_NAME}_TR_SOURCES}" "${FORMS}" "${${PROJECT_NAME}_TR_FILES}")

set(LIBNAME "libqt${LIBQUENTIER_QT_VERSION}quentier")

add_library(${LIBNAME} SHARED
            ${${PROJECT_NAME}_HEADERS}
            ${${PROJECT_NAME}_SOURCES}
            ${${PROJECT_NAME}_RESOURCES_RCC}
            ${${PROJECT_NAME}_FORMS_HEADERS})

target_include_directories(${LIBNAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>)

# Interface library exposing project's public interface features to internal
# project parts
add_library(${LIBNAME}_interface INTERFACE)

target_include_directories(${LIBNAME}_interface
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(${LIBNAME}_interface
  INTERFACE
  QEverCloud::qt${LIBQUENTIER_QT_VERSION}qevercloud)

# Interface library exposing private project's features (not included in the API
# of the shared library) to internal project parts
add_library(${LIBNAME}_private INTERFACE)

target_include_directories(${LIBNAME}_private
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

# Project parts using private features automatically use public features as well
target_link_libraries(${LIBNAME}_private
  INTERFACE
  ${LIBNAME}_interface)

include(GenerateExportHeader)

generate_export_header(
  ${LIBNAME}
  BASE_NAME ${LIBNAME}
  EXPORT_MACRO_NAME QUENTIER_EXPORT
  EXPORT_FILE_NAME quentier/utility/Linkage.h)

# installation settings, needed for target properties
if(NOT CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()

if(NOT CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR "bin")
endif()

if(NOT CMAKE_INSTALL_INCLUDEDIR)
  set(CMAKE_INSTALL_INCLUDEDIR "include")
endif()

if(NOT CMAKE_INSTALL_TRANSLATIONSDIR)
  set(CMAKE_INSTALL_TRANSLATIONSDIR "share/${PROJECT_NAME}/translations")
endif()

if(NOT INSTALL_CMAKE_DIR)
  set(INSTALL_CMAKE_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/Libquentier-qt${LIBQUENTIER_QT_VERSION}"
    CACHE PATH "Installation directory for CMake files")
endif()

enable_testing()
find_package(GMock)
find_package(GTest)

add_subdirectory(src/enml)
add_subdirectory(src/local_storage/sql)
add_subdirectory(src/synchronization)
add_subdirectory(src/threading)
add_subdirectory(src/utility)
add_subdirectory(tests)

target_link_libraries(${LIBNAME}
  PUBLIC
  QEverCloud::qt${LIBQUENTIER_QT_VERSION}qevercloud
  Qt${LIBQUENTIER_QT_VERSION}::Core
  Qt${LIBQUENTIER_QT_VERSION}::Gui
  Qt${LIBQUENTIER_QT_VERSION}::Widgets
  Qt${LIBQUENTIER_QT_VERSION}::PrintSupport
  Qt${LIBQUENTIER_QT_VERSION}Keychain::Qt${LIBQUENTIER_QT_VERSION}Keychain
  Qt${LIBQUENTIER_QT_VERSION}::Network
  Qt${LIBQUENTIER_QT_VERSION}::Xml
  Qt${LIBQUENTIER_QT_VERSION}::Sql
  TidyHtml5::TidyHtml5
  OpenSSL::SSL
  OpenSSL::Crypto
  Hunspell::Libhunspell
  PRIVATE
  Boost::boost
  ${LIBNAME}_enml
  ${LIBNAME}_local_storage_sql
  ${LIBNAME}_synchronization
  ${LIBNAME}_threading
  ${LIBNAME}_utility)

if(BUILD_WITH_NOTE_EDITOR)
  target_link_libraries(${LIBNAME}
    PUBLIC
    Qt${LIBQUENTIER_QT_VERSION}::WebEngineCore
    Qt${LIBQUENTIER_QT_VERSION}::WebEngineWidgets
    PRIVATE
    Qt${LIBQUENTIER_QT_VERSION}::WebChannel
    Qt${LIBQUENTIER_QT_VERSION}::WebSockets)
endif()

if(MSVC)
  target_link_libraries(${LIBNAME}
    PRIVATE
    secur32.lib)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  target_link_libraries(${LIBNAME}
    PRIVATE
    "-ldl")
endif()

add_sanitizers(${LIBNAME})

# library target properties
set_target_properties(${LIBNAME} PROPERTIES
  PREFIX ""
  VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
  SOVERSION "${PROJECT_VERSION_MAJOR}"
  CXX_STANDARD 17
  CXX_EXTENSIONS OFF
  MACOSX_RPATH 1
  COMPILE_DEFINITIONS "${LIBNAME}_EXPORTS")

target_compile_definitions(${LIBNAME}
  PRIVATE
  "-DQT_NO_CAST_FROM_ASCII"
  "-DQT_NO_CAST_TO_ASCII"
  "-DQT_NO_CAST_FROM_BYTEARRAY"
  "-DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT"
)

set(TEST_HEADERS
    src/tests/types/ResourceRecognitionIndicesParsingTest.h
    src/tests/types/TypesTester.h
    src/tests/utility/LRUCacheTests.h
    src/tests/utility/TagSortByParentChildRelationsTest.h
    src/tests/utility/UtilityTester.h
    src/tests/TestMacros.h)

set(TEST_SOURCES
    src/tests/types/ResourceRecognitionIndicesParsingTest.cpp
    src/tests/types/TypesTester.cpp
    src/tests/utility/LRUCacheTests.cpp
    src/tests/utility/TagSortByParentChildRelationsTest.cpp
    src/tests/utility/UtilityTester.cpp
    src/tests/TestMain.cpp)

set(TEST_RESOURCES
    src/tests/resources/test_resources.qrc)

if(BUILD_WITH_QT6)
  qt6_add_resources(${PROJECT_NAME}_TEST_RESOURCES_RCC ${TEST_RESOURCES})
else()
  qt5_add_resources(${PROJECT_NAME}_TEST_RESOURCES_RCC ${TEST_RESOURCES})
endif()

add_executable(test_${PROJECT_NAME} ${TEST_HEADERS} ${TEST_SOURCES} ${${PROJECT_NAME}_TEST_RESOURCES_RCC})

# test executable target properties
set_target_properties(test_${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_EXTENSIONS OFF)

target_compile_definitions(test_${PROJECT_NAME}
  PRIVATE
  "-DQT_NO_CAST_FROM_ASCII"
  "-DQT_NO_CAST_TO_ASCII"
  "-DQT_NO_CAST_FROM_BYTEARRAY"
  "-DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT"
)

add_sanitizers(test_${PROJECT_NAME})
add_test(NAME test_${PROJECT_NAME} COMMAND test_${PROJECT_NAME})

target_link_libraries(test_${PROJECT_NAME}
  PRIVATE
    ${LIBNAME}
    Qt${LIBQUENTIER_QT_VERSION}::Test)

include(SetupClangFormat)
include(SetupClangTidy)

# set Doxygen documentation properties
set(DOXY_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/headers ${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(DOXY_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(DOXY_RECURSIVE "YES")
set(DOXY_PROJECT_NAME ${PROJECT_NAME})
set(DOXY_PROJECT_NUMBER "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(DOXY_PROJECT_BRIEF "\"The library for rich desktop clients of Evernote service\"")
set(DOXY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doc")
ADD_DOCUMENTATION(doc Doxyfile)

if(BUILD_WITH_NOTE_EDITOR)
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_OPTION "set(LIBQUENTIER_HAS_NOTE_EDITOR TRUE)")
else()
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_OPTION "set(LIBQUENTIER_HAS_NOTE_EDITOR FALSE)")
endif()

if(BUILD_WITH_QT6)
  set(LIB_QUENTIER_USE_QT6 "set(LIBQUENTIER_USE_QT6 TRUE)")
else()
  set(LIB_QUENTIER_USE_QT6 "set(LIBQUENTIER_USE_QT6 FALSE)")
endif()

configure_file(cmake/modules/LibquentierConfig.cmake.in
               ${PROJECT_BINARY_DIR}/Libquentier-qt${LIBQUENTIER_QT_VERSION}Config.cmake @ONLY)
configure_file(cmake/modules/LibquentierConfigVersion.cmake.in
               ${PROJECT_BINARY_DIR}/Libquentier-qt${LIBQUENTIER_QT_VERSION}ConfigVersion.cmake @ONLY)

# install cmake files
file(COPY cmake/modules/LibquentierFindDependencies.cmake DESTINATION ${PROJECT_BINARY_DIR})
file(COPY cmake/modules/LibquentierFindTidyHtml5.cmake DESTINATION ${PROJECT_BINARY_DIR})
file(COPY cmake/modules/LibquentierFindHunspell.cmake DESTINATION ${PROJECT_BINARY_DIR})

# install shared library
install(TARGETS ${LIBNAME}
        EXPORT Libquentier-qt${LIBQUENTIER_QT_VERSION}LibraryDepends
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# install headers
if(BUILD_WITH_NOTE_EDITOR)
  foreach(ITEM ${NOTE_EDITOR_HEADERS})
    install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/note_editor)
  endforeach()
endif()

foreach(ITEM ${TYPES_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/types)
endforeach()

foreach(ITEM ${ENML_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/enml)
endforeach()

foreach(ITEM ${ENML_CONVERSION_RULES_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/enml/conversion_rules)
endforeach()

foreach(ITEM ${LOCAL_STORAGE_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/local_storage)
endforeach()

foreach(ITEM ${SYNCHRONIZATION_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/synchronization)
endforeach()

foreach(ITEM ${SYNCHRONIZATION_TYPES_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/synchronization/types)
endforeach()

foreach(ITEM ${SYNCHRONIZATION_TYPES_SERIALIZATION_JSON_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/synchronization/types/serialization/json)
endforeach()

foreach(ITEM ${LOGGING_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/logging)
endforeach()

foreach(ITEM ${EXCEPTION_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/exception)
endforeach()

foreach(ITEM ${UTILITY_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/utility)
endforeach()

foreach(ITEM ${UTILITY_CANCELER_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/utility/cancelers)
endforeach()

foreach(ITEM ${THREADING_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/threading)
endforeach()

install(FILES ${PROJECT_BINARY_DIR}/quentier/utility/Linkage.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/utility)

# install translations
foreach(QM_FILE ${${PROJECT_NAME}_QM_FILES})
  install(CODE "
          set(QM_FILE \"${QM_FILE})\")
          if(EXISTS \"${QM_FILE}\")
            get_filename_component(QM_FILE_BASE_NAME \"${QM_FILE}\" NAME)
            message(STATUS \"Installing: \$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_TRANSLATIONSDIR}/\${QM_FILE_BASE_NAME}\")
            file(COPY \"${QM_FILE}\" DESTINATION \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_TRANSLATIONSDIR}\")
          endif()
          " COMPONENT Runtime)
endforeach()

# install cmake files
install(EXPORT Libquentier-qt${LIBQUENTIER_QT_VERSION}LibraryDepends NAMESPACE Libquentier:: DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES cmake/modules/hunspell_new_api_check.cpp DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES cmake/modules/LibquentierFindDependencies.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES cmake/modules/LibquentierFindTidyHtml5.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES cmake/modules/LibquentierFindHunspell.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-qt${LIBQUENTIER_QT_VERSION}Config.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-qt${LIBQUENTIER_QT_VERSION}ConfigVersion.cmake DESTINATION ${INSTALL_CMAKE_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_custom_target(check COMMAND "${CMAKE_CTEST_COMMAND}" "--verbose")
