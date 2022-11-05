cmake_minimum_required(VERSION 3.5.1)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/sanitizers ${CMAKE_MODULE_PATH})

include(LibquentierCMakePolicies)
SET_POLICIES()

project(libquentier VERSION 0.5.0)

set(PROJECT_VENDOR "Dmitry Ivanov")
set(PROJECT_COPYRIGHT_YEAR "2020")
set(PROJECT_DOMAIN_FIRST "quentier")
set(PROJECT_DOMAIN_SECOND "org")
set(PROJECT_DOMAIN "${PROJECT_DOMAIN_FIRST}.${PROJECT_DOMAIN_SECOND}")

set(BUILD_WITH_AUTHENTICATION_MANAGER ON CACHE BOOL "If on, libquentier includes its own authentication manager, otherwise only the interface for it is included")
set(BUILD_WITH_NOTE_EDITOR ON CACHE BOOL "If on, libquentier includes note editor UI component")

include(LibquentierCompilerSettings)
include(LibquentierAdditionalCompilerWarnings)
include(LibquentierLinkerSettings)
include(LibquentierSetupQt)
include(LibquentierUpdateTranslations)
include(LibquentierFindLibXml2)
include(LibquentierFindOpenSSL)
include(LibquentierFindBoost)
include(LibquentierFindHunspell)
include(LibquentierFindQtKeychain)
include(LibquentierFindQEverCloud)
include(LibquentierFindTidyHtml5)
include(LibquentierSetupDoxygen)
include(PrependPath)

find_package(Sanitizers)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/headers
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(THIRDPARTY_LIBS ${QEVERCLOUD_LIBRARIES})
list(APPEND THIRDPARTY_LIBS ${TIDY_HTML5_LIBRARIES})
list(APPEND THIRDPARTY_LIBS ${QTKEYCHAIN_LIBRARIES})
list(APPEND THIRDPARTY_LIBS OpenSSL::SSL)
list(APPEND THIRDPARTY_LIBS OpenSSL::Crypto)
list(APPEND THIRDPARTY_LIBS Libxml2::Libxml2)
list(APPEND THIRDPARTY_LIBS Hunspell::Libhunspell)
if(LIBCPP)
  list(APPEND THIRDPARTY_LIBS ${LIBCPP})
endif()
if(MSVC)
  list(APPEND THIRDPARTY_LIBS secur32.lib)
endif()
if(MINGW)
  list(APPEND THIRDPARTY_LIBS secur32)
endif()
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  list(APPEND THIRDPARTY_LIBS "-ldl")
endif()

set(THIRDPARTY_LIB_DIRS ${QEVERCLOUD_LIB_DIR})
list(APPEND THIRDPARTY_LIB_DIRS ${TIDY_HTML5_LIB_DIR})
list(APPEND THIRDPARTY_LIB_DIRS ${QTKEYCHAIN_LIB_DIR})
list(APPEND THIRDPARTY_LIB_DIRS ${OPENSSL_LIB_DIRS})
list(APPEND THIRDPARTY_LIB_DIRS ${LIBXML2_LIB_DIR})
list(APPEND THIRDPARTY_LIB_DIRS ${HUNSPELL_LIB_DIR})
if(LIBCPP)
  get_filename_component(LIBCPP_LIB_DIR "${LIBCPP}" PATH)
  list(APPEND THIRDPARTY_LIB_DIRS ${LIBCPP_LIB_DIR})
endif()

include_directories(headers)

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
    headers/quentier/types/IFavoritableDataElement.h
    headers/quentier/types/LinkedNotebook.h
    headers/quentier/types/ILocalStorageDataElement.h
    headers/quentier/types/Note.h
    headers/quentier/types/Notebook.h
    headers/quentier/types/INoteStoreDataElement.h
    headers/quentier/types/Resource.h
    headers/quentier/types/ResourceRecognitionIndexItem.h
    headers/quentier/types/ResourceRecognitionIndices.h
    headers/quentier/types/SavedSearch.h
    headers/quentier/types/SharedNote.h
    headers/quentier/types/SharedNotebook.h
    headers/quentier/types/Tag.h
    headers/quentier/types/User.h
    headers/quentier/types/RegisterMetatypes.h)

set(ENML_HEADERS
    headers/quentier/enml/DecryptedTextManager.h
    headers/quentier/enml/ENMLConverter.h
    headers/quentier/enml/HTMLCleaner.h)

set(LOCAL_STORAGE_HEADERS
    headers/quentier/local_storage/ILocalStorageCacheExpiryChecker.h
    headers/quentier/local_storage/ILocalStoragePatch.h
    headers/quentier/local_storage/DefaultLocalStorageCacheExpiryChecker.h
    headers/quentier/local_storage/Lists.h
    headers/quentier/local_storage/LocalStorageCacheManager.h
    headers/quentier/local_storage/LocalStorageManager.h
    headers/quentier/local_storage/LocalStorageManagerAsync.h
    headers/quentier/local_storage/NoteSearchQuery.h)

set(SYNCHRONIZATION_HEADERS
    headers/quentier/synchronization/ForwardDeclarations.h
    headers/quentier/synchronization/IAuthenticationManager.h
    headers/quentier/synchronization/INoteStore.h
    headers/quentier/synchronization/ISyncStateStorage.h
    headers/quentier/synchronization/IUserStore.h
    headers/quentier/synchronization/SynchronizationManager.h)

if(BUILD_WITH_AUTHENTICATION_MANAGER)
  list(APPEND SYNCHRONIZATION_HEADERS
    headers/quentier/synchronization/AuthenticationManager.h)
endif()

set(LOGGING_HEADERS
    headers/quentier/logging/QuentierLogger.h)

set(EXCEPTION_HEADERS
    headers/quentier/exception/ApplicationSettingsInitializationException.h
    headers/quentier/exception/DatabaseLockedException.h
    headers/quentier/exception/DatabaseLockFailedException.h
    headers/quentier/exception/DatabaseOpeningException.h
    headers/quentier/exception/DatabaseRequestException.h
    headers/quentier/exception/EmptyDataElementException.h
    headers/quentier/exception/IQuentierException.h
    headers/quentier/exception/LocalStorageCacheManagerException.h
    headers/quentier/exception/LoggerInitializationException.h
    headers/quentier/exception/NoteEditorInitializationException.h
    headers/quentier/exception/NoteEditorPluginInitializationException.h
    headers/quentier/exception/NullPtrException.h)

set(UTILITY_HEADERS
    headers/quentier/utility/ApplicationSettings.h
    headers/quentier/utility/Checks.h
    headers/quentier/utility/Compat.h
    headers/quentier/utility/DateTime.h
    headers/quentier/utility/EncryptionManager.h
    headers/quentier/utility/EventLoopWithExitStatus.h
    headers/quentier/utility/FileCopier.h
    headers/quentier/utility/FileIOProcessorAsync.h
    headers/quentier/utility/FileSystem.h
    headers/quentier/utility/FileSystemWatcher.h
    headers/quentier/utility/ForwardDeclarations.h
    headers/quentier/utility/IKeychainService.h
    headers/quentier/utility/Initialize.h
    headers/quentier/utility/LimitedStack.h
    headers/quentier/utility/Linkage.h
    headers/quentier/utility/LRUCache.hpp
    headers/quentier/utility/MessageBox.h
    headers/quentier/utility/Printable.h
    headers/quentier/utility/QuentierApplication.h
    headers/quentier/utility/QuentierCheckPtr.h
    headers/quentier/utility/QuentierUndoCommand.h
    headers/quentier/utility/ShortcutManager.h
    headers/quentier/utility/Size.h
    headers/quentier/utility/StandardPaths.h
    headers/quentier/utility/StringUtils.h
    headers/quentier/utility/SuppressWarnings.h
    headers/quentier/utility/SysInfo.h
    headers/quentier/utility/System.h
    headers/quentier/utility/TagSortByParentChildRelations.h
    headers/quentier/utility/UidGenerator.h)

set(PUBLIC_HEADERS ${NOTE_EDITOR_HEADERS})
list(APPEND PUBLIC_HEADERS ${TYPES_HEADERS})
list(APPEND PUBLIC_HEADERS ${ENML_HEADERS})
list(APPEND PUBLIC_HEADERS ${LOCAL_STORAGE_HEADERS})
list(APPEND PUBLIC_HEADERS ${SYNCHRONIZATION_HEADERS})
list(APPEND PUBLIC_HEADERS ${LOGGING_HEADERS})
list(APPEND PUBLIC_HEADERS ${EXCEPTION_HEADERS})
list(APPEND PUBLIC_HEADERS ${UTILITY_HEADERS})

set(PRIVATE_HEADERS
    src/local_storage/Transaction.h
    src/logging/QuentierLogger_p.h
    src/types/data/AccountData.h
    src/types/data/ErrorStringData.h
    src/types/data/FavoritableDataElementData.h
    src/types/data/LocalStorageDataElementData.h
    src/types/data/LinkedNotebookData.h
    src/types/data/NoteStoreDataElementData.h
    src/types/data/NoteData.h
    src/types/data/NotebookData.h
    src/types/data/ResourceData.h
    src/types/data/SharedNotebookData.h
    src/types/data/TagData.h
    src/types/data/SharedNoteData.h
    src/types/data/SavedSearchData.h
    src/types/data/ResourceRecognitionIndexItemData.h
    src/types/data/ResourceRecognitionIndicesData.h
    src/types/data/UserData.h
    src/enml/ENMLConverter_p.h
    src/enml/DecryptedTextManager_p.h
    src/local_storage/LocalStorageCacheManager_p.h
    src/local_storage/LocalStoragePatchManager.h
    src/local_storage/LocalStorageManager_p.h
    src/local_storage/LocalStorageShared.h
    src/local_storage/NoteSearchQueryData.h
    src/local_storage/patches/LocalStoragePatch1To2.h
    src/synchronization/ExceptionHandlingHelpers.h
    src/synchronization/InkNoteImageDownloader.h
    src/synchronization/NoteStore.h
    src/synchronization/UserStore.h
    src/synchronization/NoteThumbnailDownloader.h
    src/synchronization/RemoteToLocalSynchronizationManager.h
    src/synchronization/SendLocalChangesManager.h
    src/synchronization/SynchronizationShared.h
    src/synchronization/SynchronizationManager_p.h
    src/synchronization/SyncStateStorage.h
    src/synchronization/NotebookSyncConflictResolver.h
    src/synchronization/NotebookSyncCache.h
    src/synchronization/TagSyncConflictResolver.h
    src/synchronization/TagSyncCache.h
    src/synchronization/SavedSearchSyncConflictResolver.h
    src/synchronization/SavedSearchSyncCache.h
    src/synchronization/NoteSyncConflictResolver.h
    src/synchronization/NoteSyncCache.h
    src/synchronization/FullSyncStaleDataItemsExpunger.h
    src/utility/keychain/CompositeKeychainService.h
    src/utility/keychain/MigratingKeychainService.h
    src/utility/keychain/QtKeychainService.h
    src/utility/keychain/ObfuscatingKeychainService.h
    src/utility/keychain/QtKeychainWrapper.h
    src/utility/tag_topological_sort/TagDirectedGraph.h
    src/utility/tag_topological_sort/TagDirectedGraphDepthFirstSearch.h
    src/utility/EncryptionManager_p.h
    src/utility/FileCopier_p.h
    src/utility/FileIOProcessorAsync_p.h
    src/utility/FileSystemWatcher_p.h
    src/utility/ShortcutManager_p.h
    src/utility/SysInfo_p.h
    src/utility/StringUtils_p.h
    src/utility/TagSortByParentChildRelationsHelpers.hpp)

if(BUILD_WITH_AUTHENTICATION_MANAGER)
  list(APPEND PRIVATE_HEADERS
       src/synchronization/AuthenticationManager_p.h)
endif()

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
       src/note_editor/SpellCheckerDictionariesFinder.h
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
       src/note_editor/javascript_glue/ResourceInfoJavaScriptHandler.h
       src/note_editor/javascript_glue/ResizableImageJavaScriptHandler.h
       src/note_editor/javascript_glue/TextCursorPositionJavaScriptHandler.h
       src/note_editor/javascript_glue/ContextMenuEventJavaScriptHandler.h
       src/note_editor/javascript_glue/PageMutationHandler.h
       src/note_editor/javascript_glue/ToDoCheckboxAutomaticInsertionHandler.h
       src/note_editor/javascript_glue/ToDoCheckboxOnClickHandler.h
       src/note_editor/javascript_glue/TableResizeJavaScriptHandler.h
       src/note_editor/javascript_glue/SpellCheckerDynamicHelper.h
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

  if(NOT QUENTIER_USE_QT_WEB_ENGINE)
    list(APPEND PRIVATE_HEADERS
         src/note_editor/NoteEditorPluginFactory.h
         src/note_editor/INoteEditorResourcePlugin.h
         src/note_editor/EncryptedAreaPlugin.h
         src/note_editor/GenericResourceDisplayWidget.h)
  else()
    list(APPEND PRIVATE_HEADERS
         src/note_editor/javascript_glue/EnCryptElementOnClickHandler.h
         src/note_editor/javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.h
         src/note_editor/javascript_glue/GenericResourceImageJavaScriptHandler.h
         src/note_editor/javascript_glue/HyperlinkClickJavaScriptHandler.h
         src/note_editor/javascript_glue/WebSocketWaiter.h
         src/note_editor/WebSocketClientWrapper.h
         src/note_editor/WebSocketTransport.h)
  endif()
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  list(APPEND PRIVATE_HEADERS
    src/utility/unix/linux/StackTrace.h)
  set_source_files_properties(
    src/utility/unix/linux/StackTrace.h
    PROPERTIES skip_autoformat ON)
endif()

set(${PROJECT_NAME}_HEADERS ${PUBLIC_HEADERS} ${PRIVATE_HEADERS})

set(${PROJECT_NAME}_SOURCES
    src/logging/QuentierLogger.cpp
    src/logging/QuentierLogger_p.cpp
    src/types/Account.cpp
    src/types/ErrorString.cpp
    src/types/LinkedNotebook.cpp
    src/types/Note.cpp
    src/types/Notebook.cpp
    src/types/Resource.cpp
    src/types/ResourceRecognitionIndexItem.cpp
    src/types/ResourceRecognitionIndices.cpp
    src/types/SavedSearch.cpp
    src/types/SharedNote.cpp
    src/types/SharedNotebook.cpp
    src/types/Tag.cpp
    src/types/User.cpp
    src/types/RegisterMetatypes.cpp
    src/types/data/AccountData.cpp
    src/types/data/LinkedNotebookData.cpp
    src/types/data/NoteData.cpp
    src/types/data/NotebookData.cpp
    src/types/data/ResourceData.cpp
    src/types/data/ResourceRecognitionIndexItemData.cpp
    src/types/data/ResourceRecognitionIndicesData.cpp
    src/types/data/SharedNotebookData.cpp
    src/types/data/TagData.cpp
    src/types/data/SavedSearchData.cpp
    src/types/data/SharedNoteData.cpp
    src/types/data/UserData.cpp
    src/enml/ENMLConverter.cpp
    src/enml/ENMLConverter_p.cpp
    src/enml/HTMLCleaner.cpp
    src/enml/DecryptedTextManager.cpp
    src/enml/DecryptedTextManager_p.cpp
    src/local_storage/ILocalStorageCacheExpiryChecker.cpp
    src/local_storage/DefaultLocalStorageCacheExpiryChecker.cpp
    src/local_storage/LocalStorageManager.cpp
    src/local_storage/LocalStorageManager_p.cpp
    src/local_storage/LocalStorageCacheManager.cpp
    src/local_storage/LocalStorageCacheManager_p.cpp
    src/local_storage/LocalStoragePatchManager.cpp
    src/local_storage/LocalStorageManagerAsync.cpp
    src/local_storage/LocalStorageShared.cpp
    src/local_storage/NoteSearchQuery.cpp
    src/local_storage/NoteSearchQueryData.cpp
    src/local_storage/Transaction.cpp
    src/local_storage/patches/ILocalStoragePatch.cpp
    src/local_storage/patches/LocalStoragePatch1To2.cpp
    src/synchronization/IAuthenticationManager.cpp
    src/synchronization/InkNoteImageDownloader.cpp
    src/synchronization/INoteStore.cpp
    src/synchronization/ISyncStateStorage.cpp
    src/synchronization/IUserStore.cpp
    src/synchronization/NoteStore.cpp
    src/synchronization/UserStore.cpp
    src/synchronization/NoteThumbnailDownloader.cpp
    src/synchronization/SynchronizationShared.cpp
    src/synchronization/SynchronizationManager.cpp
    src/synchronization/SynchronizationManager_p.cpp
    src/synchronization/SyncStateStorage.cpp
    src/synchronization/RemoteToLocalSynchronizationManager.cpp
    src/synchronization/SendLocalChangesManager.cpp
    src/synchronization/NotebookSyncCache.cpp
    src/synchronization/TagSyncConflictResolver.cpp
    src/synchronization/TagSyncCache.cpp
    src/synchronization/SavedSearchSyncConflictResolver.cpp
    src/synchronization/SavedSearchSyncCache.cpp
    src/synchronization/NotebookSyncConflictResolver.cpp
    src/synchronization/NoteSyncConflictResolver.cpp
    src/synchronization/NoteSyncCache.cpp
    src/synchronization/FullSyncStaleDataItemsExpunger.cpp
    src/exception/ApplicationSettingsInitializationException.cpp
    src/exception/EmptyDataElementException.cpp
    src/exception/DatabaseLockedException.cpp
    src/exception/DatabaseLockFailedException.cpp
    src/exception/DatabaseOpeningException.cpp
    src/exception/DatabaseRequestException.cpp
    src/exception/IQuentierException.cpp
    src/exception/NoteEditorInitializationException.cpp
    src/exception/NoteEditorPluginInitializationException.cpp
    src/exception/NullPtrException.cpp
    src/exception/LocalStorageCacheManagerException.cpp
    src/exception/LoggerInitializationException.cpp
    src/utility/keychain/CompositeKeychainService.cpp
    src/utility/keychain/IKeychainService.cpp
    src/utility/keychain/MigratingKeychainService.cpp
    src/utility/keychain/QtKeychainService.cpp
    src/utility/keychain/ObfuscatingKeychainService.cpp
    src/utility/keychain/QtKeychainWrapper.cpp
    src/utility/tag_topological_sort/TagDirectedGraph.cpp
    src/utility/tag_topological_sort/TagDirectedGraphDepthFirstSearch.cpp
    src/utility/ApplicationSettings.cpp
    src/utility/Checks.cpp
    src/utility/DateTime.cpp
    src/utility/Initialize.cpp
    src/utility/MessageBox.cpp
    src/utility/EncryptionManager.cpp
    src/utility/EncryptionManager_p.cpp
    src/utility/EventLoopWithExitStatus.cpp
    src/utility/FileCopier.cpp
    src/utility/FileCopier_p.cpp
    src/utility/FileIOProcessorAsync.cpp
    src/utility/FileIOProcessorAsync_p.cpp
    src/utility/FileSystem.cpp
    src/utility/FileSystemWatcher.cpp
    src/utility/FileSystemWatcher_p.cpp
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

if(BUILD_WITH_AUTHENTICATION_MANAGER)
  list(APPEND ${PROJECT_NAME}_SOURCES
       src/synchronization/AuthenticationManager.cpp
       src/synchronization/AuthenticationManager_p.cpp)
endif()

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
       src/note_editor/SpellCheckerDictionariesFinder.cpp
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
       src/note_editor/javascript_glue/ResourceInfoJavaScriptHandler.cpp
       src/note_editor/javascript_glue/ResizableImageJavaScriptHandler.cpp
       src/note_editor/javascript_glue/TextCursorPositionJavaScriptHandler.cpp
       src/note_editor/javascript_glue/ContextMenuEventJavaScriptHandler.cpp
       src/note_editor/javascript_glue/PageMutationHandler.cpp
       src/note_editor/javascript_glue/ToDoCheckboxAutomaticInsertionHandler.cpp
       src/note_editor/javascript_glue/ToDoCheckboxOnClickHandler.cpp
       src/note_editor/javascript_glue/TableResizeJavaScriptHandler.cpp
       src/note_editor/javascript_glue/SpellCheckerDynamicHelper.cpp
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
  if(NOT QUENTIER_USE_QT_WEB_ENGINE)
    list(APPEND ${PROJECT_NAME}_SOURCES
         src/note_editor/EncryptedAreaPlugin.cpp
         src/note_editor/GenericResourceDisplayWidget.cpp
         src/note_editor/INoteEditorResourcePlugin.cpp
         src/note_editor/NoteEditorPluginFactory.cpp)
  else()
    list(APPEND ${PROJECT_NAME}_SOURCES
         src/note_editor/javascript_glue/EnCryptElementOnClickHandler.cpp
         src/note_editor/javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.cpp
         src/note_editor/javascript_glue/GenericResourceImageJavaScriptHandler.cpp
         src/note_editor/javascript_glue/HyperlinkClickJavaScriptHandler.cpp
         src/note_editor/javascript_glue/WebSocketWaiter.cpp
         src/note_editor/WebSocketTransport.cpp
         src/note_editor/WebSocketClientWrapper.cpp)
  endif()
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
if(QUENTIER_USE_QT_WEB_ENGINE)
  set(LIB_QUENTIER_QT_WEB_ENGINE_DEFINE "#define LIB_QUENTIER_USE_QT_WEB_ENGINE 1")
else()
  set(LIB_QUENTIER_QT_WEB_ENGINE_DEFINE "#define LIB_QUENTIER_USE_QT_WEB_ENGINE 0")
endif()

if(BUILD_WITH_AUTHENTICATION_MANAGER)
  set(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER_DEFINE "#define LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER 1")
else()
  set(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER_DEFINE "#define LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER 0")
endif()

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

qt_add_resources(${PROJECT_NAME}_RESOURCES_RCC ${RESOURCES})

if(BUILD_WITH_NOTE_EDITOR)
  set(FORMS
      src/note_editor/dialogs/DecryptionDialog.ui
      src/note_editor/dialogs/EncryptionDialog.ui
      src/note_editor/dialogs/EditHyperlinkDialog.ui
      src/note_editor/dialogs/RenameResourceDialog.ui)
  if(NOT QUENTIER_USE_QT_WEB_ENGINE)
    list(APPEND FORMS
         src/note_editor/EncryptedAreaPlugin.ui
         src/note_editor/GenericResourceDisplayWidget.ui)
  endif()
  qt_wrap_ui(${PROJECT_NAME}_FORMS_HEADERS ${FORMS})
endif()

set(${PROJECT_NAME}_TR_SOURCES
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_SOURCES})

set(${PROJECT_NAME}_TR_FILES
    translations/libquentier_ru_RU.ts)

set(${PROJECT_NAME}_QM_FILES "")
update_translation("${${PROJECT_NAME}_TR_SOURCES}" "${FORMS}" "${${PROJECT_NAME}_TR_FILES}")

set(LIBQUENTIER_QT_VERSION "qt5")

set(MAJOR_VERSION_LIB_NAME_SUFFIX OFF CACHE BOOL "If on, the project's major version would be added to the library's name as a suffix")

set(LIBQUENTIER_LIBNAME_SUFFIX "")
if(MAJOR_VERSION_LIB_NAME_SUFFIX)
  set(LIBQUENTIER_LIBNAME_SUFFIX "${PROJECT_VERSION_MAJOR}")
endif()

set(LIBNAME "lib${LIBQUENTIER_QT_VERSION}quentier${LIBQUENTIER_LIBNAME_SUFFIX}")

add_library(${LIBNAME} SHARED
            ${${PROJECT_NAME}_HEADERS}
            ${${PROJECT_NAME}_SOURCES}
            ${${PROJECT_NAME}_RESOURCES_RCC}
            ${${PROJECT_NAME}_FORMS_HEADERS})

add_sanitizers(${LIBNAME})

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
  if(WIN32 AND NOT CYGWIN)
    set(DEF_INSTALL_CMAKE_DIR "CMake")
  else()
    set(DEF_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Libquentier-${LIBQUENTIER_QT_VERSION}")
  endif()
  set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation directory for CMake files")
endif()

# library target properties
set_target_properties(${LIBNAME} PROPERTIES
  PREFIX ""
  VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
  SOVERSION "${PROJECT_VERSION_MAJOR}"
  CXX_STANDARD 14
  CXX_EXTENSIONS OFF
  MACOSX_RPATH 1
  COMPILE_DEFINITIONS "BUILDING_QUENTIER_DLL=1")

target_link_libraries(${LIBNAME} ${QT_LIBRARIES} ${THIRDPARTY_LIBS})

add_definitions("-DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII")
add_definitions("-DQT_NO_CAST_FROM_BYTEARRAY -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT")

set(TEST_HEADERS
    src/tests/enml/EnexExportImportTests.h
    src/tests/enml/ENMLConverterTests.h
    src/tests/enml/ENMLTester.h
    src/tests/local_storage/LocalStorageCacheAsyncTester.h
    src/tests/local_storage/LocalStorageManagerTester.h
    src/tests/local_storage/LocalStorageManagerAsyncTests.h
    src/tests/local_storage/LocalStorageManagerBasicTests.h
    src/tests/local_storage/LocalStorageManagerListTests.h
    src/tests/local_storage/LocalStorageManagerNoteSearchQueryTest.h
    src/tests/local_storage/LinkedNotebookLocalStorageManagerAsyncTester.h
    src/tests/local_storage/NotebookLocalStorageManagerAsyncTester.h
    src/tests/local_storage/NoteLocalStorageManagerAsyncTester.h
    src/tests/local_storage/NoteNotebookAndTagListTrackingAsyncTester.h
    src/tests/local_storage/NoteSearchQueryParsingTest.h
    src/tests/local_storage/ResourceLocalStorageManagerAsyncTester.h
    src/tests/local_storage/SavedSearchLocalStorageManagerAsyncTester.h
    src/tests/local_storage/TagLocalStorageManagerAsyncTester.h
    src/tests/local_storage/UserLocalStorageManagerAsyncTester.h
    src/tests/types/ResourceRecognitionIndicesParsingTest.h
    src/tests/types/TypesTester.h
    src/tests/synchronization/FakeAuthenticationManager.h
    src/tests/synchronization/FakeKeychainService.h
    src/tests/synchronization/FakeNoteStore.h
    src/tests/synchronization/FakeUserStore.h
    src/tests/synchronization/FullSyncStaleDataItemsExpungerTester.h
    src/tests/synchronization/SynchronizationManagerSignalsCatcher.h
    src/tests/synchronization/SynchronizationTester.h
    src/tests/utility/EncryptionManagerTests.h
    src/tests/utility/LRUCacheTests.h
    src/tests/utility/TagSortByParentChildRelationsTest.h
    src/tests/utility/UtilityTester.h
    src/tests/utility/keychain/CompositeKeychainTester.h
    src/tests/utility/keychain/KeychainServiceMock.h
    src/tests/utility/keychain/MigratingKeychainTester.h
    src/tests/utility/keychain/ObfuscatingKeychainTester.h
    src/tests/TestMacros.h
    src/synchronization/FullSyncStaleDataItemsExpunger.h
    src/synchronization/TagSyncCache.h
    src/synchronization/SavedSearchSyncCache.h
    src/synchronization/NoteSyncCache.h
    src/synchronization/NotebookSyncCache.h)

set(TEST_SOURCES
    src/tests/enml/EnexExportImportTests.cpp
    src/tests/enml/ENMLConverterTests.cpp
    src/tests/enml/ENMLTester.cpp
    src/tests/local_storage/LocalStorageCacheAsyncTester.cpp
    src/tests/local_storage/LocalStorageManagerTester.cpp
    src/tests/local_storage/LocalStorageManagerAsyncTests.cpp
    src/tests/local_storage/LocalStorageManagerBasicTests.cpp
    src/tests/local_storage/LocalStorageManagerListTests.cpp
    src/tests/local_storage/LocalStorageManagerNoteSearchQueryTest.cpp
    src/tests/local_storage/LinkedNotebookLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/NotebookLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/NoteLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/NoteNotebookAndTagListTrackingAsyncTester.cpp
    src/tests/local_storage/NoteSearchQueryParsingTest.cpp
    src/tests/local_storage/ResourceLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/SavedSearchLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/TagLocalStorageManagerAsyncTester.cpp
    src/tests/local_storage/UserLocalStorageManagerAsyncTester.cpp
    src/tests/types/ResourceRecognitionIndicesParsingTest.cpp
    src/tests/types/TypesTester.cpp
    src/tests/synchronization/FakeAuthenticationManager.cpp
    src/tests/synchronization/FakeKeychainService.cpp
    src/tests/synchronization/FakeNoteStore.cpp
    src/tests/synchronization/FakeUserStore.cpp
    src/tests/synchronization/FullSyncStaleDataItemsExpungerTester.cpp
    src/tests/synchronization/SynchronizationManagerSignalsCatcher.cpp
    src/tests/synchronization/SynchronizationTester.cpp
    src/tests/utility/EncryptionManagerTests.cpp
    src/tests/utility/LRUCacheTests.cpp
    src/tests/utility/TagSortByParentChildRelationsTest.cpp
    src/tests/utility/UtilityTester.cpp
    src/tests/utility/keychain/CompositeKeychainTester.cpp
    src/tests/utility/keychain/KeychainServiceMock.cpp
    src/tests/utility/keychain/MigratingKeychainTester.cpp
    src/tests/utility/keychain/ObfuscatingKeychainTester.cpp
    src/tests/TestMain.cpp
    src/synchronization/FullSyncStaleDataItemsExpunger.cpp
    src/synchronization/TagSyncCache.cpp
    src/synchronization/SavedSearchSyncCache.cpp
    src/synchronization/NoteSyncCache.cpp
    src/synchronization/NotebookSyncCache.cpp)

set(TEST_RESOURCES
    src/tests/resources/test_resources.qrc)

qt_add_resources(${PROJECT_NAME}_TEST_RESOURCES_RCC ${TEST_RESOURCES})

add_executable(test_${PROJECT_NAME} ${TEST_HEADERS} ${TEST_SOURCES} ${${PROJECT_NAME}_TEST_RESOURCES_RCC})

# test executable target properties
set_target_properties(test_${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 14
  CXX_EXTENSIONS OFF)

add_sanitizers(test_${PROJECT_NAME})
add_test(test_${PROJECT_NAME} test_${PROJECT_NAME})

target_link_libraries(test_${PROJECT_NAME} ${LIBNAME} ${QT_LIBRARIES} ${THIRDPARTY_LIBS})

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

# modifying sources list with absolute paths for the static analyzer
prepend_path(${PROJECT_NAME}_SOURCES "${${PROJECT_NAME}_SOURCES}" ${CMAKE_CURRENT_SOURCE_DIR})
prepend_path(TEST_SOURCES "${TEST_SOURCES}" ${CMAKE_CURRENT_SOURCE_DIR})

# collect the list of sources to be checked by the static analyzer
set(LIBQUENTIER_CPPCHECKABLE_SOURCES ${${PROJECT_NAME}_SOURCES})
list(APPEND LIBQUENTIER_CPPCHECKABLE_SOURCES ${TEST_SOURCES})

if(QUENTIER_USE_QT_WEB_ENGINE)
  set(LIB_QUENTIER_USE_QT_WEB_ENGINE_OPTION "set(LIBQUENTIER_USE_QT_WEB_ENGINE TRUE)")
else()
  set(LIB_QUENTIER_USE_QT_WEB_ENGINE_OPTION "set(LIBQUENTIER_USE_QT_WEB_ENGINE FALSE)")
endif()

if(BUILD_WITH_AUTHENTICATION_MANAGER)
  set(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER_OPTION "set(LIBQUENTIER_HAS_AUTHENTICATION_MANAGER TRUE)")
else()
  set(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER_OPTION "set(LIBQUENTIER_HAS_AUTHENTICATION_MANAGER FALSE)")
endif()

if(BUILD_WITH_NOTE_EDITOR)
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_OPTION "set(LIB_QUENTIER_HAS_NOTE_EDITOR TRUE)")
else()
  set(LIB_QUENTIER_HAS_NOTE_EDITOR_OPTION "set(LIB_QUENTIER_HAS_NOTE_EDITOR FALSE)")
endif()

configure_file(cmake/modules/LibquentierBuildTreeSettings.cmake.in
               ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}BuildTreeSettings.cmake @ONLY)
configure_file(cmake/modules/LibquentierConfig.cmake.in
               ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}Config.cmake @ONLY)
configure_file(cmake/modules/LibquentierConfigVersion.cmake.in
               ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}ConfigVersion.cmake @ONLY)

if(BUILD_WITH_NOTE_EDITOR OR BUILD_WITH_AUTHENTICATION_MANAGER)
  if(USE_QT5_WEBKIT)
    file(COPY cmake/modules/LibquentierFindQt5DependenciesWebKit.cmake DESTINATION ${PROJECT_BINARY_DIR})
    file(RENAME ${PROJECT_BINARY_DIR}/LibquentierFindQt5DependenciesWebKit.cmake ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindQtDependencies.cmake)
  elseif(Qt5Core_VERSION VERSION_LESS "5.6.0")
    file(COPY cmake/modules/LibquentierFindQt5DependenciesWebEngineNoCore.cmake DESTINATION ${PROJECT_BINARY_DIR})
    file(RENAME ${PROJECT_BINARY_DIR}/LibquentierFindQt5DependenciesWebEngineNoCore.cmake ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindQtDependencies.cmake)
  else()
    file(COPY cmake/modules/LibquentierFindQt5DependenciesWebEngineCore.cmake DESTINATION ${PROJECT_BINARY_DIR})
    file(RENAME ${PROJECT_BINARY_DIR}/LibquentierFindQt5DependenciesWebEngineCore.cmake ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindQtDependencies.cmake)
  endif()
else()
  file(COPY cmake/modules/LibquentierFindQt5DependenciesBase.cmake DESTINATION ${PROJECT_BINARY_DIR})
  file(RENAME ${PROJECT_BINARY_DIR}/LibquentierFindQt5DependenciesBase.cmake ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindQtDependencies.cmake)
endif()
file(COPY cmake/modules/LibquentierFindOtherDependenciesQt5.cmake DESTINATION ${PROJECT_BINARY_DIR})
file(RENAME ${PROJECT_BINARY_DIR}/LibquentierFindOtherDependenciesQt5.cmake ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindOtherDependencies.cmake)

# install shared library
install(TARGETS ${LIBNAME}
        EXPORT Libquentier-${LIBQUENTIER_QT_VERSION}LibraryDepends
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

foreach(ITEM ${LOCAL_STORAGE_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/local_storage)
endforeach()

foreach(ITEM ${SYNCHRONIZATION_HEADERS})
  install(FILES ${ITEM} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quentier/synchronization)
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
install(EXPORT Libquentier-${LIBQUENTIER_QT_VERSION}LibraryDepends DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/cmake/modules/LibquentierFindPackageWrapperMacro.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/cmake/modules/LibquentierFindHunspell.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/cmake/modules/hunspell_new_api_check.cpp DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/cmake/modules/LibquentierFindTidyHtml5.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindQtDependencies.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}FindOtherDependencies.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}Config.cmake DESTINATION ${INSTALL_CMAKE_DIR})
install(FILES ${PROJECT_BINARY_DIR}/Libquentier-${LIBQUENTIER_QT_VERSION}ConfigVersion.cmake DESTINATION ${INSTALL_CMAKE_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

add_custom_target(check COMMAND "${CMAKE_CTEST_COMMAND}" "--verbose")

set(LIBQUENTIER_CPPCHECKABLE_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/headers")
include(LibquentierSetupCppCheck)
