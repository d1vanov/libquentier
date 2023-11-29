/*
 * Copyright 2016-2020 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NoteEditor_p.h"

#include "GenericResourceImageManager.h"
#include "NoteEditorLocalStorageBroker.h"
#include "NoteEditorPrivateMacros.h"
#include "NoteEditorSettingsNames.h"
#include "ResourceDataInTemporaryFileStorageManager.h"

#include "delegates/AddHyperlinkToSelectedTextDelegate.h"
#include "delegates/AddResourceDelegate.h"
#include "delegates/DecryptEncryptedTextDelegate.h"
#include "delegates/EditHyperlinkDelegate.h"
#include "delegates/EncryptSelectedTextDelegate.h"
#include "delegates/ImageResourceRotationDelegate.h"
#include "delegates/InsertHtmlDelegate.h"
#include "delegates/RemoveHyperlinkDelegate.h"
#include "delegates/RemoveResourceDelegate.h"
#include "delegates/RenameResourceDelegate.h"

#include "javascript_glue/ActionsWatcher.h"
#include "javascript_glue/ContextMenuEventJavaScriptHandler.h"
#include "javascript_glue/PageMutationHandler.h"
#include "javascript_glue/ResizableImageJavaScriptHandler.h"
#include "javascript_glue/ResourceInfoJavaScriptHandler.h"
#include "javascript_glue/SpellCheckerDynamicHelper.h"
#include "javascript_glue/TableResizeJavaScriptHandler.h"
#include "javascript_glue/TextCursorPositionJavaScriptHandler.h"
#include "javascript_glue/ToDoCheckboxAutomaticInsertionHandler.h"
#include "javascript_glue/ToDoCheckboxOnClickHandler.h"

#include "undo_stack/AddHyperlinkUndoCommand.h"
#include "undo_stack/AddResourceUndoCommand.h"
#include "undo_stack/DecryptUndoCommand.h"
#include "undo_stack/EditHyperlinkUndoCommand.h"
#include "undo_stack/EncryptUndoCommand.h"
#include "undo_stack/HideDecryptedTextUndoCommand.h"
#include "undo_stack/ImageResizeUndoCommand.h"
#include "undo_stack/ImageResourceRotationUndoCommand.h"
#include "undo_stack/InsertHtmlUndoCommand.h"
#include "undo_stack/NoteEditorContentEditUndoCommand.h"
#include "undo_stack/RemoveHyperlinkUndoCommand.h"
#include "undo_stack/RemoveResourceUndoCommand.h"
#include "undo_stack/RenameResourceUndoCommand.h"
#include "undo_stack/ReplaceAllUndoCommand.h"
#include "undo_stack/ReplaceUndoCommand.h"
#include "undo_stack/SourceCodeFormatUndoCommand.h"
#include "undo_stack/SpellCheckAddToUserWordListUndoCommand.h"
#include "undo_stack/SpellCheckIgnoreWordUndoCommand.h"
#include "undo_stack/SpellCorrectionUndoCommand.h"
#include "undo_stack/TableActionUndoCommand.h"
#include "undo_stack/ToDoCheckboxAutomaticInsertionUndoCommand.h"
#include "undo_stack/ToDoCheckboxUndoCommand.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/note_editor/SpellChecker.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Checks.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/Size.h>
#include <quentier/utility/StandardPaths.h>

#include <QDesktopServices>
#include <QFileDialog>

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebFrame>
#include <QWebPage>

using WebSettings = QWebSettings;
using OwnershipNamespace = QWebFrame;

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#else // QUENTIER_USE_QT_WEB_ENGINE
#include "javascript_glue/EnCryptElementOnClickHandler.h"
#include "javascript_glue/GenericResourceImageJavaScriptHandler.h"
#include "javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.h"
#include "javascript_glue/HyperlinkClickJavaScriptHandler.h"
#include "javascript_glue/WebSocketWaiter.h"

#include "WebSocketClientWrapper.h"
#include "WebSocketTransport.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPageLayout>
#include <QPainter>
#include <QTimer>
#include <QWebEngineSettings>

#include <QtWebChannel>
#include <QtWebSockets/QWebSocketServer>
typedef QWebEngineSettings WebSettings;
#endif // QUENTIER_USE_QT_WEB_ENGINE

#include <quentier/enml/ENMLConverter.h>
#include <quentier/enml/HTMLCleaner.h>
#include <quentier/exception/NoteEditorInitializationException.h>
#include <quentier/exception/NoteEditorPluginInitializationException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Account.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/ResourceRecognitionIndexItem.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/ShortcutManager.h>

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontDialog>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMimeDatabase>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <QTransform>

#include <algorithm>
#include <cmath>

#define NOTE_EDITOR_PAGE_HEADER                                                \
    QStringLiteral(                                                            \
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "                 \
        "\"http://www.w3.org/TR/html4/strict.dtd\"><html><head>"               \
        "<meta http-equiv=\"Content-Type\" content=\"text/html\" "             \
        "charset=\"UTF-8\" />")

#define NOTE_EDITOR_PAGE_CSS                                                   \
    QStringLiteral(                                                            \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/jquery-ui.min.css\">"                                 \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/en-crypt.css\">"                                      \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/hover.css\">"                                         \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/en-decrypted.css\">"                                  \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/en-media-generic.css\">"                              \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/en-media-image.css\">"                                \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/image-area-hilitor.css\">"                            \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/en-todo.css\">"                                       \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/link.css\">"                                          \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/misspell.css\">"                                      \
        "<link rel=\"stylesheet\" type=\"text/css\" "                          \
        "href=\"qrc:/css/edit_cursor_trick.css\">")

namespace quentier {

namespace {

int fontMetricsWidth(
    const QFontMetrics & fontMetrics, const QString & text, int len = -1)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return fontMetrics.horizontalAdvance(text, len);
#else
    return fontMetrics.width(text, len);
#endif
}

} // namespace

NoteEditorPrivate::NoteEditorPrivate(NoteEditor & noteEditor) :
    INoteEditorBackend(&noteEditor),
#ifdef QUENTIER_USE_QT_WEB_ENGINE
    m_pWebSocketServer(new QWebSocketServer(
        QStringLiteral("QWebChannel server"), QWebSocketServer::NonSecureMode,
        this)),
    m_pWebSocketClientWrapper(
        new WebSocketClientWrapper(m_pWebSocketServer, this)),
    m_pWebChannel(new QWebChannel(this)),
    m_pEnCryptElementClickHandler(new EnCryptElementOnClickHandler(this)),
    m_pGenericResourceOpenAndSaveButtonsOnClickHandler(
        new GenericResourceOpenAndSaveButtonsOnClickHandler(this)),
    m_pHyperlinkClickJavaScriptHandler(
        new HyperlinkClickJavaScriptHandler(this)),
    m_pWebSocketWaiter(new WebSocketWaiter(this)),
#endif
    m_pSpellCheckerDynamicHandler(new SpellCheckerDynamicHelper(this)),
    m_pTableResizeJavaScriptHandler(new TableResizeJavaScriptHandler(this)),
    m_pResizableImageJavaScriptHandler(
        new ResizableImageJavaScriptHandler(this)),
    m_pToDoCheckboxClickHandler(new ToDoCheckboxOnClickHandler(this)),
    m_pToDoCheckboxAutomaticInsertionHandler(
        new ToDoCheckboxAutomaticInsertionHandler(this)),
    m_pPageMutationHandler(new PageMutationHandler(this)),
    m_pActionsWatcher(new ActionsWatcher(this)),
    m_pContextMenuEventJavaScriptHandler(
        new ContextMenuEventJavaScriptHandler(this)),
    m_pTextCursorPositionJavaScriptHandler(
        new TextCursorPositionJavaScriptHandler(this)),
    m_encryptionManager(new EncryptionManager),
    m_decryptedTextManager(new DecryptedTextManager),
    m_pFileIOProcessorAsync(new FileIOProcessorAsync),
    m_pResourceInfoJavaScriptHandler(
        new ResourceInfoJavaScriptHandler(m_resourceInfo, this)),
#ifdef QUENTIER_USE_QT_WEB_ENGINE
    m_pGenericResoureImageJavaScriptHandler(
        new GenericResourceImageJavaScriptHandler(
            m_genericResourceImageFilePathsByResourceHash, this)),
#endif
    q_ptr(&noteEditor)
{
    setupSkipRulesForHtmlToEnmlConversion();
    setupTextCursorPositionJavaScriptHandlerConnections();
    setupGeneralSignalSlotConnections();
    setupScripts();
    setAcceptDrops(false);
}

NoteEditorPrivate::~NoteEditorPrivate()
{
    QObject::disconnect(m_pFileIOProcessorAsync);
    m_pFileIOProcessorAsync->deleteLater();
}

void NoteEditorPrivate::setInitialPageHtml(const QString & html)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setInitialPageHtml: " << html);

    m_initialPageHtml = html;

    if (!m_pNote || !m_pNotebook) {
        clearEditorContent(BlankPageKind::Initial);
    }
}

void NoteEditorPrivate::setNoteNotFoundPageHtml(const QString & html)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::setNoteNotFoundPageHtml: " << html);

    m_noteNotFoundPageHtml = html;

    if (m_noteWasNotFound) {
        clearEditorContent(BlankPageKind::NoteNotFound);
    }
}

void NoteEditorPrivate::setNoteDeletedPageHtml(const QString & html)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::setNoteDeletedPageHtml: " << html);

    m_noteDeletedPageHtml = html;

    if (m_noteWasDeleted) {
        clearEditorContent(BlankPageKind::NoteDeleted);
    }
}

void NoteEditorPrivate::setNoteLoadingPageHtml(const QString & html)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::setNoteLoadingPageHtml: " << html);

    m_noteLoadingPageHtml = html;
}

bool NoteEditorPrivate::isNoteLoaded() const
{
    if (!m_pNote || !m_pNotebook) {
        return false;
    }

    return !m_pendingNotePageLoad && !m_pendingJavaScriptExecution &&
        !m_pendingNoteImageResourceTemporaryFiles;
}

qint64 NoteEditorPrivate::idleTime() const
{
    if (!isNoteLoaded()) {
        return -1;
    }

    return m_lastInteractionTimestamp;
}

void NoteEditorPrivate::onNoteLoadFinished(bool ok)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteLoadFinished: ok = "
            << (ok ? "true" : "false"));

    if (!ok) {
        QNDEBUG("note_editor", "Note page was not loaded successfully");
        // NOTE: most of the times this callback fires with ok = false shortly
        // before it fires with ok = true,
        // so settling with just a debug log entry here
        return;
    }

    m_pendingNotePageLoad = false;

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG("note_editor", "No note is set to the editor");
        setPageEditable(false);
        return;
    }

    if (Q_UNLIKELY(!m_pNotebook)) {
        QNDEBUG("note_editor", "No notebook is set to the editor");
        setPageEditable(false);
        return;
    }

    m_pendingJavaScriptExecution = true;

    GET_PAGE()
    page->stopJavaScriptAutoExecution();

    bool editable = true;
    if (m_pNote->hasActive() && !m_pNote->active()) {
        QNDEBUG(
            "note_editor",
            "Current note is not active, setting it to "
                << "read-only state");
        editable = false;
    }
    else if (m_pNote->isInkNote()) {
        QNDEBUG(
            "note_editor",
            "Current note is an ink note, setting it to "
                << "read-only state");
        editable = false;
    }
    else if (m_pNotebook->hasRestrictions()) {
        const auto & restrictions = m_pNotebook->restrictions();
        if (restrictions.noUpdateNotes.isSet() &&
            restrictions.noUpdateNotes.ref()) {
            QNDEBUG(
                "note_editor",
                "Notebook restrictions forbid the note "
                    << "modification, setting note's content to read-only "
                       "state");
            editable = false;
        }
    }
    else if (
        m_pNote->hasNoteAttributes() &&
        m_pNote->noteAttributes().contentClass.isSet() &&
        !m_pNote->noteAttributes().contentClass->isEmpty())
    {
        QNDEBUG(
            "note_editor",
            "Current note has non-empty content class, "
                << "setting it to read-only state");
        editable = false;
    }

    setPageEditable(editable);

    page->executeJavaScript(m_jQueryJs);
    page->executeJavaScript(m_jQueryUiJs);
    page->executeJavaScript(m_getSelectionHtmlJs);
    page->executeJavaScript(m_replaceSelectionWithHtmlJs);
    page->executeJavaScript(m_findReplaceManagerJs);

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QWebFrame * frame = page->mainFrame();
    if (Q_UNLIKELY(!frame)) {
        return;
    }

    frame->addToJavaScriptWindowObject(
        QStringLiteral("pageMutationObserver"), m_pPageMutationHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("resourceCache"), m_pResourceInfoJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("textCursorPositionHandler"),
        m_pTextCursorPositionJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("contextMenuEventHandler"),
        m_pContextMenuEventJavaScriptHandler, OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("toDoCheckboxClickHandler"), m_pToDoCheckboxClickHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("toDoCheckboxAutomaticInsertionHandler"),
        m_pToDoCheckboxAutomaticInsertionHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("tableResizeHandler"), m_pTableResizeJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("resizableImageHandler"),
        m_pResizableImageJavaScriptHandler, OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("spellCheckerDynamicHelper"),
        m_pSpellCheckerDynamicHandler, OwnershipNamespace::QtOwnership);

    frame->addToJavaScriptWindowObject(
        QStringLiteral("actionsWatcher"), m_pActionsWatcher,
        OwnershipNamespace::QtOwnership);

    page->executeJavaScript(m_onResourceInfoReceivedJs);
    page->executeJavaScript(m_qWebKitSetupJs);
#else
    page->executeJavaScript(m_qWebChannelJs);
    page->executeJavaScript(m_onResourceInfoReceivedJs);
    page->executeJavaScript(m_onGenericResourceImageReceivedJs);

    if (!m_webSocketReady) {
        QNDEBUG("note_editor", "Waiting for web socket connection");

        page->executeJavaScript(
            QStringLiteral("(function(){window.websocketserverport = ") +
            QString::number(m_webSocketServerPort) + QStringLiteral("})();"));

        page->executeJavaScript(m_qWebChannelSetupJs);
        page->startJavaScriptAutoExecution();
        return;
    }

    page->executeJavaScript(m_genericResourceOnClickHandlerJs);
    page->executeJavaScript(m_setupGenericResourceOnClickHandlerJs);
    page->executeJavaScript(m_provideSrcAndOnClickScriptForEnCryptImgTagsJs);
    page->executeJavaScript(m_provideSrcForGenericResourceImagesJs);
    page->executeJavaScript(m_clickInterceptorJs);
    page->executeJavaScript(m_notifyTextCursorPositionChangedJs);
#endif

    page->executeJavaScript(m_findInnermostElementJs);
    page->executeJavaScript(m_resizableTableColumnsJs);
    page->executeJavaScript(m_resizableImageManagerJs);
    page->executeJavaScript(m_debounceJs);
    page->executeJavaScript(m_rangyCoreJs);
    page->executeJavaScript(m_rangySelectionSaveRestoreJs);
    page->executeJavaScript(m_onTableResizeJs);
    page->executeJavaScript(m_nodeUndoRedoManagerJs);
    page->executeJavaScript(m_selectionManagerJs);
    page->executeJavaScript(m_textEditingUndoRedoManagerJs);
    page->executeJavaScript(m_snapSelectionToWordJs);
    page->executeJavaScript(m_updateResourceHashJs);
    page->executeJavaScript(m_updateImageResourceSrcJs);
    page->executeJavaScript(m_provideSrcForResourceImgTagsJs);
    page->executeJavaScript(m_determineStatesForCurrentTextCursorPositionJs);
    page->executeJavaScript(m_determineContextMenuEventTargetJs);
    page->executeJavaScript(m_tableManagerJs);
    page->executeJavaScript(m_resourceManagerJs);
    page->executeJavaScript(m_htmlInsertionManagerJs);
    page->executeJavaScript(m_sourceCodeFormatterJs);
    page->executeJavaScript(m_hyperlinkManagerJs);
    page->executeJavaScript(m_encryptDecryptManagerJs);
    page->executeJavaScript(m_hilitorJs);
    page->executeJavaScript(m_imageAreasHilitorJs);
    page->executeJavaScript(m_spellCheckerJs);
    page->executeJavaScript(m_managedPageActionJs);
    page->executeJavaScript(m_findAndReplaceDOMTextJs);
    page->executeJavaScript(m_replaceStyleJs);
    page->executeJavaScript(m_setFontFamilyJs);
    page->executeJavaScript(m_setFontSizeJs);

    if (m_isPageEditable) {
        QNTRACE("note_editor", "Note page is editable");
        page->executeJavaScript(m_setupEnToDoTagsJs);
        page->executeJavaScript(m_flipEnToDoCheckboxStateJs);
        page->executeJavaScript(m_toDoCheckboxAutomaticInsertionJs);
        page->executeJavaScript(m_tabAndShiftTabIndentAndUnindentReplacerJs);
    }

    updateColResizableTableBindings();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
    page->executeJavaScript(m_setupTextCursorPositionTrackingJs);
    setupTextCursorPositionTracking();
    setupGenericResourceImages();
#endif

    if (!m_pendingNoteImageResourceTemporaryFiles) {
        provideSrcForResourceImgTags();

        highlightRecognizedImageAreas(
            m_lastSearchHighlightedText,
            m_lastSearchHighlightedTextCaseSensitivity);
    }

    // Set the caret position to the end of the body
    page->executeJavaScript(m_setInitialCaretPositionJs);

    // Disable the keyboard modifiers to prevent auto-triggering of note editor
    // page actions - they should go through the preprocessing of the note
    // editor
    page->executeJavaScript(m_disablePasteJs);

    // NOTE: executing page mutation observer's script last
    // so that it doesn't catch the mutations originating from the above scripts
    page->executeJavaScript(m_pageMutationObserverJs);

    if (m_spellCheckerEnabled) {
        applySpellCheck();
    }

    QNTRACE(
        "note_editor",
        "Sent commands to execute all the page's necessary "
            << "scripts");

    page->startJavaScriptAutoExecution();
}

void NoteEditorPrivate::onContentChanged()
{
    QNTRACE("note_editor", "NoteEditorPrivate::onContentChanged");

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution)
    {
        QNTRACE(
            "note_editor",
            "Skipping the content change as the note page "
                << "has not fully loaded yet");
        return;
    }

    if (m_skipPushingUndoCommandOnNextContentChange) {
        m_skipPushingUndoCommandOnNextContentChange = false;
        QNTRACE(
            "note_editor",
            "Skipping the push of edit undo command on this "
                << "content change");
    }
    else {
        pushNoteContentEditUndoCommand();
    }

    setModified();

    if (Q_LIKELY(m_watchingForContentChange)) {
        m_contentChangedSinceWatchingStart = true;
        return;
    }

    m_pageToNoteContentPostponeTimerId =
        startTimer(secondsToMilliseconds(m_secondsToWaitBeforeConversionStart));

    m_watchingForContentChange = true;
    m_contentChangedSinceWatchingStart = false;

    QNTRACE(
        "note_editor",
        "Started timer to postpone note editor page's "
            << "content to ENML conversion: timer id = "
            << m_pageToNoteContentPostponeTimerId);
}

void NoteEditorPrivate::onResourceFileChanged(
    QString resourceLocalUid, QString fileStoragePath, QByteArray resourceData,
    QByteArray resourceDataHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onResourceFileChanged: "
            << "resource local uid = " << resourceLocalUid
            << ", file storage path: " << fileStoragePath
            << ", new resource data size = "
            << humanReadableSize(
                   static_cast<quint64>(std::max(resourceData.size(), 0)))
            << ", resource data hash = " << resourceDataHash.toHex());

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG(
            "note_editor",
            "Can't process resource file change: no note is "
                << "set to the editor");
        return;
    }

    QList<Resource> resources = m_pNote->resources();
    const int numResources = resources.size();
    int targetResourceIndex = -1;
    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = qAsConst(resources)[i];
        if (resource.localUid() == resourceLocalUid) {
            targetResourceIndex = i;
            break;
        }
    }

    if (Q_UNLIKELY(targetResourceIndex < 0)) {
        QNDEBUG(
            "note_editor",
            "Can't process resource file change: can't find "
                << "the resource by local uid within note's resources");
        return;
    }

    Resource resource = qAsConst(resources)[targetResourceIndex];

    QByteArray previousResourceHash =
        (resource.hasDataHash() ? resource.dataHash() : QByteArray());

    QNTRACE(
        "note_editor",
        "Previous resource hash = " << previousResourceHash.toHex());

    if (!previousResourceHash.isEmpty() &&
        (previousResourceHash == resourceDataHash) && resource.hasDataSize() &&
        (resource.dataSize() == resourceData.size()))
    {
        QNDEBUG(
            "note_editor",
            "Neither resource hash nor binary data size has "
                << "changed -> the resource data has not actually changed, "
                << "nothing to do");
        return;
    }

    resource.setDataBody(resourceData);
    resource.setDataHash(resourceDataHash);
    resource.setDataSize(resourceData.size());

    // Need to clear any existing recognition data as the resource's contents
    // were changed
    resource.setRecognitionDataBody(QByteArray());
    resource.setRecognitionDataHash(QByteArray());
    resource.setRecognitionDataSize(-1);

    QString resourceMimeTypeName =
        (resource.hasMime() ? resource.mime() : QString());

    QString resourceDisplayName = resource.displayName();
    QString resourceDisplaySize =
        humanReadableSize(static_cast<quint64>(resourceData.size()));

    QNTRACE(
        "note_editor", "Updating the resource within the note: " << resource);

    Q_UNUSED(m_pNote->updateResource(resource))
    setModified();

    if (!previousResourceHash.isEmpty() &&
        (previousResourceHash != resourceDataHash))
    {
        QSize resourceImageSize;
        if (resource.hasHeight() && resource.hasWidth()) {
            resourceImageSize.setHeight(resource.height());
            resourceImageSize.setWidth(resource.width());
        }

        m_resourceInfo.removeResourceInfo(previousResourceHash);

        m_resourceInfo.cacheResourceInfo(
            resourceDataHash, resourceDisplayName, resourceDisplaySize,
            fileStoragePath, resourceImageSize);

        updateHashForResourceTag(previousResourceHash, resourceDataHash);
    }

    if (resourceMimeTypeName.startsWith(QStringLiteral("image/"))) {
        removeSymlinksToImageResourceFile(resourceLocalUid);

        ErrorString errorDescription;
        QString linkFilePath = createSymlinkToImageResourceFile(
            fileStoragePath, resourceLocalUid, errorDescription);

        if (linkFilePath.isEmpty()) {
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        m_resourceFileStoragePathsByResourceLocalUid[resourceLocalUid] =
            linkFilePath;

        QSize resourceImageSize;
        if (resource.hasHeight() && resource.hasWidth()) {
            resourceImageSize.setHeight(resource.height());
            resourceImageSize.setWidth(resource.width());
        }

        m_resourceInfo.cacheResourceInfo(
            resourceDataHash, resourceDisplayName, resourceDisplaySize,
            linkFilePath, resourceImageSize);

        if (!m_pendingNotePageLoad) {
            GET_PAGE()
            page->executeJavaScript(
                QStringLiteral("updateImageResourceSrc('") +
                QString::fromLocal8Bit(resourceDataHash.toHex()) +
                QStringLiteral("', '") + linkFilePath + QStringLiteral("', ") +
                QString::number(
                    resource.hasHeight() ? resource.height() : qint16(0)) +
                QStringLiteral(", ") +
                QString::number(
                    resource.hasWidth() ? resource.width() : qint16(0)) +
                QStringLiteral(");"));
        }
    }
    else {
#ifdef QUENTIER_USE_QT_WEB_ENGINE
        QImage image = buildGenericResourceImage(resource);
        saveGenericResourceImage(resource, image);
#else
        if (m_pPluginFactory) {
            m_pPluginFactory->updateResource(resource);
        }
#endif
    }
}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void NoteEditorPrivate::onGenericResourceImageSaved(
    bool success, QByteArray resourceActualHash, QString filePath,
    ErrorString errorDescription, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onGenericResourceImageSaved: "
            << "success = " << (success ? "true" : "false")
            << ", resource actual hash = " << resourceActualHash.toHex()
            << ", file path = " << filePath << ", error description = "
            << errorDescription << ", requestId = " << requestId);

    auto it = m_saveGenericResourceImageToFileRequestIds.find(requestId);
    if (it == m_saveGenericResourceImageToFileRequestIds.end()) {
        QNDEBUG("note_editor", "Haven't found request id in the cache");
        return;
    }

    Q_UNUSED(m_saveGenericResourceImageToFileRequestIds.erase(it));

    if (Q_UNLIKELY(!success)) {
        ErrorString error(
            QT_TR_NOOP("Can't save the generic resource image to file"));

        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT notifyError(error);
        return;
    }

    m_genericResourceImageFilePathsByResourceHash[resourceActualHash] =
        filePath;

    QNDEBUG(
        "note_editor",
        "Cached generic resource image file path "
            << filePath << " for resource hash " << resourceActualHash.toHex());

    if (m_saveGenericResourceImageToFileRequestIds.empty()) {
        provideSrcForGenericResourceImages();
        setupGenericResourceOnClickHandler();
    }
}

void NoteEditorPrivate::onHyperlinkClicked(QString url)
{
    handleHyperlinkClicked(QUrl(url));
}

void NoteEditorPrivate::onWebSocketReady()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onWebSocketReady");

    m_webSocketReady = true;
    onNoteLoadFinished(true);
}

#else

void NoteEditorPrivate::onHyperlinkClicked(QUrl url)
{
    handleHyperlinkClicked(url);
}

#endif // QUENTIER_USE_QT_WEB_ENGINE

void NoteEditorPrivate::onToDoCheckboxClicked(quint64 enToDoCheckboxId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onToDoCheckboxClicked: " << enToDoCheckboxId);

    setModified();
    auto * pCommand = new ToDoCheckboxUndoCommand(enToDoCheckboxId, *this);

    QObject::connect(
        pCommand, &ToDoCheckboxUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::onToDoCheckboxClickHandlerError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onToDoCheckboxClickHandlerError: " << error);

    Q_EMIT notifyError(error);
}

void NoteEditorPrivate::onToDoCheckboxInserted(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::onToDoCheckboxInserted: " << data);

    Q_UNUSED(extraData)

    QMap<QString, QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of ToDo checkbox "
                       "insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of ToDo checkbox "
                           "insertion undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the ToDo checkbox insertion"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto * pCommand = new ToDoCheckboxAutomaticInsertionUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this,
            &NoteEditorPrivate::
                onToDoCheckboxAutomaticInsertionUndoRedoFinished));

    QObject::connect(
        pCommand, &ToDoCheckboxAutomaticInsertionUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    setModified();
}

void NoteEditorPrivate::onToDoCheckboxAutomaticInsertion()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onToDoCheckboxAutomaticInsertion");

    auto * pCommand = new ToDoCheckboxAutomaticInsertionUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this,
            &NoteEditorPrivate::
                onToDoCheckboxAutomaticInsertionUndoRedoFinished));

    QObject::connect(
        pCommand, &ToDoCheckboxAutomaticInsertionUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    ++m_lastFreeEnToDoIdNumber;
    setModified();
}

void NoteEditorPrivate::onToDoCheckboxAutomaticInsertionUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onToDoCheckboxAutomaticInsertionUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    QMap<QString, QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of ToDo checkbox "
                       "automatic insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of ToDo checkbox "
                           "automatic insertion undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the ToDo checkbox automatic "
                           "insertion"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    setModified();
}

void NoteEditorPrivate::onJavaScriptLoaded()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onJavaScriptLoaded");

    NoteEditorPage * pSenderPage = qobject_cast<NoteEditorPage *>(sender());
    if (Q_UNLIKELY(!pSenderPage)) {
        QNDEBUG(
            "note_editor",
            "Can't get the pointer to NoteEditor page from which the event of "
                << "JavaScrupt loading came in, probably it is already dead");
        return;
    }

    GET_PAGE()
    if (page != pSenderPage) {
        QNDEBUG(
            "note_editor",
            "Skipping JavaScript loaded event from page "
                << "which is not the currently set one");
        return;
    }

    if (m_pendingJavaScriptExecution) {
        m_pendingJavaScriptExecution = false;

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG(
                "note_editor",
                "No note is set to the editor, won't "
                    << "retrieve the editor content's html");
            return;
        }

        if (Q_UNLIKELY(!m_pNotebook)) {
            QNDEBUG(
                "note_editor",
                "No notebook is set to the editor, won't "
                    << "retrieve the editor content's html");
            return;
        }

#ifndef QUENTIER_USE_QT_WEB_ENGINE
        m_htmlCachedMemory = page->mainFrame()->toHtml();
        onPageHtmlReceived(m_htmlCachedMemory);
#else
        page->toHtml(NoteEditorCallbackFunctor<QString>(
            this, &NoteEditorPrivate::onPageHtmlReceived));
#endif

        QNTRACE("note_editor", "Emitting noteLoaded signal");
        Q_EMIT noteLoaded();
    }

    if (m_pendingBodyStyleUpdate) {
        m_pendingBodyStyleUpdate = false;
        updateBodyStyle();
    }
}

void NoteEditorPrivate::onOpenResourceRequest(const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onOpenResourceRequest: " << resourceHash.toHex());

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't open the resource: no note is set "
                       "to the editor"));
        QNWARNING(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't open attachment"))

    QList<Resource> resources = m_pNote->resources();
    int resourceIndex = resourceIndexByHash(resources, resourceHash);
    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("The resource to be opened was not found "
                       "within the note"));
        QNWARNING("note_editor", error << ", resource hash = " << resourceHash);
        Q_EMIT notifyError(error);
        return;
    }

    const Resource & resource = resources[resourceIndex];
    const QString resourceLocalUid = resource.localUid();

    auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalUid](const auto & pair) {
            return pair.first == resourceLocalUid;
        });

    if (it == m_prepareResourceForOpeningProgressDialogs.end()) {
        auto pProgressDialog = new QProgressDialog(
            tr("Preparing to open attachment") + QStringLiteral("..."),
            QString(),
            /* min = */ 0,
            /* max = */ 100, this, Qt::Dialog);

        pProgressDialog->setWindowModality(Qt::WindowModal);
        pProgressDialog->setMinimumDuration(2000);

        m_prepareResourceForOpeningProgressDialogs.emplace_back(
            std::make_pair(resourceLocalUid, pProgressDialog));
    }

    QNTRACE(
        "note_editor",
        "Emitting the request to open resource with "
            << "local uid " << resourceLocalUid);
    Q_EMIT openResourceFile(resourceLocalUid);
}

void NoteEditorPrivate::onSaveResourceRequest(const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onSaveResourceRequest: " << resourceHash.toHex());

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't save the resource to file: no note "
                       "is set to the editor"));
        QNINFO(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    QList<Resource> resources = m_pNote->resources();
    int resourceIndex = resourceIndexByHash(resources, resourceHash);
    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("The resource to be saved was not found "
                       "within the note"));
        QNINFO(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        return;
    }

    const Resource & resource = qAsConst(resources)[resourceIndex];

    if (!resource.hasDataBody() && !resource.hasAlternateDataBody()) {
        QNTRACE(
            "note_editor",
            "The resource meant to be saved to a local file "
                << "has neither data body nor alternate data body, "
                << "need to request these from the local storage");
        Q_UNUSED(m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile
                     .insert(resource.localUid()))
        Q_EMIT findResourceData(resource.localUid());
        return;
    }

    manualSaveResourceToFile(resource);
}

void NoteEditorPrivate::contextMenuEvent(QContextMenuEvent * pEvent)
{
    QNTRACE("note_editor", "NoteEditorPrivate::contextMenuEvent");

    if (Q_UNLIKELY(!pEvent)) {
        QNINFO("note_editor", "detected null pointer to context menu event");
        return;
    }

    if (m_pendingIndexHtmlWritingToFile || m_pendingNotePageLoad ||
        m_pendingJavaScriptExecution ||
        m_pendingNoteImageResourceTemporaryFiles)
    {
        QNINFO(
            "note_editor",
            "Ignoring context menu event for now, "
                << "until the note is fully loaded...");
        return;
    }

    m_lastContextMenuEventGlobalPos = pEvent->globalPos();
    m_lastContextMenuEventPagePos = pEvent->pos();

    QNTRACE(
        "note_editor",
        "Context menu event's global pos: x = "
            << m_lastContextMenuEventGlobalPos.x()
            << ", y = " << m_lastContextMenuEventGlobalPos.y()
            << "; pos relative to child widget: x = "
            << m_lastContextMenuEventPagePos.x()
            << ", y = " << m_lastContextMenuEventPagePos.y()
            << "; context menu sequence number = "
            << m_contextMenuSequenceNumber);

    determineContextMenuEventTarget();
}

void NoteEditorPrivate::onContextMenuEventReply(
    QString contentType, QString selectedHtml, bool insideDecryptedTextFragment,
    QStringList extraData, quint64 sequenceNumber)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onContextMenuEventReply: "
            << "content type = " << contentType << ", selected html = "
            << selectedHtml << ", inside decrypted text fragment = "
            << (insideDecryptedTextFragment ? "true" : "false")
            << ", extraData: [" << extraData.join(QStringLiteral(", "))
            << "], sequence number = " << sequenceNumber);

    if (!checkContextMenuSequenceNumber(sequenceNumber)) {
        QNTRACE(
            "note_editor",
            "Sequence number is not valid, not doing "
                << "anything");
        return;
    }

    ++m_contextMenuSequenceNumber;

    m_currentContextMenuExtraData.m_contentType = contentType;
    m_currentContextMenuExtraData.m_insideDecryptedText =
        insideDecryptedTextFragment;

    if (contentType == QStringLiteral("GenericText")) {
        setupGenericTextContextMenu(
            extraData, selectedHtml, insideDecryptedTextFragment);
    }
    else if (
        (contentType == QStringLiteral("ImageResource")) ||
        (contentType == QStringLiteral("NonImageResource")))
    {
        if (Q_UNLIKELY(extraData.empty())) {
            ErrorString error(
                QT_TR_NOOP("Can't display the resource context menu: "
                           "the extra data from JavaScript is empty"));
            QNWARNING("note_editor", error);
            Q_EMIT notifyError(error);
            return;
        }

        if (Q_UNLIKELY(extraData.size() != 1)) {
            ErrorString error(
                QT_TR_NOOP("Can't display the resource context menu: "
                           "the extra data from JavaScript has wrong size"));
            error.details() = QString::number(extraData.size());
            QNWARNING("note_editor", error);
            Q_EMIT notifyError(error);
            return;
        }

        auto resourceHash = QByteArray::fromHex(extraData[0].toLocal8Bit());

        if (contentType == QStringLiteral("ImageResource")) {
            setupImageResourceContextMenu(resourceHash);
        }
        else {
            setupNonImageResourceContextMenu(resourceHash);
        }
    }
    else if (contentType == QStringLiteral("EncryptedText")) {
        QString cipher, keyLength, encryptedText, decryptedText, hint, id;
        ErrorString error;
        bool res = parseEncryptedTextContextMenuExtraData(
            extraData, encryptedText, decryptedText, cipher, keyLength, hint,
            id, error);

        if (Q_UNLIKELY(!res)) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't display the encrypted text's context menu"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        setupEncryptedTextContextMenu(
            cipher, keyLength, encryptedText, hint, id);
    }
    else {
        QNWARNING(
            "note_editor",
            "Unknown content type on context menu event "
                << "reply: " << contentType << ", sequence number "
                << sequenceNumber);
    }
}

void NoteEditorPrivate::onTextCursorPositionChange()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onTextCursorPositionChange");

    if (!m_pendingIndexHtmlWritingToFile && !m_pendingNotePageLoad &&
        !m_pendingJavaScriptExecution)
    {
        determineStatesForCurrentTextCursorPosition();
    }
}

void NoteEditorPrivate::onTextCursorBoldStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorBoldStateChanged: "
            << (state ? "bold" : "not bold"));

    m_currentTextFormattingState.m_bold = state;
    Q_EMIT textBoldState(state);
}

void NoteEditorPrivate::onTextCursorItalicStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorItalicStateChanged: "
            << (state ? "italic" : "not italic"));

    m_currentTextFormattingState.m_italic = state;
    Q_EMIT textItalicState(state);
}

void NoteEditorPrivate::onTextCursorUnderlineStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorUnderlineStateChanged: "
            << (state ? "underline" : "not underline"));

    m_currentTextFormattingState.m_underline = state;
    Q_EMIT textUnderlineState(state);
}

void NoteEditorPrivate::onTextCursorStrikethgouthStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorStrikethgouthStateChanged: "
            << (state ? "strikethrough" : "not strikethrough"));

    m_currentTextFormattingState.m_strikethrough = state;
    Q_EMIT textStrikethroughState(state);
}

void NoteEditorPrivate::onTextCursorAlignLeftStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorAlignLeftStateChanged: "
            << (state ? "true" : "false"));

    if (state) {
        m_currentTextFormattingState.m_alignment = Alignment::Left;
    }

    Q_EMIT textAlignLeftState(state);
}

void NoteEditorPrivate::onTextCursorAlignCenterStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorAlignCenterStateChanged: "
            << (state ? "true" : "false"));

    if (state) {
        m_currentTextFormattingState.m_alignment = Alignment::Center;
    }

    Q_EMIT textAlignCenterState(state);
}

void NoteEditorPrivate::onTextCursorAlignRightStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorAlignRightStateChanged: "
            << (state ? "true" : "false"));

    if (state) {
        m_currentTextFormattingState.m_alignment = Alignment::Right;
    }

    Q_EMIT textAlignRightState(state);
}

void NoteEditorPrivate::onTextCursorAlignFullStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorAlignFullStateChanged: "
            << (state ? "true" : "false"));

    if (state) {
        m_currentTextFormattingState.m_alignment = Alignment::Full;
    }

    Q_EMIT textAlignFullState(state);
}

void NoteEditorPrivate::onTextCursorInsideOrderedListStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorInsideOrderedListStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideOrderedList = state;
    Q_EMIT textInsideOrderedListState(state);
}

void NoteEditorPrivate::onTextCursorInsideUnorderedListStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorInsideUnorderedListStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideUnorderedList = state;
    Q_EMIT textInsideUnorderedListState(state);
}

void NoteEditorPrivate::onTextCursorInsideTableStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorInsideTableStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideTable = state;
    Q_EMIT textInsideTableState(state);
}

void NoteEditorPrivate::onTextCursorOnImageResourceStateChanged(
    bool state, QByteArray resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorOnImageResourceStateChanged: "
            << (state ? "yes" : "no")
            << ", resource hash = " << resourceHash.toHex());

    m_currentTextFormattingState.m_onImageResource = state;
    if (state) {
        m_currentTextFormattingState.m_resourceHash =
            QString::fromLocal8Bit(resourceHash);
    }
}

void NoteEditorPrivate::onTextCursorOnNonImageResourceStateChanged(
    bool state, QByteArray resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorOnNonImageResourceStateChanged: "
            << (state ? "yes" : "no")
            << ", resource hash = " << resourceHash.toHex());

    m_currentTextFormattingState.m_onNonImageResource = state;
    if (state) {
        m_currentTextFormattingState.m_resourceHash =
            QString::fromLocal8Bit(resourceHash);
    }
}

void NoteEditorPrivate::onTextCursorOnEnCryptTagStateChanged(
    bool state, QString encryptedText, QString cipher, QString length)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onTextCursorOnEnCryptTagStateChanged: "
            << (state ? "yes" : "no") << ", encrypted text = " << encryptedText
            << ", cipher = " << cipher << ", length = " << length);

    m_currentTextFormattingState.m_onEnCryptTag = state;
    if (state) {
        m_currentTextFormattingState.m_encryptedText = encryptedText;
        m_currentTextFormattingState.m_cipher = cipher;
        m_currentTextFormattingState.m_length = length;
    }
}

void NoteEditorPrivate::onTextCursorFontNameChanged(QString fontName)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorFontNameChanged: "
            << "font name = " << fontName);
    Q_EMIT textFontFamilyChanged(fontName);
}

void NoteEditorPrivate::onTextCursorFontSizeChanged(int fontSize)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorFontSizeChanged: "
            << "font size = " << fontSize);
    Q_EMIT textFontSizeChanged(fontSize);
}

void NoteEditorPrivate::onWriteFileRequestProcessed(
    bool success, ErrorString errorDescription, QUuid requestId)
{
    if (requestId == m_writeNoteHtmlToFileRequestId) {
        QNDEBUG(
            "note_editor",
            "Write note html to file completed: success = "
                << (success ? "true" : "false")
                << ", request id = " << requestId);

        m_writeNoteHtmlToFileRequestId = QUuid();
        m_pendingIndexHtmlWritingToFile = false;

        if (!success) {
            ErrorString error(QT_TR_NOOP("Could not write note html to file"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = errorDescription.details();
            clearEditorContent(BlankPageKind::InternalError, error);
            Q_EMIT notifyError(error);
            return;
        }

        QUrl url = QUrl::fromLocalFile(noteEditorPagePath());
        QNDEBUG("note_editor", "URL to use for page loading: " << url);

        m_pendingNextPageUrl = url;

        if (m_pendingNotePageLoadMethodExit) {
            QNDEBUG(
                "note_editor",
                "Already loading something into the editor, "
                    << "need to wait for the previous note load to complete");
            return;
        }

        while (!m_pendingNextPageUrl.isEmpty()) {
            /**
             * WARNING: the piece of code just below is trickier than it might
             * seem, thanks to Qt developers. Make sure to read comments in
             * NoteEditor_p.h near the declaration of class members
             * m_pendingNotePageLoadMethodExit and m_pendingNextPageUrl to see
             * how it works
             */

            QNDEBUG(
                "note_editor",
                "Setting the pending url: " << m_pendingNextPageUrl);

            url = m_pendingNextPageUrl;
            m_pendingNotePageLoad = true;
            m_pendingNotePageLoadMethodExit = true;
#ifdef QUENTIER_USE_QT_WEB_ENGINE
            page()->setUrl(url);
#else
            page()->mainFrame()->setUrl(url);
#endif
            m_pendingNotePageLoadMethodExit = false;
            QNDEBUG(
                "note_editor",
                "After having started to load the url "
                    << "into the page: " << url);

            /**
             * Check that while we were within setUrl method, the next URL to be
             * loaded has not changed; if so, just clear the member variable and
             * exit from the loop; otherwise, repeat the loop
             */
            if (url == m_pendingNextPageUrl) {
                m_pendingNextPageUrl.clear();
                break;
            }
        }
    }

    auto manualSaveResourceIt =
        m_manualSaveResourceToFileRequestIds.find(requestId);
    if (manualSaveResourceIt != m_manualSaveResourceToFileRequestIds.end()) {
        if (success) {
            QNDEBUG(
                "note_editor",
                "Successfully saved resource to file for "
                    << "request id " << requestId);
        }
        else {
            QNWARNING(
                "note_editor",
                "Could not save resource to file: " << errorDescription);
        }

        Q_UNUSED(
            m_manualSaveResourceToFileRequestIds.erase(manualSaveResourceIt));
        return;
    }
}

void NoteEditorPrivate::onSelectionFormattedAsSourceCode(
    const QVariant & response,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::onSelectionFormattedAsSourceCode");

    Q_UNUSED(extraData)
    QMap<QString, QVariant> resultMap = response.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't find the status within the result "
                       "of selection formatting as source code"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;
        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Internal error: can't parse the error of "
                           "selection formatting as source code from "
                           "JavaScript"));
        }
        else {
            QString errorValue = errorIt.value().toString();
            if (!errorValue.isEmpty()) {
                error.setBase(
                    QT_TR_NOOP("Internal error: can't format "
                               "the selection as source code"));
                error.details() = errorValue;
                QNWARNING("note_editor", error);
                Q_EMIT notifyError(error);
            }
            else {
                QString feedback;
                auto feedbackIt = resultMap.find(QStringLiteral("feedback"));
                if (feedbackIt != resultMap.end()) {
                    feedback = feedbackIt.value().toString();
                }

                if (Q_UNLIKELY(feedback.isEmpty())) {
                    error.setBase(
                        QT_TR_NOOP("Internal error: can't format the selection "
                                   "as source code, unknown error"));
                    QNWARNING("note_editor", error);
                    Q_EMIT notifyError(error);
                }
                else {
                    QNDEBUG("note_editor", feedback);
                }
            }
        }

        return;
    }

    auto * pCommand = new SourceCodeFormatUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSourceCodeFormatUndoRedoFinished));

    QObject::connect(
        pCommand, &SourceCodeFormatUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onAddResourceDelegateFinished(
    Resource addedResource, QString resourceFileStoragePath)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onAddResourceDelegateFinished: "
            << "resource file storage path = " << resourceFileStoragePath);

    QNTRACE("note_editor", addedResource);

    if (Q_UNLIKELY(!addedResource.hasDataHash())) {
        ErrorString error(
            QT_TR_NOOP("The added resource doesn't contain the data hash"));
        QNWARNING("note_editor", error);
        removeResourceFromNote(addedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!addedResource.hasDataSize())) {
        ErrorString error(
            QT_TR_NOOP("The added resource doesn't contain the data size"));
        QNWARNING("note_editor", error);
        removeResourceFromNote(addedResource);
        Q_EMIT notifyError(error);
        return;
    }

    m_resourceFileStoragePathsByResourceLocalUid[addedResource.localUid()] =
        resourceFileStoragePath;

    QSize resourceImageSize;
    if (addedResource.hasHeight() && addedResource.hasWidth()) {
        resourceImageSize.setHeight(addedResource.height());
        resourceImageSize.setWidth(addedResource.width());
    }

    m_resourceInfo.cacheResourceInfo(
        addedResource.dataHash(), addedResource.displayName(),
        humanReadableSize(static_cast<quint64>(addedResource.dataSize())),
        resourceFileStoragePath, resourceImageSize);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    setupGenericResourceImages();
#endif

    provideSrcForResourceImgTags();

    auto * pCommand = new AddResourceUndoCommand(
        addedResource,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onAddResourceUndoRedoFinished),
        *this);

    QObject::connect(
        pCommand, &AddResourceUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * pDelegate = qobject_cast<AddResourceDelegate *>(sender());
    if (Q_LIKELY(pDelegate)) {
        pDelegate->deleteLater();
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onAddResourceDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onAddResourceDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<AddResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onAddResourceUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onAddResourceUndoRedoFinished: " << data);

    Q_UNUSED(extraData);

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of new resource "
                       "html insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of new resource "
                           "html insertion undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the new resource html "
                           "insertion into the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRemoveResourceDelegateFinished(
    Resource removedResource, bool reversible)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRemoveResourceDelegateFinished: removed resource = "
            << removedResource << "\nReversible: " << reversible);

    if (reversible) {
        NoteEditorCallbackFunctor<QVariant> callback(
            this, &NoteEditorPrivate::onRemoveResourceUndoRedoFinished);

        auto * pCommand =
            new RemoveResourceUndoCommand(removedResource, callback, *this);

        QObject::connect(
            pCommand, &RemoveResourceUndoCommand::notifyError, this,
            &NoteEditorPrivate::onUndoCommandError);

        m_pUndoStack->push(pCommand);
    }

    auto * delegate = qobject_cast<RemoveResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRemoveResourceDelegateCancelled(
    QString resourceLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRemoveResourceDelegateCancelled: resource local uid = "
            << resourceLocalUid);

    auto * delegate = qobject_cast<RemoveResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRemoveResourceDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onRemoveResourceDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<RemoveResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRemoveResourceUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRemoveResourceUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    if (!m_lastSearchHighlightedText.isEmpty()) {
        highlightRecognizedImageAreas(
            m_lastSearchHighlightedText,
            m_lastSearchHighlightedTextCaseSensitivity);
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRenameResourceDelegateFinished(
    QString oldResourceName, QString newResourceName, Resource resource,
    bool performingUndo)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRenameResourceDelegateFinished: old resource name = "
            << oldResourceName << ", new resource name = " << newResourceName
            << ", performing undo = " << (performingUndo ? "true" : "false"));

    QNTRACE("note_editor", "Resource: " << resource);

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    if (m_pPluginFactory) {
        m_pPluginFactory->updateResource(resource);
    }
#endif

    if (!performingUndo) {
        auto * pCommand = new RenameResourceUndoCommand(
            resource, oldResourceName, *this, m_pGenericResourceImageManager,
            m_genericResourceImageFilePathsByResourceHash);

        QObject::connect(
            pCommand, &RenameResourceUndoCommand::notifyError, this,
            &NoteEditorPrivate::onUndoCommandError);

        m_pUndoStack->push(pCommand);
    }

    auto * delegate = qobject_cast<RenameResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRenameResourceDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRenameResourceDelegateCancelled");

    auto * delegate = qobject_cast<RenameResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRenameResourceDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onRenameResourceDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<RenameResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onImageResourceRotationDelegateFinished(
    QByteArray resourceDataBefore, QByteArray resourceHashBefore,
    QByteArray resourceRecognitionDataBefore,
    QByteArray resourceRecognitionDataHashBefore, QSize resourceImageSizeBefore,
    Resource resourceAfter, INoteEditorBackend::Rotation rotationDirection)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onImageResourceRotationDelegateFinished: "
            << "previous resource hash = " << resourceHashBefore.toHex()
            << ", resource local uid = " << resourceAfter.localUid()
            << ", rotation direction = " << rotationDirection);

    auto * pCommand = new ImageResourceRotationUndoCommand(
        resourceDataBefore, resourceHashBefore, resourceRecognitionDataBefore,
        resourceRecognitionDataHashBefore, resourceImageSizeBefore,
        resourceAfter, rotationDirection, *this);

    QObject::connect(
        pCommand, &ImageResourceRotationUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate = qobject_cast<ImageResourceRotationDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    if (m_pNote) {
        Q_UNUSED(m_pNote->updateResource(resourceAfter))
    }

    highlightRecognizedImageAreas(
        m_lastSearchHighlightedText,
        m_lastSearchHighlightedTextCaseSensitivity);

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onImageResourceRotationDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onImageResourceRotationDelegateError");

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<ImageResourceRotationDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onHideDecryptedTextFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onHideDecryptedTextFinished: " << data);

    Q_UNUSED(extraData)

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of decrypted text "
                       "hiding from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of decrypted text "
                           "hiding from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't hide the decrypted text"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    setModified();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
#endif

    auto * pCommand = new HideDecryptedTextUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onHideDecryptedTextUndoRedoFinished));

    QObject::connect(
        pCommand, &HideDecryptedTextUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::onHideDecryptedTextUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onHideDecryptedTextUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of decrypted text "
                       "hiding undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of decrypted text "
                           "hiding undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the decrypted text hiding"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
#endif
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateFinished()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEncryptSelectedTextDelegateFinished");

    auto * pCommand = new EncryptUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onEncryptSelectedTextUndoRedoFinished));

    QObject::connect(
        pCommand, &EncryptUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate = qobject_cast<EncryptSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
#endif
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEncryptSelectedTextDelegateCancelled");

    auto * delegate = qobject_cast<EncryptSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEncryptSelectedTextDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<EncryptSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEncryptSelectedTextUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEncryptSelectedTextUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of encryption "
                       "undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of encryption "
                           "undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the selected text encryption"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
#endif
}

void NoteEditorPrivate::onDecryptEncryptedTextDelegateFinished(
    QString encryptedText, QString cipher, size_t length, QString hint,
    QString decryptedText, QString passphrase, bool rememberForSession,
    bool decryptPermanently)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onDecryptEncryptedTextDelegateFinished");

    setModified();

    EncryptDecryptUndoCommandInfo info;
    info.m_encryptedText = encryptedText;
    info.m_decryptedText = decryptedText;
    info.m_passphrase = passphrase;
    info.m_cipher = cipher;
    info.m_hint = hint;
    info.m_keyLength = length;
    info.m_rememberForSession = rememberForSession;
    info.m_decryptPermanently = decryptPermanently;

    QVector<std::pair<QString, QString>> extraData;
    extraData << std::make_pair(
        QStringLiteral("decryptPermanently"),
        (decryptPermanently ? QStringLiteral("true")
                            : QStringLiteral("false")));

    auto * pCommand = new DecryptUndoCommand(
        info, m_decryptedTextManager, *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onDecryptEncryptedTextUndoRedoFinished,
            extraData));

    QObject::connect(
        pCommand, &DecryptUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate = qobject_cast<DecryptEncryptedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    if (decryptPermanently) {
        m_pendingConversionToNoteForSavingInLocalStorage = true;
        convertToNote();
    }
}

void NoteEditorPrivate::onDecryptEncryptedTextDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onDecryptEncryptedTextDelegateCancelled");

    auto * delegate = qobject_cast<DecryptEncryptedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onDecryptEncryptedTextDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onDecryptEncryptedTextDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<DecryptEncryptedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onDecryptEncryptedTextUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onDecryptEncryptedTextUndoRedoFinished: " << data);

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of encrypted text "
                       "decryption undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of encrypted text "
                           "decryption undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the encrypted text decryption"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool shouldConvertToNote = true;
    if (!extraData.isEmpty()) {
        const auto & pair = extraData[0];
        if (pair.second == QStringLiteral("false")) {
            shouldConvertToNote = false;
        }
    }

    if (shouldConvertToNote) {
        m_pendingConversionToNoteForSavingInLocalStorage = true;
        convertToNote();
    }
}

void NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateFinished()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onAddHyperlinkToSelectedTextDelegateFinished");

    auto * pCommand = new AddHyperlinkUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this,
            &NoteEditorPrivate::onAddHyperlinkToSelectedTextUndoRedoFinished));

    QObject::connect(
        pCommand, &AddHyperlinkUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate =
        qobject_cast<AddHyperlinkToSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onAddHyperlinkToSelectedTextDelegateCancelled");

    auto * delegate =
        qobject_cast<AddHyperlinkToSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateError(
    ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onAddHyperlinkToSelectedTextDelegateError");

    Q_EMIT notifyError(error);

    auto * delegate =
        qobject_cast<AddHyperlinkToSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onAddHyperlinkToSelectedTextUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onAddHyperlinkToSelectedTextUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "addition undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink "
                           "addition undo/redo from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't undo/redo the hyperlink addition"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onEditHyperlinkDelegateFinished()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::onEditHyperlinkDelegateFinished");

    setModified();

    auto * pCommand = new EditHyperlinkUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onEditHyperlinkUndoRedoFinished));

    QObject::connect(
        pCommand, &EditHyperlinkUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate = qobject_cast<EditHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onEditHyperlinkDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEditHyperlinkDelegateCancelled");

    auto * delegate = qobject_cast<EditHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEditHyperlinkDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEditHyperlinkDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<EditHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEditHyperlinkUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onEditHyperlinkUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "edit undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink edit "
                           "undo/redo from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't undo/redo the hyperlink edit"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRemoveHyperlinkDelegateFinished()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRemoveHyperlinkDelegateFinished");

    setModified();

    auto * pCommand = new RemoveHyperlinkUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onRemoveHyperlinkUndoRedoFinished));

    QObject::connect(
        pCommand, &RemoveHyperlinkUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    auto * delegate = qobject_cast<RemoveHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onRemoveHyperlinkDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onRemoveHyperlinkDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * delegate = qobject_cast<RemoveHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRemoveHyperlinkUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onRemoveHyperlinkUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "removal undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink "
                           "removal undo/redo from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't undo/redo the hyperlink removal"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onInsertHtmlDelegateFinished(
    QList<Resource> addedResources, QStringList resourceFileStoragePaths)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onInsertHtmlDelegateFinished: "
            << "num added resources = " << addedResources.size());

    setModified();

    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QNTRACE("note_editor", "Added resources: ");
        for (const auto & resource: qAsConst(addedResources)) {
            QNTRACE("note_editor", resource);
        }

        QNTRACE("note_editor", "Resource file storage paths: ");
        for (const auto & path: qAsConst(resourceFileStoragePaths)) {
            QNTRACE("note_editor", path);
        }
    }

    auto * pDelegate = qobject_cast<InsertHtmlDelegate *>(sender());
    if (Q_LIKELY(pDelegate)) {
        pDelegate->deleteLater();
    }

    pushInsertHtmlUndoCommand(addedResources, resourceFileStoragePaths);
    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onInsertHtmlDelegateError(ErrorString error)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onInsertHtmlDelegateError: " << error);

    Q_EMIT notifyError(error);

    auto * pDelegate = qobject_cast<InsertHtmlDelegate *>(sender());
    if (Q_LIKELY(pDelegate)) {
        pDelegate->deleteLater();
    }
}

void NoteEditorPrivate::onInsertHtmlUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onInsertHtmlUndoRedoFinished: " << data);

    Q_UNUSED(extraData);

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of html insertion "
                       "undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of html insertion "
                           "undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the html insertion "
                           "into the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onSourceCodeFormatUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSourceCodeFormatUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    auto resultMap = data.toMap();
    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of source code "
                       "formatting undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of source code "
                           "formatting undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the source code formatting"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onUndoCommandError(ErrorString error)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onUndoCommandError: " << error);
    Q_EMIT notifyError(error);
}

void NoteEditorPrivate::onSpellCheckerDictionaryEnabledOrDisabled(bool checked)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSpellCheckerDictionaryEnabledOrDisabled: "
            << "checked = " << (checked ? "true" : "false"));

    auto * pAction = qobject_cast<QAction *>(sender());
    if (Q_UNLIKELY(!pAction)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't change the enabled/disabled state of a spell "
                       "checker dictionary: internal error, can't cast "
                       "the slot invoker to QAction"));
        QNWARNING("note_editor", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't change the enabled/disabled state of a spell "
                       "checker dictionary: internal error, the spell checker "
                       "is not set up for the note editor"));
        QNWARNING("note_editor", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QString dictionaryName = pAction->text();
    dictionaryName.remove(QStringLiteral("&"));

    if (checked) {
        m_pSpellChecker->enableDictionary(dictionaryName);
    }
    else {
        m_pSpellChecker->disableDictionary(dictionaryName);
    }

    if (!m_spellCheckerEnabled) {
        QNDEBUG(
            "note_editor",
            "The spell checker is not enabled at "
                << "the moment, won't refresh it");
        return;
    }

    refreshMisSpelledWordsList();
    applySpellCheck();
}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void NoteEditorPrivate::onPageHtmlReceivedForPrinting(
    const QString & html,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onPageHtmlReceivedForPrinting: " << html);

    Q_UNUSED(extraData)
    m_htmlForPrinting = html;
    Q_EMIT htmlReadyForPrinting();
}
#endif

void NoteEditorPrivate::clearCurrentNoteInfo()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::clearCurrentNoteInfo");

    // Remove the no longer needed html file with the note editor page
    if (m_pNote) {
        QFileInfo noteEditorPageFileInfo(noteEditorPagePath());

        if (noteEditorPageFileInfo.exists() && noteEditorPageFileInfo.isFile())
        {
            Q_UNUSED(removeFile(noteEditorPageFileInfo.absoluteFilePath()))
        }
    }

    m_resourceInfo.clear();
    m_resourceFileStoragePathsByResourceLocalUid.clear();
    m_genericResourceImageFilePathsByResourceHash.clear();
    m_saveGenericResourceImageToFileRequestIds.clear();
    m_recognitionIndicesByResourceHash.clear();
    m_decryptedTextManager->clearNonRememberedForSessionEntries();

    m_lastSearchHighlightedText.resize(0);
    m_lastSearchHighlightedTextCaseSensitivity = false;

    m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile.clear();
    m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage.clear();

    m_noteWasNotFound = false;
    m_noteWasDeleted = false;

    m_pendingConversionToNote = false;
    m_pendingConversionToNoteForSavingInLocalStorage = false;

    m_pendingNoteSavingInLocalStorage = false;
    m_shouldRepeatSavingNoteInLocalStorage = false;

    m_pendingNoteImageResourceTemporaryFiles = false;

    m_lastInteractionTimestamp = -1;

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    m_webSocketServerPort = 0;
#else
    page()->setPluginFactory(nullptr);
    delete m_pPluginFactory;
    m_pPluginFactory = nullptr;
#endif

    for (auto & pair: m_prepareResourceForOpeningProgressDialogs) {
        pair.second->accept();
        pair.second->deleteLater();
    }
    m_prepareResourceForOpeningProgressDialogs.clear();
}

void NoteEditorPrivate::reloadCurrentNote()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::reloadCurrentNote");

    if (Q_UNLIKELY(m_noteLocalUid.isEmpty())) {
        QNWARNING(
            "note_editor",
            "Can't reload current note - no note is set "
                << "to the editor");
        return;
    }

    if (Q_UNLIKELY(!m_pNote || !m_pNotebook)) {
        QString noteLocalUid = m_noteLocalUid;
        m_noteLocalUid.clear();
        setCurrentNoteLocalUid(noteLocalUid);
        return;
    }

    Note note = *m_pNote;
    Notebook notebook = *m_pNotebook;
    clearCurrentNoteInfo();
    onFoundNoteAndNotebook(note, notebook);
}

void NoteEditorPrivate::clearPrepareResourceForOpeningProgressDialog(
    const QString & resourceLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::clearPrepareResourceForOpeningProgressDialog: "
            << "resource local uid = " << resourceLocalUid);

    auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalUid](const auto & pair) {
            return pair.first == resourceLocalUid;
        });

    if (Q_UNLIKELY(it == m_prepareResourceForOpeningProgressDialogs.end())) {
        QNDEBUG(
            "note_editor",
            "Haven't found QProgressDialog for this "
                << "resource");
        return;
    }

    it->second->accept();
    it->second->deleteLater();
    it->second = nullptr;

    m_prepareResourceForOpeningProgressDialogs.erase(it);
}

void NoteEditorPrivate::timerEvent(QTimerEvent * pEvent)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::timerEvent: "
            << (pEvent ? QString::number(pEvent->timerId())
                       : QStringLiteral("<null>")));

    if (Q_UNLIKELY(!pEvent)) {
        QNINFO("note_editor", "Detected null pointer to timer event");
        return;
    }

    if (pEvent->timerId() == m_pageToNoteContentPostponeTimerId) {
        if (m_contentChangedSinceWatchingStart) {
            QNTRACE(
                "note_editor",
                "Note editor page's content has been "
                    << "changed lately, the editing is most likely in progress "
                    << "now, postponing the conversion to ENML");
            m_contentChangedSinceWatchingStart = false;
            return;
        }

        ErrorString error;
        QNTRACE(
            "note_editor",
            "Looks like the note editing has stopped for "
                << "a while, will convert the note editor page's content to "
                   "ENML");
        bool res = htmlToNoteContent(error);
        if (!res) {
            Q_EMIT notifyError(error);
        }

        killTimer(m_pageToNoteContentPostponeTimerId);
        m_pageToNoteContentPostponeTimerId = 0;

        m_watchingForContentChange = false;
        m_contentChangedSinceWatchingStart = false;
    }
}

void NoteEditorPrivate::dragMoveEvent(QDragMoveEvent * pEvent)
{
    if (Q_UNLIKELY(!pEvent)) {
        QNINFO("note_editor", "Detected null pointer to drag move event");
        return;
    }

    const auto * pMimeData = pEvent->mimeData();
    if (Q_UNLIKELY(!pMimeData)) {
        QNWARNING(
            "note_editor",
            "Null pointer to mime data from drag move "
                << "event was detected");
        return;
    }

    auto urls = pMimeData->urls();
    if (urls.isEmpty()) {
        return;
    }

    pEvent->acceptProposedAction();
}

void NoteEditorPrivate::dropEvent(QDropEvent * pEvent)
{
    onDropEvent(pEvent);
}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void NoteEditorPrivate::getHtmlForPrinting()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::getHtmlForPrinting");

    GET_PAGE()

    page->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceivedForPrinting));
}
#endif // QUENTIER_USE_QT_WEB_ENGINE

void NoteEditorPrivate::onFoundResourceData(Resource resource)
{
    QString resourceLocalUid = resource.localUid();

    auto sit =
        m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile.find(
            resourceLocalUid);

    if (sit !=
        m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFoundResourceData: "
                << "resource local uid = " << resourceLocalUid);
        QNTRACE("note_editor", resource);

        Q_UNUSED(m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile
                     .erase(sit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        auto resources = m_pNote->resources();
        int resourceIndex = -1;
        for (int i = 0, size = resources.size(); i < size; ++i) {
            const Resource & currentResource = qAsConst(resources)[i];
            if (currentResource.localUid() == resourceLocalUid) {
                resourceIndex = i;
                break;
            }
        }

        if (Q_UNLIKELY(resourceIndex < 0)) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't save attachment data to a file: "
                           "the attachment to be saved was not found "
                           "within the note"));
            QNWARNING(
                "note_editor",
                errorDescription << ", resource local uid = "
                                 << resourceLocalUid);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QNTRACE("note_editor", "Updating the resource within the note");
        resources[resourceIndex] = resource;
        m_pNote->setResources(resources);
        Q_EMIT currentNoteChanged(*m_pNote);

        manualSaveResourceToFile(resource);
    }

    auto iit =
        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage.find(
            resourceLocalUid);
    if (iit !=
        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage.end()) {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFoundResourceData: "
                << "resource local uid = " << resourceLocalUid);
        QNTRACE("note_editor", resource);

        Rotation rotationDirection = iit.value();
        Q_UNUSED(m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage
                     .erase(iit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        if (Q_UNLIKELY(!resource.hasDataBody() && !resource.hasDataHash())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't rotate image attachment: the image "
                           "attachment has neither data nor data hash"));
            QNWARNING(
                "note_editor", errorDescription << ", resource: " << resource);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        auto resources = m_pNote->resources();
        int resourceIndex = -1;
        for (int i = 0, size = resources.size(); i < size; ++i) {
            const auto & currentResource = qAsConst(resources)[i];
            if (currentResource.localUid() == resourceLocalUid) {
                resourceIndex = i;
                break;
            }
        }

        if (Q_UNLIKELY(resourceIndex < 0)) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't rotate image attachment: the attachment to "
                           "be rotated was not found within the note"));
            QNWARNING(
                "note_editor",
                errorDescription << ", resource local uid = "
                                 << resourceLocalUid);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        resources[resourceIndex] = resource;
        m_pNote->setResources(resources);

        QByteArray dataHash =
            (resource.hasDataHash()
                 ? resource.dataHash()
                 : QCryptographicHash::hash(
                       resource.dataBody(), QCryptographicHash::Md5));
        rotateImageAttachment(dataHash, rotationDirection);
    }
}

void NoteEditorPrivate::onFailedToFindResourceData(
    QString resourceLocalUid, ErrorString errorDescription)
{
    auto sit =
        m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile.find(
            resourceLocalUid);
    if (sit !=
        m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFailedToFindResourceData: "
                << "resource local uid = " << resourceLocalUid);

        Q_UNUSED(m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile
                     .erase(sit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        ErrorString error(
            QT_TR_NOOP("Can't save attachment data to a file: the attachment "
                       "data was not found within the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(
            "note_editor",
            error << ", resource local uid = " << resourceLocalUid);
        Q_EMIT notifyError(error);
    }

    auto iit =
        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage.find(
            resourceLocalUid);

    if (iit !=
        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage.end()) {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFailedToFindResourceData: "
                << "resource local uid = " << resourceLocalUid);

        Q_UNUSED(m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage
                     .erase(iit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        ErrorString error(
            QT_TR_NOOP("Can't rotate image attachment: attachment "
                       "data was not found within the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(
            "note_editor",
            error << ", resource local uid = " << resourceLocalUid);
        Q_EMIT notifyError(error);
    }
}

void NoteEditorPrivate::onFailedToPutResourceDataInTemporaryFile(
    QString resourceLocalUid, QString noteLocalUid,
    ErrorString errorDescription)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onFailedToPutResourceDataInTemporaryFile: resource local uid "
               "= "
            << resourceLocalUid << ", note local uid = " << noteLocalUid
            << ", error description: " << errorDescription);

    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationProgress(
    double progress, QString noteLocalUid)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onNoteResourceTemporaryFilesPreparationProgress: progress = "
            << progress << ", note local uid = " << noteLocalUid);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationError(
    QString noteLocalUid, ErrorString errorDescription)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onNoteResourceTemporaryFilesPreparationError: note local uid "
               "= "
            << noteLocalUid << ", error description: " << errorDescription);

    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesReady(QString noteLocalUid)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onNoteResourceTemporaryFilesReady: note local uid = "
            << noteLocalUid);

    /**
     * All note's image resources (if any) were written to temporary files so
     * they can now be displayed within the note editor page. However, one more
     * trick is required for the case in which the note was updated i.e.
     * previous versions of this note's image resources have already been
     * displayed: even though the image files are updated with new data, the
     * web engine's cache doesn't know about this and the updated data from
     * image files is not reloaded, the old data is displayed. The workaround
     * is to create a symlink to each resource image file and use that instead
     * of the real path, this way web engine's undesired caching is avoided
     */

    m_pendingNoteImageResourceTemporaryFiles = false;

    auto resources = m_pNote->resources();
    QString imageResourceMimePrefix = QStringLiteral("image/");
    for (const auto & resource: qAsConst(resources)) {
        QNTRACE("note_editor", "Processing resource: " << resource);

        if (!resource.hasMime() ||
            !resource.mime().startsWith(imageResourceMimePrefix)) {
            QNTRACE(
                "note_editor",
                "Skipping the resource with inappropriate "
                    << "mime type: "
                    << (resource.hasMime() ? resource.mime()
                                           : QStringLiteral("<not set>")));
            continue;
        }

        if (Q_UNLIKELY(!resource.hasDataHash())) {
            QNTRACE("note_editor", "Skipping the resource without data hash");
            continue;
        }

        if (Q_UNLIKELY(!resource.hasDataSize())) {
            QNTRACE("note_editor", "Skipping the resource without data size");
            continue;
        }

        QString resourceLocalUid = resource.localUid();

        QString fileStoragePath = ResourceDataInTemporaryFileStorageManager::
                                      imageResourceFileStorageFolderPath() +
            QStringLiteral("/") + noteLocalUid + QStringLiteral("/") +
            resourceLocalUid + QStringLiteral(".dat");

        ErrorString errorDescription;
        QString linkFilePath = createSymlinkToImageResourceFile(
            fileStoragePath, resourceLocalUid, errorDescription);
        if (Q_UNLIKELY(linkFilePath.isEmpty())) {
            QNWARNING("note_editor", errorDescription);
            // Since the proper way has failed, use the improper one as a
            // fallback
            linkFilePath = fileStoragePath;
        }

        m_resourceFileStoragePathsByResourceLocalUid[resourceLocalUid] =
            linkFilePath;

        QString resourceDisplayName = resource.displayName();
        QString resourceDisplaySize = humanReadableSize(
            static_cast<quint64>(std::max(resource.dataSize(), qint32(0))));

        QSize resourceImageSize;
        if (resource.hasHeight() && resource.hasWidth()) {
            resourceImageSize.setHeight(resource.height());
            resourceImageSize.setWidth(resource.width());
        }

        m_resourceInfo.cacheResourceInfo(
            resource.dataHash(), resourceDisplayName, resourceDisplaySize,
            linkFilePath, resourceImageSize);
    }

    if (!m_pendingNotePageLoad && !m_pendingIndexHtmlWritingToFile) {
        provideSrcForResourceImgTags();
        highlightRecognizedImageAreas(
            m_lastSearchHighlightedText,
            m_lastSearchHighlightedTextCaseSensitivity);
    }
}

void NoteEditorPrivate::onOpenResourceInExternalEditorPreparationProgress(
    double progress, QString resourceLocalUid, QString noteLocalUid)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onOpenResourceInExternalEditorPreparationProgress: progress "
               "= "
            << progress << ", resource local uid = " << resourceLocalUid
            << ", note local uid = " << noteLocalUid);

    auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalUid](const auto & pair) {
            return pair.first == resourceLocalUid;
        });

    if (Q_UNLIKELY(it == m_prepareResourceForOpeningProgressDialogs.end())) {
        QNDEBUG(
            "note_editor",
            "Haven't found QProgressDialog for this "
                << "resource");
        return;
    }

    int normalizedProgress =
        static_cast<int>(std::floor(progress * 100.0 + 0.5));

    if (normalizedProgress > 100) {
        normalizedProgress = 100;
    }

    it->second->setValue(normalizedProgress);
}

void NoteEditorPrivate::onFailedToOpenResourceInExternalEditor(
    QString resourceLocalUid, QString noteLocalUid,
    ErrorString errorDescription)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onFailedToOpenResourceInExternalEditor: resource local uid = "
            << resourceLocalUid << ", note local uid = " << noteLocalUid
            << ", error description = " << errorDescription);

    clearPrepareResourceForOpeningProgressDialog(resourceLocalUid);
    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onOpenedResourceInExternalEditor(
    QString resourceLocalUid, QString noteLocalUid)
{
    if (!m_pNote || (m_pNote->localUid() != noteLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onOpenedResourceInExternalEditor: resource local uid = "
            << resourceLocalUid << ", note local uid = " << noteLocalUid);

    clearPrepareResourceForOpeningProgressDialog(resourceLocalUid);
}

void NoteEditorPrivate::init()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::init");

    CHECK_ACCOUNT(QT_TR_NOOP("Can't initialize the note editor"))

    QString accountName = m_pAccount->name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't initialize the note editor: account "
                       "name is empty"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString storagePath = accountPersistentStoragePath(*m_pAccount);
    if (Q_UNLIKELY(storagePath.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't initialize the note editor: account "
                       "persistent storage path is empty"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_noteEditorPageFolderPath = storagePath;
    m_noteEditorPageFolderPath += QStringLiteral("/NoteEditorPage");

    m_genericResourceImageFileStoragePath =
        m_noteEditorPageFolderPath + QStringLiteral("/genericResourceImages");

    setupFileIO();
    setupNoteEditorPage();
    setAcceptDrops(true);

    QString initialHtml = initialPageHtml();
    writeNotePageFile(initialHtml);
}

void NoteEditorPrivate::onNoteSavedToLocalStorage(QString noteLocalUid)
{
    if (!m_pendingNoteSavingInLocalStorage || !m_pNote ||
        (m_pNote->localUid() != noteLocalUid))
    {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteSavedToLocalStorage: "
            << "note local uid = " << noteLocalUid);

    m_needSavingNoteInLocalStorage = false;
    m_pendingNoteSavingInLocalStorage = false;

    // NOTE: although saving the note to local storage might not be due to
    // an explicit user's interaction, it is still considered a kind of thing
    // which should bump the last interaction timestamp
    updateLastInteractionTimestamp();

    if (m_shouldRepeatSavingNoteInLocalStorage) {
        m_shouldRepeatSavingNoteInLocalStorage = false;
        saveNoteToLocalStorage();
        return;
    }

    Q_EMIT noteSavedToLocalStorage(noteLocalUid);
}

void NoteEditorPrivate::onFailedToSaveNoteToLocalStorage(
    QString noteLocalUid, ErrorString errorDescription)
{
    if (!m_pendingNoteSavingInLocalStorage || !m_pNote ||
        (m_pNote->localUid() != noteLocalUid))
    {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onFailedToSaveNoteToLocalStorage: note local uid = "
            << noteLocalUid << ", error description: " << errorDescription);

    m_pendingNoteSavingInLocalStorage = false;
    m_shouldRepeatSavingNoteInLocalStorage = false;

    Q_EMIT failedToSaveNoteToLocalStorage(errorDescription, noteLocalUid);
}

void NoteEditorPrivate::onFoundNoteAndNotebook(Note note, Notebook notebook)
{
    if (note.localUid() != m_noteLocalUid) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFoundNoteAndNotebook: note = "
            << note << "\nNotebook = " << notebook);

    m_pNotebook.reset(new Notebook(notebook));
    m_pNote.reset(new Note(note));

    rebuildRecognitionIndicesCache();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    if (m_webSocketServerPort == 0) {
        setupWebSocketServer();
    }

    if (!m_setUpJavaScriptObjects) {
        setupJavaScriptObjects();
    }
#else
    auto * pNoteEditorPage = qobject_cast<NoteEditorPage *>(page());
    if (Q_LIKELY(pNoteEditorPage)) {
        bool missingPluginFactory = !m_pPluginFactory;
        if (missingPluginFactory) {
            m_pPluginFactory =
                new NoteEditorPluginFactory(*this, pNoteEditorPage);
        }

        m_pPluginFactory->setNote(*m_pNote);

        if (missingPluginFactory) {
            QNDEBUG(
                "note_editor",
                "Setting note editor plugin factory to "
                    << "the page");
            pNoteEditorPage->setPluginFactory(m_pPluginFactory);
        }
    }
#endif

    Q_EMIT noteAndNotebookFoundInLocalStorage(*m_pNote, *m_pNotebook);

    Q_EMIT currentNoteChanged(*m_pNote);
    noteToEditorContent();
    QNTRACE("note_editor", "Done setting the current note and notebook");
}

void NoteEditorPrivate::onFailedToFindNoteOrNotebook(
    QString noteLocalUid, ErrorString errorDescription)
{
    if (noteLocalUid != m_noteLocalUid) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorPrivate::onFailedToFindNoteOrNotebook: "
            << "note local uid = " << noteLocalUid
            << ", error description: " << errorDescription);

    m_noteLocalUid.clear();
    m_noteWasNotFound = true;
    Q_EMIT noteNotFound(noteLocalUid);

    clearEditorContent(BlankPageKind::NoteNotFound);
}

void NoteEditorPrivate::onNoteUpdated(Note note)
{
    if (note.localUid() != m_noteLocalUid) {
        return;
    }

    QNDEBUG("note_editor", "NoteEditorPrivate::onNoteUpdated: " << note);

    if (Q_UNLIKELY(!m_pNote)) {
        if (m_pNotebook) {
            QNDEBUG(
                "note_editor",
                "Current note is unexpectedly empty on note "
                    << "update, acting as if the note has just been found");
            Notebook notebook = *m_pNotebook;
            onFoundNoteAndNotebook(note, notebook);
        }
        else {
            QNWARNING(
                "note_editor",
                "Can't handle the update of note: "
                    << "note editor contains neither note nor notebook");
            // Trying to recover through re-requesting note and notebook
            // from the local storage
            m_noteLocalUid.clear();
            setCurrentNoteLocalUid(note.localUid());
        }

        return;
    }

    if (Q_UNLIKELY(!note.hasNotebookLocalUid())) {
        QNWARNING(
            "note_editor",
            "Can't handle the update of a note: "
                << "the updated note has no notebook local uid: " << note);
        return;
    }

    if (Q_UNLIKELY(!m_pNotebook) ||
        (m_pNotebook->localUid() != note.notebookLocalUid()))
    {
        QNDEBUG(
            "note_editor",
            "Note's notebook has changed: new notebook "
                << "local uid = " << note.notebookLocalUid());

        // Re-requesting both note and notebook from
        // NoteEditorLocalStorageBroker
        QString noteLocalUid = m_noteLocalUid;
        clearCurrentNoteInfo();
        Q_EMIT findNoteAndNotebook(noteLocalUid);
        return;
    }

    bool noteChanged = false;
    noteChanged = noteChanged || (m_pNote->hasContent() != note.hasContent());
    noteChanged =
        noteChanged || (m_pNote->hasResources() != note.hasResources());

    if (!noteChanged && m_pNote->hasResources() && note.hasResources()) {
        auto currentResources = m_pNote->resources();
        auto updatedResources = note.resources();

        int size = currentResources.size();
        noteChanged = (size != updatedResources.size());
        if (!noteChanged) {
            // NOTE: clearing out data bodies before comparing resources
            // to speed up the comparison
            for (int i = 0; i < size; ++i) {
                Resource currentResource = qAsConst(currentResources).at(i);
                currentResource.setDataBody(QByteArray());
                currentResource.setAlternateDataBody(QByteArray());

                Resource updatedResource = qAsConst(updatedResources).at(i);
                updatedResource.setDataBody(QByteArray());
                updatedResource.setAlternateDataBody(QByteArray());

                if (currentResource != updatedResource) {
                    noteChanged = true;
                    break;
                }
            }
        }
    }

    if (!noteChanged && m_pNote->hasContent() && note.hasContent()) {
        noteChanged = (m_pNote->content() != note.content());
    }

    if (!noteChanged) {
        QNDEBUG(
            "note_editor",
            "Haven't found the updates within the note "
                << "which would be sufficient enough to reload the note "
                   "editor");
        *m_pNote = note;
        return;
    }

    // FIXME: if the note was modified, need to let the user choose what to do -
    // either continue to edit the note or reload it

    QNDEBUG(
        "note_editor",
        "Note has changed substantially, need to reload "
            << "the editor");
    *m_pNote = note;
    reloadCurrentNote();
}

void NoteEditorPrivate::onNotebookUpdated(Notebook notebook)
{
    if (!m_pNotebook || (m_pNotebook->localUid() != notebook.localUid())) {
        return;
    }

    QNDEBUG("note_editor", "NoteEditorPrivate::onNotebookUpdated");

    bool restrictionsChanged = false;
    if (m_pNotebook->hasRestrictions() != notebook.hasRestrictions()) {
        restrictionsChanged = true;
    }
    else if (m_pNotebook->hasRestrictions() && notebook.hasRestrictions()) {
        const auto & previousRestrictions = m_pNotebook->restrictions();
        bool previousCanUpdateNote =
            (!previousRestrictions.noUpdateNotes.isSet() ||
             !previousRestrictions.noUpdateNotes.ref());

        const auto & newRestrictions = notebook.restrictions();
        bool newCanUpdateNote =
            (!newRestrictions.noUpdateNotes.isSet() ||
             !newRestrictions.noUpdateNotes.ref());

        restrictionsChanged = (previousCanUpdateNote != newCanUpdateNote);
    }

    *m_pNotebook = notebook;

    if (!restrictionsChanged) {
        QNDEBUG("note_editor", "Detected no change of notebook restrictions");
        return;
    }

    if (Q_UNLIKELY(!m_pNote)) {
        QNWARNING("note_editor", "Note editor has notebook but no note");
        return;
    }

    bool canUpdateNote = true;
    if (m_pNotebook->hasRestrictions()) {
        const auto & restrictions = notebook.restrictions();
        canUpdateNote =
            (!restrictions.noUpdateNotes.isSet() ||
             !restrictions.noUpdateNotes.ref());
    }

    if (!canUpdateNote && m_isPageEditable) {
        QNDEBUG("note_editor", "Note has become non-editable");
        setPageEditable(false);
        return;
    }

    if (canUpdateNote && !m_isPageEditable) {
        if (m_pNote->hasActive() && !m_pNote->active()) {
            QNDEBUG(
                "note_editor",
                "Notebook no longer restricts the update of "
                    << "a note but the note is not active");
            return;
        }

        if (m_pNote->isInkNote()) {
            QNDEBUG(
                "note_editor",
                "Notebook no longer restricts the update of "
                    << "a note but the note is an ink note");
            return;
        }

        QNDEBUG("note_editor", "Note has become editable");
        setPageEditable(true);
        return;
    }
}

void NoteEditorPrivate::onNoteDeleted(QString noteLocalUid)
{
    if (m_noteLocalUid != noteLocalUid) {
        return;
    }

    QNDEBUG(
        "note_editor", "NoteEditorPrivate::onNoteDeleted: " << noteLocalUid);

    Q_EMIT noteDeleted(m_noteLocalUid);

    // FIXME: need to display the dedicated note editor page about the fact that
    // the note has been deleted
    // FIXME: if the note editor has been marked as modified, need to offer the
    // option to the user to save their edits as a new note

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);
    m_noteLocalUid = QString();
    clearCurrentNoteInfo();
    m_noteWasDeleted = true;
    clearEditorContent(BlankPageKind::NoteDeleted);
}

void NoteEditorPrivate::onNotebookDeleted(QString notebookLocalUid)
{
    if (!m_pNotebook || (m_pNotebook->localUid() != notebookLocalUid)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNotebookDeleted: " << notebookLocalUid);

    Q_EMIT noteDeleted(m_noteLocalUid);

    // FIXME: need to display the dedicated note editor page about the fact that
    // the note has been deleted
    // FIXME: if the note editor has been marked as modified, need to offer the
    // option to the user to save their edits as a new note

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);
    m_noteLocalUid = QString();
    clearCurrentNoteInfo();
    m_noteWasDeleted = true;
    clearEditorContent(BlankPageKind::NoteDeleted);
}

void NoteEditorPrivate::handleHyperlinkClicked(const QUrl & url)
{
    QString urlString = url.toString();

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::handleHyperlinkClicked: " << urlString);

    if (urlString.startsWith(QStringLiteral("evernote:///"))) {
        handleInAppLinkClicked(urlString);
        return;
    }

    QDesktopServices::openUrl(url);
}

void NoteEditorPrivate::handleInAppLinkClicked(const QString & urlString)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::handleInAppLinkClicked: " << urlString);

    QString userId, shardId, noteGuid;
    ErrorString errorDescription;
    bool res =
        parseInAppLink(urlString, userId, shardId, noteGuid, errorDescription);

    if (!res) {
        QNWARNING("note_editor", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QNTRACE(
        "note_editor",
        "Parsed in-app note link: user id = " << userId
                                              << ", shard id = " << shardId
                                              << ", note guid = " << noteGuid);

    Q_EMIT inAppNoteLinkClicked(userId, shardId, noteGuid);
}

bool NoteEditorPrivate::parseInAppLink(
    const QString & urlString, QString & userId, QString & shardId,
    QString & noteGuid, ErrorString & errorDescription) const
{
    userId.resize(0);
    shardId.resize(0);
    noteGuid.resize(0);
    errorDescription.clear();

    QRegExp regex(
        QStringLiteral("evernote:///view/([^/]+)/([^/]+)/([^/]+)(/.*)?"));

    int pos = regex.indexIn(urlString);
    if (pos < 0) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't process the in-app note link: "
                       "failed to parse the note guid from the link"));
        errorDescription.details() = urlString;
        return false;
    }

    QStringList capturedTexts = regex.capturedTexts();
    if (capturedTexts.size() != 5) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't process the in-app note link: "
                       "wrong number of captured texts"));
        errorDescription.details() = urlString;

        if (!capturedTexts.isEmpty()) {
            errorDescription.details() += QStringLiteral("; decoded: ") +
                capturedTexts.join(QStringLiteral(", "));
        }

        return false;
    }

    userId = capturedTexts.at(1);
    shardId = capturedTexts.at(2);
    noteGuid = capturedTexts.at(3);
    return true;
}

bool NoteEditorPrivate::checkNoteSize(
    const QString & newNoteContent, ErrorString & errorDescription) const
{
    QNDEBUG("note_editor", "NoteEditorPrivate::checkNoteSize");

    if (Q_UNLIKELY(!m_pNote)) {
        errorDescription.setBase(
            QT_TR_NOOP("Internal error: can't check the note size on note "
                       "editor update: no note is set to the editor"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    qint64 noteSize = noteResourcesSize();
    noteSize += newNoteContent.size();

    QNTRACE(
        "note_editor",
        "New note content size = " << newNoteContent.size()
                                   << ", total note size = " << noteSize);

    if (m_pNote->hasNoteLimits()) {
        const qevercloud::NoteLimits & noteLimits = m_pNote->noteLimits();
        QNTRACE(
            "note_editor",
            "Note has its own limits, will use them to "
                << "check the note size: " << noteLimits);

        if (noteLimits.noteSizeMax.isSet() &&
            (Q_UNLIKELY(noteLimits.noteSizeMax.ref() < noteSize)))
        {
            errorDescription.setBase(
                QT_TR_NOOP("Note size (text + resources) "
                           "exceeds the allowed maximum"));
            errorDescription.details() = humanReadableSize(
                static_cast<quint64>(noteLimits.noteSizeMax.ref()));
            QNINFO("note_editor", errorDescription);
            return false;
        }
    }
    else {
        QNTRACE(
            "note_editor",
            "Note has no its own limits, will use "
                << "the account-wise limits to check the note size");

        if (Q_UNLIKELY(!m_pAccount)) {
            errorDescription.setBase(
                QT_TR_NOOP("Internal error: can't check the note size on note "
                           "editor update: no account info is set to "
                           "the editor"));
            QNWARNING("note_editor", errorDescription);
            return false;
        }

        if (Q_UNLIKELY(noteSize > m_pAccount->noteSizeMax())) {
            errorDescription.setBase(
                QT_TR_NOOP("Note size (text + resources) "
                           "exceeds the allowed maximum"));
            errorDescription.details() = humanReadableSize(
                static_cast<quint64>(m_pAccount->noteSizeMax()));
            QNINFO("note_editor", errorDescription);
            return false;
        }
    }

    return true;
}

void NoteEditorPrivate::pushNoteContentEditUndoCommand()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::pushNoteTextEditUndoCommand");

    QUENTIER_CHECK_PTR(
        "note_editor", m_pUndoStack,
        QStringLiteral("Undo stack for note editor wasn't initialized"));

    if (Q_UNLIKELY(!m_pNote)) {
        QNINFO(
            "note_editor",
            "Ignoring the content changed signal as the note "
                << "pointer is null");
        return;
    }

    QList<Resource> resources;
    if (m_pNote->hasResources()) {
        resources = m_pNote->resources();
    }

    auto * pCommand = new NoteEditorContentEditUndoCommand(*this, resources);
    QObject::connect(
        pCommand, &NoteEditorContentEditUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::pushTableActionUndoCommand(
    const QString & name, NoteEditorPage::Callback callback)
{
    auto * pCommand = new TableActionUndoCommand(*this, name, callback);
    QObject::connect(
        pCommand, &TableActionUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::pushInsertHtmlUndoCommand(
    const QList<Resource> & addedResources,
    const QStringList & resourceFileStoragePaths)
{
    auto * pCommand = new InsertHtmlUndoCommand(
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onInsertHtmlUndoRedoFinished),
        *this, m_resourceFileStoragePathsByResourceLocalUid, m_resourceInfo,
        addedResources, resourceFileStoragePaths);

    QObject::connect(
        pCommand, &InsertHtmlUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::onManagedPageActionFinished(
    const QVariant & result,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onManagedPageActionFinished: " << result);
    Q_UNUSED(extraData)

    auto resultMap = result.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of managed page "
                       "action execution attempt"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        QString errorMessage;
        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (errorIt != resultMap.end()) {
            errorMessage = errorIt.value().toString();
        }

        ErrorString error(QT_TR_NOOP("Can't execute the page action"));
        error.details() = errorMessage;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    pushNoteContentEditUndoCommand();
    updateJavaScriptBindings();
    convertToNote();
}

void NoteEditorPrivate::updateJavaScriptBindings()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::updateJavaScriptBindings");

    updateColResizableTableBindings();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    provideSrcAndOnClickScriptForImgEnCryptTags();
    setupGenericResourceImages();
#endif

    if (m_spellCheckerEnabled) {
        applySpellCheck();
    }

    GET_PAGE()
    page->executeJavaScript(m_setupEnToDoTagsJs);
}

void NoteEditorPrivate::changeFontSize(const bool increase)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::changeFontSize: increase = "
            << (increase ? "true" : "false"));

    int fontSize = m_font.pointSize();
    if (fontSize < 0) {
        QNTRACE(
            "note_editor",
            "Font size is negative which most likely means "
                << "the font is not set yet, nothing to do. "
                << "Current font: " << m_font);
        return;
    }

    QFontDatabase fontDatabase;
    QList<int> fontSizes =
        fontDatabase.pointSizes(m_font.family(), m_font.styleName());
    if (fontSizes.isEmpty()) {
        QNTRACE(
            "note_editor",
            "Coulnd't find point sizes for font family "
                << m_font.family() << ", will use standard sizes instead");
        fontSizes = fontDatabase.standardSizes();
    }

    int fontSizeIndex = fontSizes.indexOf(fontSize);
    if (fontSizeIndex < 0) {
        QNTRACE(
            "note_editor",
            "Couldn't find font size "
                << fontSize << " within the available sizes, will take "
                << "the closest one instead");
        const int numFontSizes = fontSizes.size();
        int currentSmallestDiscrepancy = 1e5;
        int currentClosestIndex = -1;
        for (int i = 0; i < numFontSizes; ++i) {
            int value = fontSizes[i];

            int discrepancy = abs(value - fontSize);
            if (currentSmallestDiscrepancy > discrepancy) {
                currentSmallestDiscrepancy = discrepancy;
                currentClosestIndex = i;
                QNTRACE(
                    "note_editor",
                    "Updated current closest index to "
                        << i << ": font size = " << value);
            }
        }

        if (currentClosestIndex >= 0) {
            fontSizeIndex = currentClosestIndex;
        }
    }

    if (fontSizeIndex >= 0) {
        if (increase && (fontSizeIndex < (fontSizes.size() - 1))) {
            fontSize = fontSizes.at(fontSizeIndex + 1);
        }
        else if (!increase && (fontSizeIndex != 0)) {
            fontSize = fontSizes.at(fontSizeIndex - 1);
        }
        else {
            QNTRACE(
                "note_editor",
                "Can't " << (increase ? "increase" : "decrease")
                         << " the font size: hit the boundary of available "
                            "font sizes");
            return;
        }
    }
    else {
        QNTRACE(
            "note_editor",
            "Wasn't able to find even the closest font size "
                << "within the available ones, will simply "
                << (increase ? "increase" : "decrease")
                << " the given font size by 1 pt and see what happens");
        if (increase) {
            ++fontSize;
        }
        else {
            --fontSize;
            if (!fontSize) {
                fontSize = 1;
            }
        }
    }

    setFontHeight(fontSize);
}

void NoteEditorPrivate::changeIndentation(const bool increase)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::changeIndentation: increase = "
            << (increase ? "true" : "false"));

    execJavascriptCommand(
        increase ? QStringLiteral("indent") : QStringLiteral("outdent"));

    setModified();
}

void NoteEditorPrivate::findText(
    const QString & textToFind, const bool matchCase, const bool searchBackward,
    NoteEditorPage::Callback callback) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::findText: "
            << textToFind << "; match case = " << (matchCase ? "true" : "false")
            << ", search backward = " << (searchBackward ? "true" : "false"));

    GET_PAGE()

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    QString escapedTextToFind = textToFind;
    escapeStringForJavaScript(escapedTextToFind);

    // The order of used parameters to window.find: text to find, match case
    // (bool), search backwards (bool), wrap the search around (bool)
    QString javascript = QStringLiteral("window.find('") + escapedTextToFind +
        QStringLiteral("', ") +
        (matchCase ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral(", ") +
        (searchBackward ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral(", true);");

    page->executeJavaScript(javascript, callback);
#else
    WebPage::FindFlags flags;
    flags |= QWebPage::FindWrapsAroundDocument;

    if (matchCase) {
        flags |= WebPage::FindCaseSensitively;
    }

    if (searchBackward) {
        flags |= WebPage::FindBackward;
    }

    bool res = page->findText(textToFind, flags);
    if (callback != 0) {
        callback(QVariant(res));
    }
#endif

    setSearchHighlight(textToFind, matchCase);
}

bool NoteEditorPrivate::searchHighlightEnabled() const
{
    return !m_lastSearchHighlightedText.isEmpty();
}

void NoteEditorPrivate::setSearchHighlight(
    const QString & textToFind, const bool matchCase, const bool force) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setSearchHighlight: "
            << textToFind << "; match case = " << (matchCase ? "true" : "false")
            << "; force = " << (force ? "true" : "false"));

    if (!force &&
        (textToFind.compare(
             m_lastSearchHighlightedText,
             (matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive)) == 0) &&
        (m_lastSearchHighlightedTextCaseSensitivity == matchCase))
    {
        QNTRACE(
            "note_editor",
            "The text to find matches the one highlighted "
                << "the last time as well as its case sensitivity");
        return;
    }

    m_lastSearchHighlightedText = textToFind;
    m_lastSearchHighlightedTextCaseSensitivity = matchCase;

    QString escapedTextToFind = textToFind;
    escapeStringForJavaScript(escapedTextToFind);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("findReplaceManager.setSearchHighlight('") +
        escapedTextToFind + QStringLiteral("', ") +
        (matchCase ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral(");"));

    highlightRecognizedImageAreas(textToFind, matchCase);
}

void NoteEditorPrivate::highlightRecognizedImageAreas(
    const QString & textToFind, const bool matchCase) const
{
    QNDEBUG("note_editor", "NoteEditorPrivate::highlightRecognizedImageAreas");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("imageAreasHilitor.clearImageHilitors();"));

    if (m_lastSearchHighlightedText.isEmpty()) {
        QNTRACE("note_editor", "Last search highlighted text is empty");
        return;
    }

    QString escapedTextToFind = m_lastSearchHighlightedText;
    escapeStringForJavaScript(escapedTextToFind);

    if (escapedTextToFind.isEmpty()) {
        QNTRACE("note_editor", "Escaped search highlighted text is empty");
        return;
    }

    for (auto it: qevercloud::toRange(m_recognitionIndicesByResourceHash)) {
        const QByteArray & resourceHash = it.key();
        const ResourceRecognitionIndices & recoIndices = it.value();
        QNTRACE(
            "note_editor",
            "Processing recognition data for resource hash "
                << resourceHash.toHex());

        auto recoIndexItems = recoIndices.items();
        const int numIndexItems = recoIndexItems.size();
        for (int j = 0; j < numIndexItems; ++j) {
            const auto & recoIndexItem = recoIndexItems[j];
            auto textItems = recoIndexItem.textItems();
            const int numTextItems = textItems.size();

            bool matchFound = false;
            for (int k = 0; k < numTextItems; ++k) {
                const auto & textItem = textItems[k];
                if (textItem.m_text.contains(
                        textToFind,
                        (matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive)))
                {
                    QNTRACE(
                        "note_editor",
                        "Found text item matching with "
                            << "the text to find: " << textItem.m_text);
                    matchFound = true;
                }
            }

            if (matchFound) {
                page->executeJavaScript(
                    QStringLiteral("imageAreasHilitor.hiliteImageArea('") +
                    QString::fromLocal8Bit(resourceHash.toHex()) +
                    QStringLiteral("', ") + QString::number(recoIndexItem.x()) +
                    QStringLiteral(", ") + QString::number(recoIndexItem.y()) +
                    QStringLiteral(", ") + QString::number(recoIndexItem.h()) +
                    QStringLiteral(", ") + QString::number(recoIndexItem.w()) +
                    QStringLiteral(");"));
            }
        }
    }
}

void NoteEditorPrivate::clearEditorContent(
    const BlankPageKind kind, const ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::clearEditorContent: blank page "
            << "kind = " << kind
            << ", error description = " << errorDescription);

    if (m_pageToNoteContentPostponeTimerId != 0) {
        killTimer(m_pageToNoteContentPostponeTimerId);
        m_pageToNoteContentPostponeTimerId = 0;
    }

    m_watchingForContentChange = false;
    m_contentChangedSinceWatchingStart = false;

    m_needConversionToNote = false;
    m_needSavingNoteInLocalStorage = false;

    m_contextMenuSequenceNumber = 1;
    m_lastContextMenuEventGlobalPos = QPoint();
    m_lastContextMenuEventPagePos = QPoint();

    m_lastFreeEnToDoIdNumber = 1;
    m_lastFreeHyperlinkIdNumber = 1;
    m_lastFreeEnCryptIdNumber = 1;
    m_lastFreeEnDecryptedIdNumber = 1;

    m_lastSearchHighlightedText.resize(0);
    m_lastSearchHighlightedTextCaseSensitivity = false;

    QString blankPageHtml;
    switch (kind) {
    case BlankPageKind::NoteNotFound:
        blankPageHtml = noteNotFoundPageHtml();
        break;
    case BlankPageKind::NoteDeleted:
        blankPageHtml = noteDeletedPageHtml();
        break;
    case BlankPageKind::NoteLoading:
        blankPageHtml = noteLoadingPageHtml();
        break;
    case BlankPageKind::InternalError:
        blankPageHtml =
            composeBlankPageHtml(errorDescription.localizedString());
        break;
    default:
        blankPageHtml = initialPageHtml();
        break;
    }

    writeNotePageFile(blankPageHtml);
}

void NoteEditorPrivate::noteToEditorContent()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::noteToEditorContent");

    if (!m_pNote) {
        QNDEBUG("note_editor", "No note has been set yet");
        clearEditorContent();
        return;
    }

    if (m_pNote->isInkNote()) {
        inkNoteToEditorContent();
        return;
    }

    QString noteContent;
    if (m_pNote->hasContent()) {
        noteContent = m_pNote->content();
    }
    else {
        QNDEBUG(
            "note_editor",
            "Note without content was inserted into "
                << "the NoteEditor, setting up the empty note content");
        noteContent = QStringLiteral("<en-note><div></div></en-note>");
    }

    m_htmlCachedMemory.resize(0);

    ErrorString error;
    ENMLConverter::NoteContentToHtmlExtraData extraData;
    bool res = m_enmlConverter.noteContentToHtml(
        noteContent, m_htmlCachedMemory, error, *m_decryptedTextManager,
        extraData);

    if (!res) {
        ErrorString error(QT_TR_NOOP("Can't convert note's content to HTML"));
        error.details() = m_errorCachedMemory;
        QNWARNING("note_editor", error);
        clearEditorContent(BlankPageKind::InternalError, error);
        Q_EMIT notifyError(error);
        return;
    }

    m_lastFreeEnToDoIdNumber = extraData.m_numEnToDoNodes + 1;
    m_lastFreeHyperlinkIdNumber = extraData.m_numHyperlinkNodes + 1;
    m_lastFreeEnCryptIdNumber = extraData.m_numEnCryptNodes + 1;
    m_lastFreeEnDecryptedIdNumber = extraData.m_numEnDecryptedNodes + 1;

    int bodyTagIndex = m_htmlCachedMemory.indexOf(QStringLiteral("<body"));
    if (bodyTagIndex < 0) {
        ErrorString error(
            QT_TR_NOOP("Can't find <body> tag in the result of note "
                       "to HTML conversion"));
        QNWARNING(
            "note_editor",
            error << ", note content: " << m_pNote->content()
                  << ", html: " << m_htmlCachedMemory);
        clearEditorContent(BlankPageKind::InternalError, error);
        Q_EMIT notifyError(error);
        return;
    }

    QString pagePrefix = noteEditorPagePrefix();
    m_htmlCachedMemory.replace(0, bodyTagIndex, pagePrefix);

    int bodyClosingTagIndex =
        m_htmlCachedMemory.indexOf(QStringLiteral("</body>"));
    if (bodyClosingTagIndex < 0) {
        error.setBase(
            QT_TR_NOOP("Can't find </body> tag in the result of note "
                       "to HTML conversion"));
        QNWARNING(
            "note_editor",
            error << ", note content: " << m_pNote->content()
                  << ", html: " << m_htmlCachedMemory);
        clearEditorContent(BlankPageKind::InternalError, error);
        Q_EMIT notifyError(error);
        return;
    }

    m_htmlCachedMemory.insert(
        bodyClosingTagIndex + 7, QStringLiteral("</html>"));

    // Webkit-specific fix
    m_htmlCachedMemory.replace(
        QStringLiteral("<br></br>"), QStringLiteral("</br>"));

    QNTRACE("note_editor", "Note page HTML: " << m_htmlCachedMemory);
    writeNotePageFile(m_htmlCachedMemory);
}

void NoteEditorPrivate::updateColResizableTableBindings()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::updateColResizableTableBindings");

    bool readOnly = !isPageEditable();

    QString javascript;
    if (readOnly) {
        javascript =
            QStringLiteral("tableManager.disableColumnHandles(\"table\");");
    }
    else {
        javascript =
            QStringLiteral("tableManager.updateColumnHandles(\"table\");");
    }

    GET_PAGE()
    page->executeJavaScript(javascript);
}

void NoteEditorPrivate::inkNoteToEditorContent()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::inkNoteToEditorContent");

    m_lastFreeEnToDoIdNumber = 1;
    m_lastFreeHyperlinkIdNumber = 1;
    m_lastFreeEnCryptIdNumber = 1;
    m_lastFreeEnDecryptedIdNumber = 1;

    bool problemDetected = false;
    QList<Resource> resources = m_pNote->resources();
    const int numResources = resources.size();

    QString inkNoteHtml = noteEditorPagePrefix();
    inkNoteHtml += QStringLiteral("<body>");

    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = qAsConst(resources).at(i);

        if (!resource.hasGuid()) {
            QNWARNING(
                "note_editor",
                "Detected ink note which has at least one "
                    << "resource without guid: note = " << *m_pNote
                    << "\nResource: " << resource);
            problemDetected = true;
            break;
        }

        if (!resource.hasDataHash()) {
            QNWARNING(
                "note_editor",
                "Detected ink note which has at least one "
                    << "resource without data hash: note = " << *m_pNote
                    << "\nResource: " << resource);
            problemDetected = true;
            break;
        }

        QFileInfo inkNoteImageFileInfo(
            m_noteEditorPageFolderPath + QStringLiteral("/inkNoteImages/") +
            resource.guid() + QStringLiteral(".png"));

        if (!inkNoteImageFileInfo.exists() || !inkNoteImageFileInfo.isFile() ||
            !inkNoteImageFileInfo.isReadable())
        {
            QNWARNING(
                "note_editor",
                "Detected broken or nonexistent ink note "
                    << "image file, check file at path "
                    << inkNoteImageFileInfo.absoluteFilePath());
            problemDetected = true;
            break;
        }

        QString inkNoteImageFilePath = inkNoteImageFileInfo.absoluteFilePath();
        escapeStringForJavaScript(inkNoteImageFilePath);
        if (Q_UNLIKELY(inkNoteImageFilePath.isEmpty())) {
            QNWARNING(
                "note_editor",
                "Unable to escape the ink note image "
                    << "file path: "
                    << inkNoteImageFileInfo.absoluteFilePath());
            problemDetected = true;
            break;
        }

        inkNoteHtml += QStringLiteral("<img src=\"");
        inkNoteHtml += inkNoteImageFilePath;
        inkNoteHtml += QStringLiteral("\" ");

        if (resource.hasHeight()) {
            inkNoteHtml += QStringLiteral("height=");
            inkNoteHtml += QString::number(resource.height());
            inkNoteHtml += QStringLiteral(" ");
        }

        if (resource.hasWidth()) {
            inkNoteHtml += QStringLiteral("width=");
            inkNoteHtml += QString::number(resource.width());
            inkNoteHtml += QStringLiteral(" ");
        }

        inkNoteHtml += QStringLiteral("/>");
    }

    if (problemDetected) {
        inkNoteHtml = noteEditorPagePrefix();
        inkNoteHtml += QStringLiteral("<body><div>");
        inkNoteHtml +=
            tr("The read-only ink note image should have been present "
               "here but something went wrong so the image is not accessible");
        inkNoteHtml += QStringLiteral("</div></body></html>");
    }

    QNTRACE("note_editor", "Ink note html: " << inkNoteHtml);
    writeNotePageFile(inkNoteHtml);
}

bool NoteEditorPrivate::htmlToNoteContent(ErrorString & errorDescription)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::htmlToNoteContent");

    if (!m_pNote) {
        errorDescription.setBase(QT_TR_NOOP("No note was set to note editor"));
        QNWARNING("note_editor", errorDescription);
        Q_EMIT cantConvertToNote(errorDescription);
        return false;
    }

    if (m_pNote->hasActive() && !m_pNote->active()) {
        errorDescription.setBase(
            QT_TR_NOOP("Current note is marked as read-only, "
                       "the changes won't be saved"));

        QNINFO(
            "note_editor",
            errorDescription
                << ", note: local uid = " << m_pNote->localUid() << ", guid = "
                << (m_pNote->hasGuid() ? m_pNote->guid()
                                       : QStringLiteral("<null>"))
                << ", title = "
                << (m_pNote->hasTitle() ? m_pNote->title()
                                        : QStringLiteral("<null>")));

        Q_EMIT cantConvertToNote(errorDescription);
        return false;
    }

    if (m_pNotebook && m_pNotebook->hasRestrictions()) {
        const auto & restrictions = m_pNotebook->restrictions();
        if (restrictions.noUpdateNotes.isSet() &&
            restrictions.noUpdateNotes.ref()) {
            errorDescription.setBase(
                QT_TR_NOOP("The notebook the current note belongs to doesn't "
                           "allow notes modification, the changes won't be "
                           "saved"));

            QNINFO(
                "note_editor",
                errorDescription
                    << ", note: local uid = " << m_pNote->localUid()
                    << ", guid = "
                    << (m_pNote->hasGuid() ? m_pNote->guid()
                                           : QStringLiteral("<null>"))
                    << ", title = "
                    << (m_pNote->hasTitle() ? m_pNote->title()
                                            : QStringLiteral("<null>"))
                    << ", notebook: local uid = " << m_pNotebook->localUid()
                    << ", guid = "
                    << (m_pNotebook->hasGuid() ? m_pNotebook->guid()
                                               : QStringLiteral("<null>"))
                    << ", name = "
                    << (m_pNotebook->hasName() ? m_pNotebook->name()
                                               : QStringLiteral("<null>")));
            Q_EMIT cantConvertToNote(errorDescription);
            return false;
        }
    }

    m_pendingConversionToNote = true;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    m_htmlCachedMemory = page()->mainFrame()->toHtml();
    onPageHtmlReceived(m_htmlCachedMemory);
#else
    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));
#endif

    return true;
}

void NoteEditorPrivate::updateHashForResourceTag(
    const QByteArray & oldResourceHash, const QByteArray & newResourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::updateHashForResourceTag: "
            << "old hash = " << oldResourceHash.toHex()
            << ", new hash = " << newResourceHash.toHex());

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("updateResourceHash('") +
        QString::fromLocal8Bit(oldResourceHash.toHex()) +
        QStringLiteral("', '") +
        QString::fromLocal8Bit(newResourceHash.toHex()) +
        QStringLiteral("');"));
}

void NoteEditorPrivate::provideSrcForResourceImgTags()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::provideSrcForResourceImgTags");

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("provideSrcForResourceImgTags();"));
}

void NoteEditorPrivate::manualSaveResourceToFile(const Resource & resource)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::manualSaveResourceToFile");

    if (Q_UNLIKELY(!resource.hasDataBody() && !resource.hasAlternateDataBody()))
    {
        ErrorString error(
            QT_TR_NOOP("Can't save resource to file: resource has "
                       "neither data body nor alternate data body"));
        QNINFO("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.hasMime())) {
        ErrorString error(
            QT_TR_NOOP("Can't save resource to file: resource has "
                       "no mime type"));
        QNINFO("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    QString resourcePreferredSuffix = resource.preferredFileSuffix();
    QString resourcePreferredFilterString;
    if (!resourcePreferredSuffix.isEmpty()) {
        resourcePreferredFilterString = QStringLiteral("(*.") +
            resourcePreferredSuffix + QStringLiteral(")");
    }

    const QString & mimeTypeName = resource.mime();

    auto preferredSuffixesIter = m_fileSuffixesForMimeType.find(mimeTypeName);
    auto fileFilterStringIter =
        m_fileFilterStringForMimeType.find(mimeTypeName);

    if ((preferredSuffixesIter == m_fileSuffixesForMimeType.end()) ||
        (fileFilterStringIter == m_fileFilterStringForMimeType.end()))
    {
        QMimeDatabase mimeDatabase;
        QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeTypeName);
        if (Q_UNLIKELY(!mimeType.isValid())) {
            ErrorString error(
                QT_TR_NOOP("Can't save resource to file: can't "
                           "identify resource's mime type"));
            QNINFO(
                "note_editor", error << ", mime type name: " << mimeTypeName);
            Q_EMIT notifyError(error);
            return;
        }

        bool shouldSkipResourcePreferredSuffix = false;
        auto suffixes = mimeType.suffixes();
        if (!resourcePreferredSuffix.isEmpty() &&
            !suffixes.contains(resourcePreferredSuffix))
        {
            const int numSuffixes = suffixes.size();
            for (int i = 0; i < numSuffixes; ++i) {
                const auto & currentSuffix = suffixes[i];
                if (resourcePreferredSuffix.contains(currentSuffix)) {
                    shouldSkipResourcePreferredSuffix = true;
                    break;
                }
            }

            if (!shouldSkipResourcePreferredSuffix) {
                suffixes.prepend(resourcePreferredSuffix);
            }
        }

        QString filterString = mimeType.filterString();
        if (!shouldSkipResourcePreferredSuffix &&
            !resourcePreferredFilterString.isEmpty())
        {
            filterString +=
                QStringLiteral(";;") + resourcePreferredFilterString;
        }

        if (preferredSuffixesIter == m_fileSuffixesForMimeType.end()) {
            preferredSuffixesIter =
                m_fileSuffixesForMimeType.insert(mimeTypeName, suffixes);
        }

        if (fileFilterStringIter == m_fileFilterStringForMimeType.end()) {
            fileFilterStringIter = m_fileFilterStringForMimeType.insert(
                mimeTypeName, filterString);
        }
    }

    QString preferredSuffix;
    QString preferredFolderPath;

    const QStringList & preferredSuffixes = preferredSuffixesIter.value();
    if (!preferredSuffixes.isEmpty()) {
        CHECK_ACCOUNT(QT_TR_NOOP("Internal error: can't save the attachment"))

        ApplicationSettings appSettings(*m_pAccount, NOTE_EDITOR_SETTINGS_NAME);
        QStringList childGroups = appSettings.childGroups();
        int attachmentsSaveLocGroupIndex =
            childGroups.indexOf(NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY);
        if (attachmentsSaveLocGroupIndex >= 0) {
            QNTRACE(
                "note_editor",
                "Found cached attachment save location "
                    << "group within application settings");

            appSettings.beginGroup(NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY);
            auto cachedFileSuffixes = appSettings.childKeys();
            const int numPreferredSuffixes = preferredSuffixes.size();
            for (int i = 0; i < numPreferredSuffixes; ++i) {
                preferredSuffix = preferredSuffixes[i];
                int indexInCache = cachedFileSuffixes.indexOf(preferredSuffix);
                if (indexInCache < 0) {
                    QNTRACE(
                        "note_editor",
                        "Haven't found cached attachment "
                            << "save directory for file suffix "
                            << preferredSuffix);
                    continue;
                }

                QVariant dirValue = appSettings.value(preferredSuffix);
                if (dirValue.isNull() || !dirValue.isValid()) {
                    QNTRACE(
                        "note_editor",
                        "Found inappropriate attachment "
                            << "save directory for file suffix "
                            << preferredSuffix);
                    continue;
                }

                QFileInfo dirInfo(dirValue.toString());
                if (!dirInfo.exists()) {
                    QNTRACE(
                        "note_editor",
                        "Cached attachment save directory "
                            << "for file suffix " << preferredSuffix
                            << " does not exist: " << dirInfo.absolutePath());
                    continue;
                }
                else if (!dirInfo.isDir()) {
                    QNTRACE(
                        "note_editor",
                        "Cached attachment save directory "
                            << "for file suffix " << preferredSuffix
                            << " is not a directory: "
                            << dirInfo.absolutePath());
                    continue;
                }
                else if (!dirInfo.isWritable()) {
                    QNTRACE(
                        "note_editor",
                        "Cached attachment save directory "
                            << "for file suffix " << preferredSuffix
                            << " is not writable: " << dirInfo.absolutePath());
                    continue;
                }

                preferredFolderPath = dirInfo.absolutePath();
                break;
            }

            appSettings.endGroup();
        }
    }

    const QString & filterString = fileFilterStringIter.value();

    QString * pSelectedFilter =
        (filterString.contains(resourcePreferredFilterString)
             ? &resourcePreferredFilterString
             : nullptr);

    QString absoluteFilePath = QFileDialog::getSaveFileName(
        this, tr("Save as") + QStringLiteral("..."), preferredFolderPath,
        filterString, pSelectedFilter);
    if (absoluteFilePath.isEmpty()) {
        QNINFO("note_editor", "User cancelled saving resource to file");
        return;
    }

    bool foundSuffix = false;
    const int numPreferredSuffixes = preferredSuffixes.size();
    for (int i = 0; i < numPreferredSuffixes; ++i) {
        const auto & currentSuffix = preferredSuffixes[i];
        if (absoluteFilePath.endsWith(currentSuffix, Qt::CaseInsensitive)) {
            foundSuffix = true;
            break;
        }
    }

    if (!foundSuffix) {
        absoluteFilePath += QStringLiteral(".") + preferredSuffix;
    }

    QUuid saveResourceToFileRequestId = QUuid::createUuid();

    const QByteArray & data =
        (resource.hasDataBody() ? resource.dataBody()
                                : resource.alternateDataBody());

    Q_UNUSED(m_manualSaveResourceToFileRequestIds.insert(
        saveResourceToFileRequestId));

    Q_EMIT saveResourceToFile(
        absoluteFilePath, data, saveResourceToFileRequestId,
        /* append = */ false);

    QNDEBUG(
        "note_editor",
        "Sent request to manually save resource to file, "
            << "request id = " << saveResourceToFileRequestId
            << ", resource local uid = " << resource.localUid());
}

QImage NoteEditorPrivate::buildGenericResourceImage(const Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::buildGenericResourceImage: "
            << "resource local uid = " << resource.localUid());

    QString resourceDisplayName = resource.displayName();
    if (Q_UNLIKELY(resourceDisplayName.isEmpty())) {
        resourceDisplayName = tr("Attachment");
    }

    QNTRACE("note_editor", "Resource display name = " << resourceDisplayName);

    QFont font = m_font;
    font.setPointSize(10);

    QString originalResourceDisplayName = resourceDisplayName;

    const int maxResourceDisplayNameWidth = 146;
    QFontMetrics fontMetrics(font);
    int width = fontMetricsWidth(fontMetrics, resourceDisplayName);
    int singleCharWidth = fontMetricsWidth(fontMetrics, QStringLiteral("n"));
    int ellipsisWidth = fontMetricsWidth(fontMetrics, QStringLiteral("..."));

    bool smartReplaceWorked = true;
    int previousWidth = width + 1;

    while (width > maxResourceDisplayNameWidth) {
        if (width >= previousWidth) {
            smartReplaceWorked = false;
            break;
        }

        previousWidth = width;

        int widthOverflow = width - maxResourceDisplayNameWidth;
        int numCharsToSkip =
            (widthOverflow + ellipsisWidth) / singleCharWidth + 1;

        int dotIndex = resourceDisplayName.lastIndexOf(QStringLiteral("."));
        if (dotIndex != 0 && (dotIndex > resourceDisplayName.size() / 2)) {
            // Try to shorten the name while preserving the file extension.
            // Need to skip some chars before the dot index
            int startSkipPos = dotIndex - numCharsToSkip;
            if (startSkipPos >= 0) {
                resourceDisplayName.replace(
                    startSkipPos, numCharsToSkip, QStringLiteral("..."));
                width = fontMetricsWidth(fontMetrics, resourceDisplayName);
                continue;
            }
        }

        // Either no file extension or name contains a dot, skip some chars
        // without attempt to preserve the file extension
        resourceDisplayName.replace(
            resourceDisplayName.size() - numCharsToSkip, numCharsToSkip,
            QStringLiteral("..."));

        width = fontMetricsWidth(fontMetrics, resourceDisplayName);
    }

    if (!smartReplaceWorked) {
        QNTRACE(
            "note_editor",
            "Wasn't able to shorten the resource name "
                << "nicely, will try to shorten it just somehow");

        width = fontMetricsWidth(fontMetrics, originalResourceDisplayName);
        int widthOverflow = width - maxResourceDisplayNameWidth;
        int numCharsToSkip =
            (widthOverflow + ellipsisWidth) / singleCharWidth + 1;
        resourceDisplayName = originalResourceDisplayName;

        if (resourceDisplayName.size() > numCharsToSkip) {
            resourceDisplayName.replace(
                resourceDisplayName.size() - numCharsToSkip, numCharsToSkip,
                QStringLiteral("..."));
        }
        else {
            resourceDisplayName = QStringLiteral("Attachment...");
        }
    }

    QNTRACE(
        "note_editor",
        "(possibly) shortened resource display name: "
            << resourceDisplayName << ", width = "
            << fontMetricsWidth(fontMetrics, resourceDisplayName));

    QString resourceHumanReadableSize;
    if (resource.hasDataSize() || resource.hasAlternateDataSize()) {
        resourceHumanReadableSize = humanReadableSize(
            resource.hasDataSize()
                ? static_cast<quint64>(resource.dataSize())
                : static_cast<quint64>(resource.alternateDataSize()));
    }

    QIcon resourceIcon;
    bool useFallbackGenericResourceIcon = false;

    if (resource.hasMime()) {
        const auto & resourceMimeTypeName = resource.mime();
        QMimeDatabase mimeDatabase;
        QMimeType mimeType = mimeDatabase.mimeTypeForName(resourceMimeTypeName);
        if (mimeType.isValid()) {
            resourceIcon = QIcon::fromTheme(mimeType.genericIconName());
            if (resourceIcon.isNull()) {
                QNTRACE(
                    "note_editor",
                    "Can't get icon from theme by name "
                        << mimeType.genericIconName());
                useFallbackGenericResourceIcon = true;
            }
        }
        else {
            QNTRACE(
                "note_editor",
                "Can't get valid mime type for name "
                    << resourceMimeTypeName
                    << ", will use fallback generic resource icon");
            useFallbackGenericResourceIcon = true;
        }
    }
    else {
        QNINFO(
            "note_editor",
            "Found resource without mime type set: " << resource);

        QNTRACE("note_editor", "Will use fallback generic resource icon");
        useFallbackGenericResourceIcon = true;
    }

    if (useFallbackGenericResourceIcon) {
        resourceIcon = QIcon(
            QStringLiteral(":/generic_resource_icons/png/attachment.png"));
    }

    QPixmap pixmap(230, 32);
    pixmap.fill();

    QPainter painter;
    painter.begin(&pixmap);
    painter.setFont(font);

    // Draw resource icon
    painter.drawPixmap(QPoint(2, 4), resourceIcon.pixmap(QSize(24, 24)));

    // Draw resource display name
    painter.drawText(QPoint(28, 14), resourceDisplayName);

    // Draw resource display size
    painter.drawText(QPoint(28, 28), resourceHumanReadableSize);

    // Draw open resource icon
    QIcon openResourceIcon = QIcon::fromTheme(
        QStringLiteral("document-open"),
        QIcon(QStringLiteral(":/generic_resource_icons/png/open_with.png")));

    painter.drawPixmap(QPoint(174, 4), openResourceIcon.pixmap(QSize(24, 24)));

    // Draw save resource icon
    QIcon saveResourceIcon = QIcon::fromTheme(
        QStringLiteral("document-save"),
        QIcon(QStringLiteral(":/generic_resource_icons/png/save.png")));

    painter.drawPixmap(QPoint(202, 4), saveResourceIcon.pixmap(QSize(24, 24)));

    painter.end();
    return pixmap.toImage();
}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void NoteEditorPrivate::saveGenericResourceImage(
    const Resource & resource, const QImage & image)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::saveGenericResourceImage: "
            << "resource local uid = " << resource.localUid());

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't save the generic resource image: "
                       "no note is set to the editor"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.hasDataHash() && !resource.hasAlternateDataHash()))
    {
        ErrorString error(
            QT_TR_NOOP("Can't save generic resource image: resource has "
                       "neither data hash nor alternate data hash"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    QByteArray imageData;
    QBuffer buffer(&imageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));
    image.save(&buffer, "PNG");

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_saveGenericResourceImageToFileRequestIds.insert(requestId));

    QNDEBUG(
        "note_editor",
        "Emitting request to write generic resource image "
            << "for resource with local uid " << resource.localUid()
            << ", request id " << requestId);

    Q_EMIT saveGenericResourceImageToFile(
        m_pNote->localUid(), resource.localUid(), imageData,
        QStringLiteral("png"),
        (resource.hasDataHash() ? resource.dataHash()
                                : resource.alternateDataHash()),
        resource.displayName(), requestId);
}

void NoteEditorPrivate::provideSrcAndOnClickScriptForImgEnCryptTags()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::provideSrcAndOnClickScriptForImgEnCryptTags");

    if (Q_UNLIKELY(!m_pNote)) {
        QNTRACE("note_editor", "No note is set for the editor");
        return;
    }

    QString iconPath =
        QStringLiteral("qrc:/encrypted_area_icons/en-crypt/en-crypt.png");

    QString javascript =
        QStringLiteral("provideSrcAndOnClickScriptForEnCryptImgTags(\"") +
        iconPath + QStringLiteral("\")");

    GET_PAGE()
    page->executeJavaScript(javascript);
}

void NoteEditorPrivate::setupGenericResourceImages()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupGenericResourceImages");

    if (!m_pNote) {
        QNDEBUG("note_editor", "No note to build generic resource images for");
        return;
    }

    if (!m_pNote->hasResources()) {
        QNDEBUG("note_editor", "Note has no resources, nothing to do");
        return;
    }

    QString mimeTypeName;
    size_t resourceImagesCounter = 0;
    bool shouldWaitForResourceImagesToSave = false;

    auto resources = m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = qAsConst(resources).at(i);
        if (resource.hasMime()) {
            mimeTypeName = resource.mime();
            if (mimeTypeName.startsWith(QStringLiteral("image/"))) {
                QNTRACE("note_editor", "Skipping image resource " << resource);
                continue;
            }
        }

        shouldWaitForResourceImagesToSave |=
            findOrBuildGenericResourceImage(resource);

        ++resourceImagesCounter;
    }

    if (resourceImagesCounter == 0) {
        QNDEBUG(
            "note_editor",
            "No generic resources requiring building "
                << "custom images were found");
        return;
    }

    if (shouldWaitForResourceImagesToSave) {
        QNTRACE(
            "note_editor",
            "Some generic resource images are being saved "
                << "to files, waiting");
        return;
    }

    QNTRACE("note_editor", "All generic resource images are ready");
    provideSrcForGenericResourceImages();
    setupGenericResourceOnClickHandler();
}

bool NoteEditorPrivate::findOrBuildGenericResourceImage(
    const Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::findOrBuildGenericResourceImage: " << resource);

    if (!resource.hasDataHash() && !resource.hasAlternateDataHash()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Found resource without either data hash or alternate "
                       "data hash"));
        QNWARNING("note_editor", errorDescription << ": " << resource);
        Q_EMIT notifyError(errorDescription);
        return true;
    }

    const QString localUid = resource.localUid();

    const QByteArray & resourceHash =
        (resource.hasDataHash() ? resource.dataHash()
                                : resource.alternateDataHash());

    QNTRACE(
        "note_editor",
        "Looking for existing generic resource image file "
            << "for resource with hash " << resourceHash.toHex());

    auto it = m_genericResourceImageFilePathsByResourceHash.find(resourceHash);
    if (it != m_genericResourceImageFilePathsByResourceHash.end()) {
        QNTRACE(
            "note_editor",
            "Found generic resource image file path "
                << "for resource with hash " << resourceHash.toHex()
                << " and local uid " << localUid << ": " << it.value());
        return false;
    }

    QImage img = buildGenericResourceImage(resource);
    if (img.isNull()) {
        QNDEBUG("note_editor", "Can't build generic resource image");
        return true;
    }

    saveGenericResourceImage(resource, img);
    return true;
}

void NoteEditorPrivate::provideSrcForGenericResourceImages()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::provideSrcForGenericResourceImages");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("provideSrcForGenericResourceImages();"));
}

void NoteEditorPrivate::setupGenericResourceOnClickHandler()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupGenericResourceOnClickHandler");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("setupGenericResourceOnClickHandler();"));
}

void NoteEditorPrivate::setupWebSocketServer()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupWebSocketServer");

    if (m_pWebSocketServer->isListening()) {
        m_pWebSocketServer->close();
        QNDEBUG(
            "note_editor",
            "Closed the already established web socket "
                << "server");
        m_webSocketReady = false;
    }

    if (!m_pWebSocketServer->listen(QHostAddress::LocalHost, 0)) {
        ErrorString error(QT_TR_NOOP("Can't open web socket server"));
        error.details() = m_pWebSocketServer->errorString();
        QNERROR("note_editor", error);
        throw NoteEditorInitializationException(error);
    }

    m_webSocketServerPort = m_pWebSocketServer->serverPort();
    QNDEBUG(
        "note_editor",
        "Using automatically selected websocket server port "
            << m_webSocketServerPort);

    QObject::connect(
        m_pWebSocketClientWrapper, &WebSocketClientWrapper::clientConnected,
        m_pWebChannel, &QWebChannel::connectTo,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void NoteEditorPrivate::setupJavaScriptObjects()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupJavaScriptObjects");

    QObject::connect(
        m_pEnCryptElementClickHandler, &EnCryptElementOnClickHandler::decrypt,
        this, &NoteEditorPrivate::decryptEncryptedText,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pGenericResourceOpenAndSaveButtonsOnClickHandler,
        &GenericResourceOpenAndSaveButtonsOnClickHandler::saveResourceRequest,
        this, &NoteEditorPrivate::onSaveResourceRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pGenericResourceOpenAndSaveButtonsOnClickHandler,
        &GenericResourceOpenAndSaveButtonsOnClickHandler::openResourceRequest,
        this, &NoteEditorPrivate::onOpenResourceRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionChanged, this,
        &NoteEditorPrivate::onTextCursorPositionChange,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pHyperlinkClickJavaScriptHandler,
        &HyperlinkClickJavaScriptHandler::hyperlinkClicked, this,
        &NoteEditorPrivate::onHyperlinkClicked,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pWebSocketWaiter, &WebSocketWaiter::ready, this,
        &NoteEditorPrivate::onWebSocketReady,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_pWebChannel->registerObject(
        QStringLiteral("webSocketWaiter"), m_pWebSocketWaiter);

    m_pWebChannel->registerObject(
        QStringLiteral("actionsWatcher"), m_pActionsWatcher);

    m_pWebChannel->registerObject(
        QStringLiteral("resourceCache"), m_pResourceInfoJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("enCryptElementClickHandler"),
        m_pEnCryptElementClickHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("pageMutationObserver"), m_pPageMutationHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("openAndSaveResourceButtonsHandler"),
        m_pGenericResourceOpenAndSaveButtonsOnClickHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("textCursorPositionHandler"),
        m_pTextCursorPositionJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("contextMenuEventHandler"),
        m_pContextMenuEventJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("genericResourceImageHandler"),
        m_pGenericResoureImageJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("hyperlinkClickHandler"),
        m_pHyperlinkClickJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("toDoCheckboxClickHandler"),
        m_pToDoCheckboxClickHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("toDoCheckboxAutomaticInsertionHandler"),
        m_pToDoCheckboxAutomaticInsertionHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("tableResizeHandler"), m_pTableResizeJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("resizableImageHandler"),
        m_pResizableImageJavaScriptHandler);

    m_pWebChannel->registerObject(
        QStringLiteral("spellCheckerDynamicHelper"),
        m_pSpellCheckerDynamicHandler);

    QNDEBUG("note_editor", "Registered objects exposed to JavaScript");

    m_setUpJavaScriptObjects = true;
}

void NoteEditorPrivate::setupTextCursorPositionTracking()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::setupTextCursorPositionTracking");

    QString javascript = QStringLiteral("setupTextCursorPositionTracking();");

    GET_PAGE()
    page->executeJavaScript(javascript);
}

#endif // QUENTIER_USE_QT_WEB_ENGINE

void NoteEditorPrivate::updateResource(
    const QString & resourceLocalUid, const QByteArray & previousResourceHash,
    Resource updatedResource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::updateResource: resource local "
            << "uid = " << resourceLocalUid
            << ", previous hash = " << previousResourceHash.toHex()
            << ", updated resource: " << updatedResource);

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: no note "
                       "is set to the editor"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!updatedResource.hasNoteLocalUid())) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated "
                       "resource has no note local uid"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!updatedResource.hasMime())) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated "
                       "resource has no mime type"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!updatedResource.hasDataBody())) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated "
                       "resource contains no data body"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!updatedResource.hasDataHash()) {
        updatedResource.setDataHash(QCryptographicHash::hash(
            updatedResource.dataBody(), QCryptographicHash::Md5));

        QNDEBUG(
            "note_editor",
            "Set updated resource's data hash to "
                << updatedResource.dataHash().toHex());
    }

    if (!updatedResource.hasDataSize()) {
        updatedResource.setDataSize(updatedResource.dataBody().size());
        QNDEBUG(
            "note_editor",
            "Set updated resource's data size to "
                << updatedResource.dataSize());
    }

    bool res = m_pNote->updateResource(updatedResource);
    if (Q_UNLIKELY(!res)) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: resource to be "
                       "updated was not found within the note"));

        QNWARNING(
            "note_editor",
            error << ", updated resource: " << updatedResource
                  << "\nNote: " << *m_pNote);
        Q_EMIT notifyError(error);
        return;
    }

    m_resourceInfo.removeResourceInfo(previousResourceHash);

    auto recoIt = m_recognitionIndicesByResourceHash.find(previousResourceHash);
    if (recoIt != m_recognitionIndicesByResourceHash.end()) {
        Q_UNUSED(m_recognitionIndicesByResourceHash.erase(recoIt));
    }

    updateHashForResourceTag(previousResourceHash, updatedResource.dataHash());

    m_pendingNoteImageResourceTemporaryFiles = true;

    /**
     * Emitting this signal would cause the update of the temporary file
     * corresponding to this resource (if any) within
     * ResourceDataInTemporaryFileStorageManager and then
     * NoteEditorPrivate::onNoteResourceTemporaryFilesReady
     * slot would get invoked where the src for img tag would be updated
     */
    Q_EMIT convertedToNote(*m_pNote);
}

void NoteEditorPrivate::setupGenericTextContextMenu(
    const QStringList & extraData, const QString & selectedHtml,
    bool insideDecryptedTextFragment)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setupGenericTextContextMenu: "
            << "selected html = " << selectedHtml
            << "; inside decrypted text fragment = "
            << (insideDecryptedTextFragment ? "true" : "false"));

    m_lastSelectedHtml = selectedHtml;

    delete m_pGenericTextContextMenu;
    m_pGenericTextContextMenu = new QMenu(this);

#define ADD_ACTION_WITH_SHORTCUT(key, name, menu, slot, enabled, ...)          \
    {                                                                          \
        QAction * action = new QAction(name, menu);                            \
        action->setEnabled(enabled);                                           \
        setupActionShortcut(key, QString::fromUtf8("" #__VA_ARGS__), *action); \
        QObject::connect(                                                      \
            action, &QAction::triggered, this, &NoteEditorPrivate::slot);      \
        menu->addAction(action);                                               \
    }

    bool enabled = true;

    // See if extraData contains the misspelled word
    QString misSpelledWord;
    const int extraDataSize = extraData.size();
    for (int i = 0; i < extraDataSize; ++i) {
        const QString & item = extraData[i];
        if (!item.startsWith(QStringLiteral("MisSpelledWord_"))) {
            continue;
        }

        misSpelledWord = item.mid(15);
    }

    if (!misSpelledWord.isEmpty()) {
        m_lastMisSpelledWord = misSpelledWord;

        QStringList correctionSuggestions;
        if (m_pSpellChecker) {
            correctionSuggestions =
                m_pSpellChecker->spellCorrectionSuggestions(misSpelledWord);
        }

        if (!correctionSuggestions.isEmpty()) {
            const int numCorrectionSuggestions = correctionSuggestions.size();
            for (int i = 0; i < numCorrectionSuggestions; ++i) {
                const auto & correctionSuggestion = correctionSuggestions[i];
                if (Q_UNLIKELY(correctionSuggestion.isEmpty())) {
                    continue;
                }

                QAction * action = new QAction(
                    correctionSuggestion, m_pGenericTextContextMenu);
                action->setText(correctionSuggestion);
                action->setToolTip(tr("Correct the misspelled word"));
                action->setEnabled(m_isPageEditable);

                QObject::connect(
                    action, &QAction::triggered, this,
                    &NoteEditorPrivate::onSpellCheckCorrectionAction);

                m_pGenericTextContextMenu->addAction(action);
            }

            Q_UNUSED(m_pGenericTextContextMenu->addSeparator());
        }

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::SpellCheckIgnoreWord, tr("Ignore word"),
            m_pGenericTextContextMenu, onSpellCheckIgnoreWordAction, enabled);

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::SpellCheckAddWordToUserDictionary,
            tr("Add word to user dictionary"), m_pGenericTextContextMenu,
            onSpellCheckAddWordToUserDictionaryAction, enabled);

        Q_UNUSED(m_pGenericTextContextMenu->addSeparator());
    }

    if (insideDecryptedTextFragment) {
        QString cipher, keyLength, encryptedText, decryptedText, hint, id;
        ErrorString error;
        bool res = parseEncryptedTextContextMenuExtraData(
            extraData, encryptedText, decryptedText, cipher, keyLength, hint,
            id, error);
        if (Q_UNLIKELY(!res)) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't display the encrypted text's context menu"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        m_currentContextMenuExtraData.m_encryptedText = encryptedText;
        m_currentContextMenuExtraData.m_keyLength = keyLength;
        m_currentContextMenuExtraData.m_cipher = cipher;
        m_currentContextMenuExtraData.m_hint = hint;
        m_currentContextMenuExtraData.m_id = id;
        m_currentContextMenuExtraData.m_decryptedText = decryptedText;
    }

    if (!selectedHtml.isEmpty()) {
        ADD_ACTION_WITH_SHORTCUT(
            QKeySequence::Cut, tr("Cut"), m_pGenericTextContextMenu, cut,
            m_isPageEditable);

        ADD_ACTION_WITH_SHORTCUT(
            QKeySequence::Copy, tr("Copy"), m_pGenericTextContextMenu, copy,
            enabled);
    }

    setupPasteGenericTextMenuActions();

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::Font, tr("Font") + QStringLiteral("..."),
        m_pGenericTextContextMenu, fontMenu, m_isPageEditable);

    setupParagraphSubMenuForGenericTextMenu(selectedHtml);
    setupStyleSubMenuForGenericTextMenu();
    setupSpellCheckerDictionariesSubMenuForGenericTextMenu();

    Q_UNUSED(m_pGenericTextContextMenu->addSeparator());

    if (extraData.contains(QStringLiteral("InsideTable"))) {
        QMenu * pTableMenu = m_pGenericTextContextMenu->addMenu(tr("Table"));

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::InsertRow, tr("Insert row"), pTableMenu,
            insertTableRow, m_isPageEditable);

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::InsertColumn, tr("Insert column"), pTableMenu,
            insertTableColumn, m_isPageEditable);

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::RemoveRow, tr("Remove row"), pTableMenu,
            removeTableRow, m_isPageEditable);

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::RemoveColumn, tr("Remove column"), pTableMenu,
            removeTableColumn, m_isPageEditable);

        Q_UNUSED(m_pGenericTextContextMenu->addSeparator());
    }
    else {
        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::InsertTable,
            tr("Insert table") + QStringLiteral("..."),
            m_pGenericTextContextMenu, insertTableDialog, m_isPageEditable);
    }

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::InsertHorizontalLine, tr("Insert horizontal line"),
        m_pGenericTextContextMenu, insertHorizontalLine, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::AddAttachment,
        tr("Add attachment") + QStringLiteral("..."), m_pGenericTextContextMenu,
        addAttachmentDialog, m_isPageEditable);

    Q_UNUSED(m_pGenericTextContextMenu->addSeparator());

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::InsertToDoTag, tr("Insert ToDo tag"),
        m_pGenericTextContextMenu, insertToDoCheckbox, m_isPageEditable);

    Q_UNUSED(m_pGenericTextContextMenu->addSeparator());

    auto * pHyperlinkMenu = m_pGenericTextContextMenu->addMenu(tr("Hyperlink"));

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::EditHyperlink, tr("Add/edit") + QStringLiteral("..."),
        pHyperlinkMenu, editHyperlinkDialog, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::CopyHyperlink, tr("Copy"), pHyperlinkMenu,
        copyHyperlink, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::RemoveHyperlink, tr("Remove"), pHyperlinkMenu,
        removeHyperlink, m_isPageEditable);

    if (!insideDecryptedTextFragment && !selectedHtml.isEmpty()) {
        Q_UNUSED(m_pGenericTextContextMenu->addSeparator());

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::Encrypt,
            tr("Encrypt selected fragment") + QStringLiteral("..."),
            m_pGenericTextContextMenu, encryptSelectedText, m_isPageEditable);
    }
    else if (insideDecryptedTextFragment) {
        Q_UNUSED(m_pGenericTextContextMenu->addSeparator());

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::Encrypt, tr("Encrypt back"),
            m_pGenericTextContextMenu, hideDecryptedTextUnderCursor,
            m_isPageEditable);
    }

    m_pGenericTextContextMenu->exec(m_lastContextMenuEventGlobalPos);
}

void NoteEditorPrivate::setupImageResourceContextMenu(
    const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setupImageResourceContextMenu: "
            << "resource hash = " << resourceHash.toHex());

    m_currentContextMenuExtraData.m_resourceHash = resourceHash;

    delete m_pImageResourceContextMenu;
    m_pImageResourceContextMenu = new QMenu(this);

    bool enabled = true;

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::CopyAttachment, tr("Copy"),
        m_pImageResourceContextMenu, copyAttachmentUnderCursor, enabled);

    bool canRemoveResource = m_isPageEditable && m_pAccount &&
        (m_pAccount->type() != Account::Type::Evernote);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::RemoveAttachment, tr("Remove"),
        m_pImageResourceContextMenu, removeAttachmentUnderCursor,
        canRemoveResource);

    Q_UNUSED(m_pImageResourceContextMenu->addSeparator());

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::ImageRotateClockwise, tr("Rotate clockwise"),
        m_pImageResourceContextMenu, rotateImageAttachmentUnderCursorClockwise,
        m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::ImageRotateCounterClockwise,
        tr("Rotate countercloskwise"), m_pImageResourceContextMenu,
        rotateImageAttachmentUnderCursorCounterclockwise, m_isPageEditable);

    Q_UNUSED(m_pImageResourceContextMenu->addSeparator());

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::OpenAttachment, tr("Open"),
        m_pImageResourceContextMenu, openAttachmentUnderCursor,
        m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::SaveAttachment, tr("Save as") + QStringLiteral("..."),
        m_pImageResourceContextMenu, saveAttachmentUnderCursor, enabled);

    m_pImageResourceContextMenu->exec(m_lastContextMenuEventGlobalPos);
}

void NoteEditorPrivate::setupNonImageResourceContextMenu(
    const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupNonImageResourceContextMenu: resource hash = "
            << resourceHash.toHex());

    m_currentContextMenuExtraData.m_resourceHash = resourceHash;

    delete m_pNonImageResourceContextMenu;
    m_pNonImageResourceContextMenu = new QMenu(this);

    bool enabled = true;

    ADD_ACTION_WITH_SHORTCUT(
        QKeySequence::Copy, tr("Copy"), m_pNonImageResourceContextMenu, copy,
        enabled);

    bool canRemoveResource = m_isPageEditable && m_pAccount &&
        (m_pAccount->type() != Account::Type::Evernote);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::RemoveAttachment, tr("Remove"),
        m_pNonImageResourceContextMenu, removeAttachmentUnderCursor,
        canRemoveResource);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::RenameAttachment, tr("Rename"),
        m_pNonImageResourceContextMenu, renameAttachmentUnderCursor,
        m_isPageEditable);

    QClipboard * pClipboard = QApplication::clipboard();
    if (pClipboard && pClipboard->mimeData(QClipboard::Clipboard)) {
        QNTRACE(
            "note_editor",
            "Clipboard buffer has something, adding paste "
                << "action");

        ADD_ACTION_WITH_SHORTCUT(
            QKeySequence::Paste, tr("Paste"), m_pNonImageResourceContextMenu,
            paste, m_isPageEditable);
    }

    m_pNonImageResourceContextMenu->exec(m_lastContextMenuEventGlobalPos);
}

void NoteEditorPrivate::setupEncryptedTextContextMenu(
    const QString & cipher, const QString & keyLength,
    const QString & encryptedText, const QString & hint, const QString & id)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setupEncryptedTextContextMenu: "
            << "cipher = " << cipher << ", key length = " << keyLength
            << ", encrypted text = " << encryptedText << ", hint = " << hint
            << ", en-crypt-id = " << id);

    m_currentContextMenuExtraData.m_encryptedText = encryptedText;
    m_currentContextMenuExtraData.m_keyLength = keyLength;
    m_currentContextMenuExtraData.m_cipher = cipher;
    m_currentContextMenuExtraData.m_hint = hint;
    m_currentContextMenuExtraData.m_id = id;

    delete m_pEncryptedTextContextMenu;
    m_pEncryptedTextContextMenu = new QMenu(this);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::Decrypt, tr("Decrypt") + QStringLiteral("..."),
        m_pEncryptedTextContextMenu, decryptEncryptedTextUnderCursor,
        m_isPageEditable);

    m_pEncryptedTextContextMenu->exec(m_lastContextMenuEventGlobalPos);
}

void NoteEditorPrivate::setupActionShortcut(
    const int key, const QString & context, QAction & action)
{
    if (Q_UNLIKELY(!m_pAccount)) {
        QNDEBUG(
            "note_editor",
            "Can't set shortcut to the action: no account "
                << "is set to the note editor");
        return;
    }

    ShortcutManager shortcutManager;
    QKeySequence shortcut = shortcutManager.shortcut(key, *m_pAccount, context);
    if (!shortcut.isEmpty()) {
        QNTRACE(
            "note_editor",
            "Setting shortcut " << shortcut << " for action "
                                << action.objectName() << " (" << action.text()
                                << ")");
        action.setShortcut(shortcut);
    }
}

void NoteEditorPrivate::setupFileIO()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupFileIO");

    QObject::connect(
        this, &NoteEditorPrivate::writeNoteHtmlToFile, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onWriteFileRequest);

    QObject::connect(
        this, &NoteEditorPrivate::saveResourceToFile, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onWriteFileRequest);

    QObject::connect(
        m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::writeFileRequestProcessed, this,
        &NoteEditorPrivate::onWriteFileRequestProcessed);

    if (m_pResourceDataInTemporaryFileStorageManager) {
        m_pResourceDataInTemporaryFileStorageManager->deleteLater();
        m_pResourceDataInTemporaryFileStorageManager = nullptr;
    }

    m_pResourceDataInTemporaryFileStorageManager =
        new ResourceDataInTemporaryFileStorageManager();
    m_pResourceDataInTemporaryFileStorageManager->moveToThread(
        m_pFileIOProcessorAsync->thread());

    QObject::connect(
        this, &NoteEditorPrivate::currentNoteChanged,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::onCurrentNoteChanged);

    QObject::connect(
        this, &NoteEditorPrivate::convertedToNote,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::onCurrentNoteChanged);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            failedToPutResourceDataIntoTemporaryFile,
        this, &NoteEditorPrivate::onFailedToPutResourceDataInTemporaryFile);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            noteResourcesPreparationProgress,
        this,
        &NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationProgress);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            noteResourcesPreparationError,
        this, &NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationError);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::noteResourcesReady, this,
        &NoteEditorPrivate::onNoteResourceTemporaryFilesReady);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            openResourcePreparationProgress,
        this,
        &NoteEditorPrivate::onOpenResourceInExternalEditorPreparationProgress);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::failedToOpenResource, this,
        &NoteEditorPrivate::onFailedToOpenResourceInExternalEditor);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::openedResource, this,
        &NoteEditorPrivate::onOpenedResourceInExternalEditor);

    QObject::connect(
        this, &NoteEditorPrivate::openResourceFile,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::onOpenResourceRequest);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::resourceFileChanged, this,
        &NoteEditorPrivate::onResourceFileChanged);

    if (m_pGenericResourceImageManager) {
        m_pGenericResourceImageManager->deleteLater();
        m_pGenericResourceImageManager = nullptr;
    }

    m_pGenericResourceImageManager = new GenericResourceImageManager;
    m_pGenericResourceImageManager->setStorageFolderPath(
        m_genericResourceImageFileStoragePath);
    m_pGenericResourceImageManager->moveToThread(
        m_pFileIOProcessorAsync->thread());

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    QObject::connect(
        this, &NoteEditorPrivate::saveGenericResourceImageToFile,
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::onGenericResourceImageWriteRequest);

    QObject::connect(
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::genericResourceImageWriteReply, this,
        &NoteEditorPrivate::onGenericResourceImageSaved);

    QObject::connect(
        this, &NoteEditorPrivate::currentNoteChanged,
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::onCurrentNoteChanged);
#endif
}

void NoteEditorPrivate::setupSpellChecker()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupSpellChecker");

    QUENTIER_CHECK_PTR(
        "note_editor", m_pSpellChecker,
        QStringLiteral("no spell checker was passed to note editor"));

    if (!m_pSpellChecker->isReady()) {
        QObject::connect(
            m_pSpellChecker, &SpellChecker::ready, this,
            &NoteEditorPrivate::onSpellCheckerReady);
    }
    else {
        onSpellCheckerReady();
    }
}

void NoteEditorPrivate::setupScripts()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupScripts");

    initNoteEditorResources();

    QFile file;

#define SETUP_SCRIPT(scriptPathPart, scriptVarName)                            \
    file.setFileName(QStringLiteral(":/" scriptPathPart));                     \
    file.open(QIODevice::ReadOnly);                                            \
    scriptVarName = QString::fromUtf8(file.readAll());                         \
    file.close()

    SETUP_SCRIPT("javascript/jquery/jquery-2.1.3.min.js", m_jQueryJs);
    SETUP_SCRIPT("javascript/jquery/jquery-ui.min.js", m_jQueryUiJs);

    SETUP_SCRIPT(
        "javascript/scripts/pageMutationObserver.js", m_pageMutationObserverJs);

    SETUP_SCRIPT(
        "javascript/colResizable/colResizable-1.5.min.js",
        m_resizableTableColumnsJs);

    SETUP_SCRIPT(
        "javascript/scripts/resizableImageManager.js",
        m_resizableImageManagerJs);

    SETUP_SCRIPT("javascript/debounce/jquery.debounce-1.0.5.js", m_debounceJs);
    SETUP_SCRIPT("javascript/rangy/rangy-core.js", m_rangyCoreJs);

    SETUP_SCRIPT(
        "javascript/rangy/rangy-selectionsaverestore.js",
        m_rangySelectionSaveRestoreJs);

    SETUP_SCRIPT("javascript/hilitor/hilitor-utf8.js", m_hilitorJs);

    SETUP_SCRIPT(
        "javascript/scripts/imageAreasHilitor.js", m_imageAreasHilitorJs);

    SETUP_SCRIPT("javascript/scripts/onTableResize.js", m_onTableResizeJs);

    SETUP_SCRIPT(
        "javascript/scripts/nodeUndoRedoManager.js", m_nodeUndoRedoManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/selectionManager.js", m_selectionManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/textEditingUndoRedoManager.js",
        m_textEditingUndoRedoManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/getSelectionHtml.js", m_getSelectionHtmlJs);

    SETUP_SCRIPT(
        "javascript/scripts/snapSelectionToWord.js", m_snapSelectionToWordJs);

    SETUP_SCRIPT(
        "javascript/scripts/replaceSelectionWithHtml.js",
        m_replaceSelectionWithHtmlJs);

    SETUP_SCRIPT(
        "javascript/scripts/findReplaceManager.js", m_findReplaceManagerJs);

    SETUP_SCRIPT("javascript/scripts/spellChecker.js", m_spellCheckerJs);

    SETUP_SCRIPT(
        "javascript/scripts/managedPageAction.js", m_managedPageActionJs);

    SETUP_SCRIPT(
        "javascript/scripts/setInitialCaretPosition.js",
        m_setInitialCaretPositionJs);

    SETUP_SCRIPT(
        "javascript/scripts/toDoCheckboxAutomaticInserter.js",
        m_toDoCheckboxAutomaticInsertionJs);

    SETUP_SCRIPT("javascript/scripts/disablePaste.js", m_disablePasteJs);

    SETUP_SCRIPT(
        "javascript/scripts/updateResourceHash.js", m_updateResourceHashJs);

    SETUP_SCRIPT(
        "javascript/scripts/updateImageResourceSrc.js",
        m_updateImageResourceSrcJs);

    SETUP_SCRIPT(
        "javascript/scripts/provideSrcForResourceImgTags.js",
        m_provideSrcForResourceImgTagsJs);

    SETUP_SCRIPT(
        "javascript/scripts/onResourceInfoReceived.js",
        m_onResourceInfoReceivedJs);

    SETUP_SCRIPT(
        "javascript/scripts/findInnermostElement.js", m_findInnermostElementJs);

    SETUP_SCRIPT(
        "javascript/scripts/determineStatesForCurrentTextCursorPosition.js",
        m_determineStatesForCurrentTextCursorPositionJs);

    SETUP_SCRIPT(
        "javascript/scripts/determineContextMenuEventTarget.js",
        m_determineContextMenuEventTargetJs);

    SETUP_SCRIPT("javascript/scripts/tableManager.js", m_tableManagerJs);

    SETUP_SCRIPT("javascript/scripts/resourceManager.js", m_resourceManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/htmlInsertionManager.js", m_htmlInsertionManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/sourceCodeFormatter.js", m_sourceCodeFormatterJs);

    SETUP_SCRIPT(
        "javascript/scripts/hyperlinkManager.js", m_hyperlinkManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/encryptDecryptManager.js",
        m_encryptDecryptManagerJs);

    SETUP_SCRIPT(
        "javascript/scripts/findAndReplaceDOMText.js",
        m_findAndReplaceDOMTextJs);

    SETUP_SCRIPT(
        "javascript/scripts/tabAndShiftTabToIndentAndUnindentReplacer.js",
        m_tabAndShiftTabIndentAndUnindentReplacerJs);

    SETUP_SCRIPT("javascript/scripts/replaceStyle.js", m_replaceStyleJs);
    SETUP_SCRIPT("javascript/scripts/setFontFamily.js", m_setFontFamilyJs);
    SETUP_SCRIPT("javascript/scripts/setFontSize.js", m_setFontSizeJs);

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    SETUP_SCRIPT("javascript/scripts/qWebKitSetup.js", m_qWebKitSetupJs);
#else
    SETUP_SCRIPT("qtwebchannel/qwebchannel.js", m_qWebChannelJs);

    SETUP_SCRIPT(
        "javascript/scripts/qWebChannelSetup.js", m_qWebChannelSetupJs);
#endif // QUENTIER_USE_QT_WEB_ENGINE

    SETUP_SCRIPT("javascript/scripts/enToDoTagsSetup.js", m_setupEnToDoTagsJs);

    SETUP_SCRIPT(
        "javascript/scripts/flipEnToDoCheckboxState.js",
        m_flipEnToDoCheckboxStateJs);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    SETUP_SCRIPT(
        "javascript/scripts/provideSrcAndOnClickScriptForEnCryptImgTags.js",
        m_provideSrcAndOnClickScriptForEnCryptImgTagsJs);

    SETUP_SCRIPT(
        "javascript/scripts/provideSrcForGenericResourceImages.js",
        m_provideSrcForGenericResourceImagesJs);

    SETUP_SCRIPT(
        "javascript/scripts/onGenericResourceImageReceived.js",
        m_onGenericResourceImageReceivedJs);

    SETUP_SCRIPT(
        "javascript/scripts/genericResourceOnClickHandler.js",
        m_genericResourceOnClickHandlerJs);

    SETUP_SCRIPT(
        "javascript/scripts/setupGenericResourceOnClickHandler.js",
        m_setupGenericResourceOnClickHandlerJs);

    SETUP_SCRIPT(
        "javascript/scripts/clickInterceptor.js", m_clickInterceptorJs);

    SETUP_SCRIPT(
        "javascript/scripts/notifyTextCursorPositionChanged.js",
        m_notifyTextCursorPositionChangedJs);

    SETUP_SCRIPT(
        "javascript/scripts/setupTextCursorPositionTracking.js",
        m_setupTextCursorPositionTrackingJs);
#endif // QUENTIER_USE_QT_WEB_ENGINE

#undef SETUP_SCRIPT
}

void NoteEditorPrivate::setupGeneralSignalSlotConnections()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupGeneralSignalSlotConnections");

    QObject::connect(
        m_pTableResizeJavaScriptHandler,
        &TableResizeJavaScriptHandler::tableResized, this,
        &NoteEditorPrivate::onTableResized);

    QObject::connect(
        m_pResizableImageJavaScriptHandler,
        &ResizableImageJavaScriptHandler::imageResourceResized, this,
        &NoteEditorPrivate::onImageResourceResized);

    QObject::connect(
        m_pSpellCheckerDynamicHandler,
        &SpellCheckerDynamicHelper::lastEnteredWords, this,
        &NoteEditorPrivate::onSpellCheckerDynamicHelperUpdate);

    QObject::connect(
        m_pToDoCheckboxClickHandler,
        &ToDoCheckboxOnClickHandler::toDoCheckboxClicked, this,
        &NoteEditorPrivate::onToDoCheckboxClicked);

    QObject::connect(
        m_pToDoCheckboxAutomaticInsertionHandler,
        &ToDoCheckboxAutomaticInsertionHandler::
            notifyToDoCheckboxInsertedAutomatically,
        this, &NoteEditorPrivate::onToDoCheckboxAutomaticInsertion);

    QObject::connect(
        m_pToDoCheckboxClickHandler, &ToDoCheckboxOnClickHandler::notifyError,
        this, &NoteEditorPrivate::onToDoCheckboxClickHandlerError);

    QObject::connect(
        m_pPageMutationHandler, &PageMutationHandler::contentsChanged, this,
        &NoteEditorPrivate::contentChanged);

    QObject::connect(
        m_pPageMutationHandler, &PageMutationHandler::contentsChanged, this,
        &NoteEditorPrivate::noteModified);

    QObject::connect(
        m_pPageMutationHandler, &PageMutationHandler::contentsChanged, this,
        &NoteEditorPrivate::onContentChanged);

    QObject::connect(
        m_pContextMenuEventJavaScriptHandler,
        &ContextMenuEventJavaScriptHandler::contextMenuEventReply, this,
        &NoteEditorPrivate::onContextMenuEventReply);

    QObject::connect(
        m_pActionsWatcher, &ActionsWatcher::cutActionToggled, this,
        &NoteEditorPrivate::cut);

    QObject::connect(
        m_pActionsWatcher, &ActionsWatcher::pasteActionToggled, this,
        &NoteEditorPrivate::paste);

    // Connect with NoteEditorLocalStorageBroker

    auto & noteEditorLocalStorageBroker =
        NoteEditorLocalStorageBroker::instance();

    QObject::connect(
        this, &NoteEditorPrivate::findNoteAndNotebook,
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::findNoteAndNotebook);

    QObject::connect(
        this, &NoteEditorPrivate::saveNoteToLocalStorageRequest,
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::saveNoteToLocalStorage);

    QObject::connect(
        this, &NoteEditorPrivate::findResourceData,
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::findResourceData);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::noteSavedToLocalStorage, this,
        &NoteEditorPrivate::onNoteSavedToLocalStorage);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::failedToSaveNoteToLocalStorage, this,
        &NoteEditorPrivate::onFailedToSaveNoteToLocalStorage);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::foundNoteAndNotebook, this,
        &NoteEditorPrivate::onFoundNoteAndNotebook);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::failedToFindNoteOrNotebook, this,
        &NoteEditorPrivate::onFailedToFindNoteOrNotebook);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::noteUpdated, this,
        &NoteEditorPrivate::onNoteUpdated);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::notebookUpdated, this,
        &NoteEditorPrivate::onNotebookUpdated);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::noteDeleted, this,
        &NoteEditorPrivate::onNoteDeleted);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::notebookDeleted, this,
        &NoteEditorPrivate::onNotebookDeleted);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::foundResourceData, this,
        &NoteEditorPrivate::onFoundResourceData);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::failedToFindResourceData, this,
        &NoteEditorPrivate::onFailedToFindResourceData);

    // Connect with public NoteEditor class signals

    Q_Q(NoteEditor);

    QObject::connect(
        this, &NoteEditorPrivate::notifyError, q, &NoteEditor::notifyError);

    QObject::connect(
        this, &NoteEditorPrivate::inAppNoteLinkClicked, q,
        &NoteEditor::inAppNoteLinkClicked);

    QObject::connect(
        this, &NoteEditorPrivate::inAppNoteLinkPasteRequested, q,
        &NoteEditor::inAppNoteLinkPasteRequested);

    QObject::connect(
        this, &NoteEditorPrivate::convertedToNote, q,
        &NoteEditor::convertedToNote);

    QObject::connect(
        this, &NoteEditorPrivate::cantConvertToNote, q,
        &NoteEditor::cantConvertToNote);

    QObject::connect(
        this, &NoteEditorPrivate::noteEditorHtmlUpdated, q,
        &NoteEditor::noteEditorHtmlUpdated);

    QObject::connect(
        this, &NoteEditorPrivate::currentNoteChanged, q,
        &NoteEditor::currentNoteChanged);

    QObject::connect(
        this, &NoteEditorPrivate::contentChanged, q,
        &NoteEditor::contentChanged);

    QObject::connect(
        this, &NoteEditorPrivate::noteAndNotebookFoundInLocalStorage, q,
        &NoteEditor::noteAndNotebookFoundInLocalStorage);

    QObject::connect(
        this, &NoteEditorPrivate::noteNotFound, q, &NoteEditor::noteNotFound);

    QObject::connect(
        this, &NoteEditorPrivate::noteDeleted, q, &NoteEditor::noteDeleted);

    QObject::connect(
        this, &NoteEditorPrivate::noteModified, q, &NoteEditor::noteModified);

    QObject::connect(
        this, &NoteEditorPrivate::spellCheckerNotReady, q,
        &NoteEditor::spellCheckerNotReady);

    QObject::connect(
        this, &NoteEditorPrivate::spellCheckerReady, q,
        &NoteEditor::spellCheckerReady);

    QObject::connect(
        this, &NoteEditorPrivate::noteLoaded, q, &NoteEditor::noteLoaded);

    QObject::connect(
        this, &NoteEditorPrivate::noteSavedToLocalStorage, q,
        &NoteEditor::noteSavedToLocalStorage);

    QObject::connect(
        this, &NoteEditorPrivate::failedToSaveNoteToLocalStorage, q,
        &NoteEditor::failedToSaveNoteToLocalStorage);

    QObject::connect(
        this, &NoteEditorPrivate::insertTableDialogRequested, q,
        &NoteEditor::insertTableDialogRequested);
}

void NoteEditorPrivate::setupNoteEditorPage()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupNoteEditorPage");

    NoteEditorPage * page = new NoteEditorPage(*this);

    page->settings()->setAttribute(
        WebSettings::LocalContentCanAccessFileUrls, true);

    page->settings()->setAttribute(
        WebSettings::LocalContentCanAccessRemoteUrls, false);

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    page->settings()->setAttribute(QWebSettings::PluginsEnabled, true);
    page->settings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);

    page->setLinkDelegationPolicy(QWebPage::DelegateExternalLinks);
    page->setContentEditable(true);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("pageMutationObserver"), m_pPageMutationHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("resourceCache"), m_pResourceInfoJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("textCursorPositionHandler"),
        m_pTextCursorPositionJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("contextMenuEventHandler"),
        m_pContextMenuEventJavaScriptHandler, OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("toDoCheckboxClickHandler"), m_pToDoCheckboxClickHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("toDoCheckboxAutomaticInsertionHandler"),
        m_pToDoCheckboxAutomaticInsertionHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("tableResizeHandler"), m_pTableResizeJavaScriptHandler,
        OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("resizableImageHandler"),
        m_pResizableImageJavaScriptHandler, OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("spellCheckerDynamicHelper"),
        m_pSpellCheckerDynamicHandler, OwnershipNamespace::QtOwnership);

    page->mainFrame()->addToJavaScriptWindowObject(
        QStringLiteral("actionsWatcher"), m_pActionsWatcher,
        OwnershipNamespace::QtOwnership);

    m_pPluginFactory = new NoteEditorPluginFactory(*this, page);
    if (Q_LIKELY(m_pNote)) {
        m_pPluginFactory->setNote(*m_pNote);
    }

    QNDEBUG("note_editor", "Setting note editor plugin factory to the page");
    page->setPluginFactory(m_pPluginFactory);

#endif // QUENTIER_USE_QT_WEB_ENGINE

    setupNoteEditorPageConnections(page);
    setPage(page);

    QNTRACE("note_editor", "Done setting up new note editor page");
}

void NoteEditorPrivate::setupNoteEditorPageConnections(NoteEditorPage * page)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupNoteEditorPageConnections");

    QObject::connect(
        page, &NoteEditorPage::javaScriptLoaded, this,
        &NoteEditorPrivate::onJavaScriptLoaded);

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QObject::connect(
        page, &NoteEditorPage::microFocusChanged, this,
        &NoteEditorPrivate::onTextCursorPositionChange);

    QWebFrame * frame = page->mainFrame();

    QObject::connect(
        frame, &QWebFrame::loadFinished, this,
        &NoteEditorPrivate::onNoteLoadFinished);

    QObject::connect(
        page, &QWebPage::linkClicked, this,
        &NoteEditorPrivate::onHyperlinkClicked);
#else
    QObject::connect(
        page, &NoteEditorPage::loadFinished, this,
        &NoteEditorPrivate::onNoteLoadFinished);
#endif

    QObject::connect(
        page, &NoteEditorPage::undoActionRequested, this,
        &NoteEditorPrivate::undo);

    QObject::connect(
        page, &NoteEditorPage::redoActionRequested, this,
        &NoteEditorPrivate::redo);

    QObject::connect(
        page, &NoteEditorPage::pasteActionRequested, this,
        &NoteEditorPrivate::paste);

    QObject::connect(
        page, &NoteEditorPage::pasteAndMatchStyleActionRequested, this,
        &NoteEditorPrivate::pasteUnformatted);

    QObject::connect(
        page, &NoteEditorPage::cutActionRequested, this,
        &NoteEditorPrivate::cut);
}

void NoteEditorPrivate::setupTextCursorPositionJavaScriptHandlerConnections()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupTextCursorPositionJavaScriptHandlerConnections");

    // Connect JavaScript glue object's signals to slots
    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionBoldState, this,
        &NoteEditorPrivate::onTextCursorBoldStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionItalicState,
        this, &NoteEditorPrivate::onTextCursorItalicStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionUnderlineState,
        this, &NoteEditorPrivate::onTextCursorUnderlineStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionStrikethroughState,
        this, &NoteEditorPrivate::onTextCursorStrikethgouthStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionAlignLeftState,
        this, &NoteEditorPrivate::onTextCursorAlignLeftStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionAlignCenterState,
        this, &NoteEditorPrivate::onTextCursorAlignCenterStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionAlignRightState,
        this, &NoteEditorPrivate::onTextCursorAlignRightStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionAlignFullState,
        this, &NoteEditorPrivate::onTextCursorAlignFullStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionInsideOrderedListState,
        this, &NoteEditorPrivate::onTextCursorInsideOrderedListStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionInsideUnorderedListState,
        this, &NoteEditorPrivate::onTextCursorInsideUnorderedListStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionInsideTableState,
        this, &NoteEditorPrivate::onTextCursorInsideTableStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionOnImageResourceState,
        this, &NoteEditorPrivate::onTextCursorOnImageResourceStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionOnNonImageResourceState,
        this, &NoteEditorPrivate::onTextCursorOnNonImageResourceStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::
            textCursorPositionOnEnCryptTagState,
        this, &NoteEditorPrivate::onTextCursorOnEnCryptTagStateChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionFontName, this,
        &NoteEditorPrivate::onTextCursorFontNameChanged);

    QObject::connect(
        m_pTextCursorPositionJavaScriptHandler,
        &TextCursorPositionJavaScriptHandler::textCursorPositionFontSize, this,
        &NoteEditorPrivate::onTextCursorFontSizeChanged);

    // Connect signals to signals of public class
    Q_Q(NoteEditor);

    QObject::connect(
        this, &NoteEditorPrivate::textBoldState, q, &NoteEditor::textBoldState);

    QObject::connect(
        this, &NoteEditorPrivate::textItalicState, q,
        &NoteEditor::textItalicState);

    QObject::connect(
        this, &NoteEditorPrivate::textUnderlineState, q,
        &NoteEditor::textUnderlineState);

    QObject::connect(
        this, &NoteEditorPrivate::textStrikethroughState, q,
        &NoteEditor::textStrikethroughState);

    QObject::connect(
        this, &NoteEditorPrivate::textAlignLeftState, q,
        &NoteEditor::textAlignLeftState);

    QObject::connect(
        this, &NoteEditorPrivate::textAlignCenterState, q,
        &NoteEditor::textAlignCenterState);

    QObject::connect(
        this, &NoteEditorPrivate::textAlignRightState, q,
        &NoteEditor::textAlignRightState);

    QObject::connect(
        this, &NoteEditorPrivate::textAlignFullState, q,
        &NoteEditor::textAlignFullState);

    QObject::connect(
        this, &NoteEditorPrivate::textInsideOrderedListState, q,
        &NoteEditor::textInsideOrderedListState);

    QObject::connect(
        this, &NoteEditorPrivate::textInsideUnorderedListState, q,
        &NoteEditor::textInsideUnorderedListState);

    QObject::connect(
        this, &NoteEditorPrivate::textInsideTableState, q,
        &NoteEditor::textInsideTableState);

    QObject::connect(
        this, &NoteEditorPrivate::textFontFamilyChanged, q,
        &NoteEditor::textFontFamilyChanged);

    QObject::connect(
        this, &NoteEditorPrivate::textFontSizeChanged, q,
        &NoteEditor::textFontSizeChanged);
}

QString NoteEditorPrivate::noteEditorPagePrefix() const
{
    QString prefix;
    QTextStream strm(&prefix);

    strm << NOTE_EDITOR_PAGE_HEADER;
    strm << NOTE_EDITOR_PAGE_CSS;
    strm << "<title></title></head>"
         << "<style id=\"bodyStyleTag\" type=\"text/css\">";
    strm << bodyStyleCss();
    strm << "</style>";

    strm.flush();
    return prefix;
}

QString NoteEditorPrivate::bodyStyleCss() const
{
    QString css;
    QTextStream strm(&css);

    strm << "body { color: ";

    QPalette pal = defaultPalette();

    strm << pal.color(QPalette::WindowText).name();
    strm << "; background-color: ";
    strm << pal.color(QPalette::Base).name();
    strm << ";";

    appendDefaultFontInfoToCss(strm);

    strm << "}"
         << "::selection { "
         << "background: ";
    strm << pal.color(QPalette::Highlight).name();
    strm << "; color: ";
    strm << pal.color(QPalette::HighlightedText).name();

    strm << ";} ";

    strm.flush();
    return css;
}

void NoteEditorPrivate::appendDefaultFontInfoToCss(QTextStream & strm) const
{
    if (!m_pDefaultFont) {
        return;
    }

    strm << "font: ";

    if (m_pDefaultFont->bold()) {
        strm << "bold ";
    }

    if (m_pDefaultFont->italic()) {
        strm << "italic ";
    }

    QFontMetrics fontMetrics(*m_pDefaultFont);

    int pointSize = m_pDefaultFont->pointSize();
    if (pointSize >= 0) {
        strm << pointSize << "pt";
    }
    else {
        int pixelSize = m_pDefaultFont->pixelSize();
        strm << pixelSize << "px";
    }

    strm << "/" << fontMetrics.height();
    if (pointSize >= 0) {
        strm << "pt ";
    }
    else {
        strm << "px ";
    }

    strm << "\"" << m_pDefaultFont->family() << "\";";
}

void NoteEditorPrivate::setupSkipRulesForHtmlToEnmlConversion()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupSkipRulesForHtmlToEnmlConversion");

    m_skipRulesForHtmlToEnmlConversion.reserve(7);

    ENMLConverter::SkipHtmlElementRule tableSkipRule;
    tableSkipRule.m_attributeValueToSkip = QStringLiteral("JCLRgrip");

    tableSkipRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::StartsWith;

    tableSkipRule.m_attributeValueCaseSensitivity = Qt::CaseSensitive;
    m_skipRulesForHtmlToEnmlConversion << tableSkipRule;

    ENMLConverter::SkipHtmlElementRule hilitorSkipRule;
    hilitorSkipRule.m_includeElementContents = true;
    hilitorSkipRule.m_attributeValueToSkip = QStringLiteral("hilitorHelper");
    hilitorSkipRule.m_attributeValueCaseSensitivity = Qt::CaseInsensitive;

    hilitorSkipRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << hilitorSkipRule;

    ENMLConverter::SkipHtmlElementRule imageAreaHilitorSkipRule;
    imageAreaHilitorSkipRule.m_includeElementContents = false;

    imageAreaHilitorSkipRule.m_attributeValueToSkip =
        QStringLiteral("image-area-hilitor");

    imageAreaHilitorSkipRule.m_attributeValueCaseSensitivity =
        Qt::CaseSensitive;

    imageAreaHilitorSkipRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << imageAreaHilitorSkipRule;

    ENMLConverter::SkipHtmlElementRule spellCheckerHelperSkipRule;
    spellCheckerHelperSkipRule.m_includeElementContents = true;

    spellCheckerHelperSkipRule.m_attributeValueToSkip =
        QStringLiteral("misspell");

    spellCheckerHelperSkipRule.m_attributeValueCaseSensitivity =
        Qt::CaseSensitive;

    spellCheckerHelperSkipRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << spellCheckerHelperSkipRule;

    ENMLConverter::SkipHtmlElementRule rangySelectionBoundaryRule;
    rangySelectionBoundaryRule.m_includeElementContents = false;

    rangySelectionBoundaryRule.m_attributeValueToSkip =
        QStringLiteral("rangySelectionBoundary");

    rangySelectionBoundaryRule.m_attributeValueCaseSensitivity =
        Qt::CaseSensitive;

    rangySelectionBoundaryRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << rangySelectionBoundaryRule;

    ENMLConverter::SkipHtmlElementRule resizableImageHandleRule;
    resizableImageHandleRule.m_includeElementContents = false;

    resizableImageHandleRule.m_attributeValueToSkip =
        QStringLiteral("ui-resizable-handle");

    resizableImageHandleRule.m_attributeValueCaseSensitivity =
        Qt::CaseSensitive;

    resizableImageHandleRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << resizableImageHandleRule;

    ENMLConverter::SkipHtmlElementRule resizableImageHelperDivRule;
    resizableImageHelperDivRule.m_includeElementContents = true;

    resizableImageHelperDivRule.m_attributeValueToSkip =
        QStringLiteral("ui-wrapper");

    resizableImageHelperDivRule.m_attributeValueCaseSensitivity =
        Qt::CaseSensitive;

    resizableImageHelperDivRule.m_attributeValueComparisonRule =
        ENMLConverter::SkipHtmlElementRule::ComparisonRule::Contains;

    m_skipRulesForHtmlToEnmlConversion << resizableImageHelperDivRule;
}

QString NoteEditorPrivate::noteNotFoundPageHtml() const
{
    if (!m_noteNotFoundPageHtml.isEmpty()) {
        return m_noteNotFoundPageHtml;
    }

    QString text = tr("Failed to find the note in the local storage");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::noteDeletedPageHtml() const
{
    if (!m_noteDeletedPageHtml.isEmpty()) {
        return m_noteDeletedPageHtml;
    }

    QString text = tr("Note was deleted");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::noteLoadingPageHtml() const
{
    if (!m_noteLoadingPageHtml.isEmpty()) {
        return m_noteLoadingPageHtml;
    }

    QString text = tr("Loading note...");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::initialPageHtml() const
{
    if (!m_initialPageHtml.isEmpty()) {
        return m_initialPageHtml;
    }

    QString text = tr("Please select some existing note or create a new one");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::composeBlankPageHtml(const QString & rawText) const
{
    QString html;
    QTextStream strm(&html);

    strm << NOTE_EDITOR_PAGE_HEADER;
    strm << "<style>"
         << "body {"
         << "background-color: ";

    QColor backgroundColor = palette().color(QPalette::Window).darker(115);
    strm << backgroundColor.name();

    strm << ";"
         << "color: ";
    QColor foregroundColor = palette().color(QPalette::WindowText);
    strm << foregroundColor.name() << ";";

    appendDefaultFontInfoToCss(strm);

    strm << " "
         << "-webkit-user-select: none;"
         << "}"
         << ".outer {"
         << "    display: table;"
         << "    position: absolute;"
         << "    height: 95%;"
         << "    width: 95%;"
         << "}"
         << ".middle {"
         << "    display: table-cell;"
         << "    vertical-align: middle;"
         << "}"
         << ".inner {"
         << "    text-align: center;"
         << "}"
         << "</style><title></title></head>"
         << "<body><div class=\"outer\"><div class=\"middle\">"
         << "<div class=\"inner\">\n\n\n";

    strm << rawText;
    strm << "</div></div></div></body></html>";

    strm.flush();
    return html;
}

void NoteEditorPrivate::determineStatesForCurrentTextCursorPosition()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::determineStatesForCurrentTextCursorPosition");

    QString javascript = QStringLiteral(
        "if (typeof window[\"determineStatesForCurrentTextCursorPosition\"]"
        "!== 'undefined')"
        "{ determineStatesForCurrentTextCursorPosition(); }");

    GET_PAGE()
    page->executeJavaScript(javascript);
}

void NoteEditorPrivate::determineContextMenuEventTarget()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::determineContextMenuEventTarget");

    QString javascript = QStringLiteral("determineContextMenuEventTarget(") +
        QString::number(m_contextMenuSequenceNumber) + QStringLiteral(", ") +
        QString::number(m_lastContextMenuEventPagePos.x()) +
        QStringLiteral(", ") +
        QString::number(m_lastContextMenuEventPagePos.y()) +
        QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(javascript);
}

void NoteEditorPrivate::setPageEditable(const bool editable)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setPageEditable: "
            << (editable ? "true" : "false"));

    GET_PAGE()

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    page->setContentEditable(editable);
#else
    QString javascript = QStringLiteral("document.body.contentEditable='") +
        (editable ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral("'; document.designMode='") +
        (editable ? QStringLiteral("on") : QStringLiteral("off")) +
        QStringLiteral("'; void 0;");

    page->executeJavaScript(javascript);
    QNTRACE(
        "note_editor",
        "Queued javascript to make page "
            << (editable ? "editable" : "non-editable") << ": " << javascript);
#endif

    m_isPageEditable = editable;
}

bool NoteEditorPrivate::checkContextMenuSequenceNumber(
    const quint64 sequenceNumber) const
{
    return m_contextMenuSequenceNumber == sequenceNumber;
}

void NoteEditorPrivate::onPageHtmlReceived(
    const QString & html,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onPageHtmlReceived");
    Q_UNUSED(extraData)

    Q_EMIT noteEditorHtmlUpdated(html);

    if (!m_pendingConversionToNote) {
        return;
    }

    if (Q_UNLIKELY(!m_pNote)) {
        m_pendingConversionToNote = false;
        ErrorString error(QT_TR_NOOP("No current note is set to note editor"));
        Q_EMIT cantConvertToNote(error);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;
            Q_EMIT failedToSaveNoteToLocalStorage(error, m_noteLocalUid);
        }

        return;
    }

    if (Q_UNLIKELY(m_pNote->isInkNote())) {
        m_pendingConversionToNote = false;

        QNINFO(
            "note_editor",
            "Currently selected note is an ink note, it's "
                << "not editable hence won't respond to the unexpected change "
                   "of "
                << "its HTML");

        Q_EMIT convertedToNote(*m_pNote);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;
            // Pretend the note was actually saved to local storage
            Q_EMIT noteSavedToLocalStorage(m_noteLocalUid);
        }

        return;
    }

    m_lastSelectedHtml.resize(0);
    m_htmlCachedMemory = html;
    m_enmlCachedMemory.resize(0);
    ErrorString error;

    bool res = m_enmlConverter.htmlToNoteContent(
        m_htmlCachedMemory, m_enmlCachedMemory, *m_decryptedTextManager, error,
        m_skipRulesForHtmlToEnmlConversion);

    if (!res) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't convert note editor page's content to ENML"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        Q_EMIT notifyError(errorDescription);

        m_pendingConversionToNote = false;
        Q_EMIT cantConvertToNote(errorDescription);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;

            Q_EMIT failedToSaveNoteToLocalStorage(
                errorDescription, m_noteLocalUid);
        }

        return;
    }

    ErrorString errorDescription;
    if (!checkNoteSize(m_enmlCachedMemory, errorDescription)) {
        m_pendingConversionToNote = false;
        Q_EMIT cantConvertToNote(errorDescription);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;

            Q_EMIT failedToSaveNoteToLocalStorage(
                errorDescription, m_noteLocalUid);
        }

        return;
    }

    m_pNote->setContent(m_enmlCachedMemory);

    if (m_pendingConversionToNoteForSavingInLocalStorage) {
        m_pendingConversionToNoteForSavingInLocalStorage = false;

        if (m_needConversionToNote) {
            m_pNote->setDirty(true);

            m_pNote->setModificationTimestamp(
                QDateTime::currentMSecsSinceEpoch());
        }

        saveNoteToLocalStorage();
    }

    m_needConversionToNote = false;
    m_pendingConversionToNote = false;
    Q_EMIT convertedToNote(*m_pNote);
}

void NoteEditorPrivate::onSelectedTextEncryptionDone(
    const QVariant & dummy,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onSelectedTextEncryptionDone");

    Q_UNUSED(dummy);
    Q_UNUSED(extraData);

    m_pendingConversionToNote = true;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    m_htmlCachedMemory = page()->mainFrame()->toHtml();
    onPageHtmlReceived(m_htmlCachedMemory);
#else
    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));

    provideSrcAndOnClickScriptForImgEnCryptTags();
#endif
}

void NoteEditorPrivate::onTableActionDone(
    const QVariant & dummy,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onTableActionDone");

    Q_UNUSED(dummy);
    Q_UNUSED(extraData);

    setModified();
    convertToNote();
}

int NoteEditorPrivate::resourceIndexByHash(
    const QList<Resource> & resources, const QByteArray & resourceHash) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::resourceIndexByHash: hash = "
            << resourceHash.toHex());

    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = resources[i];
        if (resource.hasDataHash() && (resource.dataHash() == resourceHash)) {
            return i;
        }
    }

    return -1;
}

void NoteEditorPrivate::writeNotePageFile(const QString & html)
{
    m_writeNoteHtmlToFileRequestId = QUuid::createUuid();
    m_pendingIndexHtmlWritingToFile = true;
    QString pagePath = noteEditorPagePath();

    QNTRACE(
        "note_editor",
        "Emitting the request to write note html to file: "
            << "request id = " << m_writeNoteHtmlToFileRequestId);

    Q_EMIT writeNoteHtmlToFile(
        pagePath, html.toUtf8(), m_writeNoteHtmlToFileRequestId,
        /* append = */ false);
}

bool NoteEditorPrivate::parseEncryptedTextContextMenuExtraData(
    const QStringList & extraData, QString & encryptedText,
    QString & decryptedText, QString & cipher, QString & keyLength,
    QString & hint, QString & id, ErrorString & errorDescription) const
{
    if (Q_UNLIKELY(extraData.empty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Extra data from JavaScript is empty"));
        return false;
    }

    int extraDataSize = extraData.size();
    if (Q_UNLIKELY(extraDataSize != 5) && Q_UNLIKELY(extraDataSize != 6)) {
        errorDescription.setBase(
            QT_TR_NOOP("Extra data from JavaScript has wrong size"));
        errorDescription.details() = QString::number(extraDataSize);
        return false;
    }

    cipher = extraData[0];
    keyLength = extraData[1];
    encryptedText = extraData[2];
    hint = extraData[3];
    id = extraData[4];

    if (extraDataSize == 6) {
        decryptedText = extraData[5];
    }
    else {
        decryptedText.clear();
    }

    return true;
}

void NoteEditorPrivate::setupPasteGenericTextMenuActions()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupPasteGenericTextMenuActions");

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    bool clipboardHasHtml = false;
    bool clipboardHasText = false;
    bool clipboardHasImage = false;
    bool clipboardHasUrls = false;

    QClipboard * pClipboard = QApplication::clipboard();
    const QMimeData * pClipboardMimeData =
        (pClipboard ? pClipboard->mimeData(QClipboard::Clipboard) : nullptr);
    if (pClipboardMimeData) {
        if (pClipboardMimeData->hasHtml()) {
            clipboardHasHtml = !pClipboardMimeData->html().isEmpty();
        }
        else if (pClipboardMimeData->hasText()) {
            clipboardHasText = !pClipboardMimeData->text().isEmpty();
        }
        else if (pClipboardMimeData->hasImage()) {
            clipboardHasImage = true;
        }
        else if (pClipboardMimeData->hasUrls()) {
            clipboardHasUrls = true;
        }
    }

    if (clipboardHasHtml || clipboardHasText || clipboardHasImage ||
        clipboardHasUrls)
    {
        QNTRACE(
            "note_editor",
            "Clipboard buffer has something, adding paste "
                << "action");

        ADD_ACTION_WITH_SHORTCUT(
            QKeySequence::Paste, tr("Paste"), m_pGenericTextContextMenu, paste,
            m_isPageEditable);
    }

    if (clipboardHasHtml) {
        QNTRACE(
            "note_editor",
            "Clipboard buffer has html, adding paste "
                << "unformatted action");

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::PasteUnformatted, tr("Paste as unformatted text"),
            m_pGenericTextContextMenu, pasteUnformatted, m_isPageEditable);
    }

    Q_UNUSED(m_pGenericTextContextMenu->addSeparator());
}

void NoteEditorPrivate::setupParagraphSubMenuForGenericTextMenu(
    const QString & selectedHtml)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupParagraphSubMenuForGenericTextMenu: selected html = "
            << selectedHtml);

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    if (!isPageEditable()) {
        QNDEBUG(
            "note_editor",
            "Note is not editable, no paragraph sub-menu "
                << "actions are allowed");
        return;
    }

    QMenu * pParagraphSubMenu =
        m_pGenericTextContextMenu->addMenu(tr("Paragraph"));

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::AlignLeft, tr("Align left"), pParagraphSubMenu,
        alignLeft, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::AlignCenter, tr("Center text"), pParagraphSubMenu,
        alignCenter, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::AlignRight, tr("Align right"), pParagraphSubMenu,
        alignRight, m_isPageEditable);

    Q_UNUSED(pParagraphSubMenu->addSeparator());
    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::IncreaseIndentation, tr("Increase indentation"),
        pParagraphSubMenu, increaseIndentation, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::DecreaseIndentation, tr("Decrease indentation"),
        pParagraphSubMenu, decreaseIndentation, m_isPageEditable);

    Q_UNUSED(pParagraphSubMenu->addSeparator());

    if (!selectedHtml.isEmpty()) {
        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::IncreaseFontSize, tr("Increase font size"),
            pParagraphSubMenu, increaseFontSize, m_isPageEditable);

        ADD_ACTION_WITH_SHORTCUT(
            ShortcutManager::DecreaseFontSize, tr("Decrease font size"),
            pParagraphSubMenu, decreaseFontSize, m_isPageEditable);

        Q_UNUSED(pParagraphSubMenu->addSeparator());
    }

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::InsertNumberedList, tr("Numbered list"),
        pParagraphSubMenu, insertNumberedList, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::InsertBulletedList, tr("Bulleted list"),
        pParagraphSubMenu, insertBulletedList, m_isPageEditable);
}

void NoteEditorPrivate::setupStyleSubMenuForGenericTextMenu()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupStyleSubMenuForGenericTextMenu");

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    if (!isPageEditable()) {
        QNDEBUG(
            "note_editor",
            "Note is not editable, no style sub-menu "
                << "actions are allowed");
        return;
    }

    QMenu * pStyleSubMenu = m_pGenericTextContextMenu->addMenu(tr("Style"));

    ADD_ACTION_WITH_SHORTCUT(
        QKeySequence::Bold, tr("Bold"), pStyleSubMenu, textBold,
        m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        QKeySequence::Italic, tr("Italic"), pStyleSubMenu, textItalic,
        m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        QKeySequence::Underline, tr("Underline"), pStyleSubMenu, textUnderline,
        m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::Strikethrough, tr("Strikethrough"), pStyleSubMenu,
        textStrikethrough, m_isPageEditable);

    ADD_ACTION_WITH_SHORTCUT(
        ShortcutManager::Highlight, tr("Highlight"), pStyleSubMenu,
        textHighlight, m_isPageEditable);
}

void NoteEditorPrivate::setupSpellCheckerDictionariesSubMenuForGenericTextMenu()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::setupSpellCheckerDictionariesSubMenuForGenericTextMenu");

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNWARNING(
            "note_editor",
            "No spell checker was set up for "
                << "the note editor");
        return;
    }

    auto availableDictionaries = m_pSpellChecker->listAvailableDictionaries();
    if (Q_UNLIKELY(availableDictionaries.isEmpty())) {
        QNDEBUG("note_editor", "The list of available dictionaries is empty");
        return;
    }

    auto * pSpellCheckerDictionariesSubMenu =
        m_pGenericTextContextMenu->addMenu(tr("Spell checker dictionaries"));

    for (const auto & pair: qAsConst(availableDictionaries)) {
        const QString & name = pair.first;

        QAction * pAction = new QAction(name, pSpellCheckerDictionariesSubMenu);
        pAction->setEnabled(true);
        pAction->setCheckable(true);
        pAction->setChecked(pair.second);

        QObject::connect(
            pAction, &QAction::toggled, this,
            &NoteEditorPrivate::onSpellCheckerDictionaryEnabledOrDisabled);

        pSpellCheckerDictionariesSubMenu->addAction(pAction);
    }
}

void NoteEditorPrivate::rebuildRecognitionIndicesCache()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::rebuildRecognitionIndicesCache");

    m_recognitionIndicesByResourceHash.clear();

    if (Q_UNLIKELY(!m_pNote)) {
        QNTRACE("note_editor", "No note is set");
        return;
    }

    if (!m_pNote->hasResources()) {
        QNTRACE("note_editor", "The note has no resources");
        return;
    }

    QList<Resource> resources = m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = qAsConst(resources).at(i);
        if (Q_UNLIKELY(!resource.hasDataHash())) {
            QNDEBUG(
                "note_editor",
                "Skipping the resource without the data "
                    << "hash: " << resource);
            continue;
        }

        if (!resource.hasRecognitionDataBody()) {
            QNTRACE(
                "note_editor",
                "Skipping the resource without recognition "
                    << "data body");
            continue;
        }

        ResourceRecognitionIndices recoIndices(resource.recognitionDataBody());
        if (recoIndices.isNull() || !recoIndices.isValid()) {
            QNTRACE(
                "note_editor",
                "Skipping null/invalid resource recognition "
                    << "indices");
            continue;
        }

        m_recognitionIndicesByResourceHash[resource.dataHash()] = recoIndices;
    }
}

void NoteEditorPrivate::enableSpellCheck()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::enableSpellCheck");

    if (!m_pSpellChecker->isReady()) {
        QNTRACE("note_editor", "Spell checker is not ready");
        Q_EMIT spellCheckerNotReady();
        return;
    }

    refreshMisSpelledWordsList();
    applySpellCheck();
    enableDynamicSpellCheck();
}

void NoteEditorPrivate::disableSpellCheck()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::disableSpellCheck");

    m_currentNoteMisSpelledWords.clear();
    removeSpellCheck();
    disableDynamicSpellCheck();
}

void NoteEditorPrivate::refreshMisSpelledWordsList()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::refreshMisSpelledWordsList");

    if (!m_pNote) {
        QNDEBUG("note_editor", "No note is set to the editor");
        return;
    }

    m_currentNoteMisSpelledWords.clear();

    ErrorString error;
    QStringList words = m_pNote->listOfWords(&error);
    if (words.isEmpty() && !error.isEmpty()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't get the list of words from the note"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("note_editor", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    for (const auto & originalWord: qAsConst(words)) {
        QNTRACE("note_editor", "Checking word \"" << originalWord << "\"");

        QString word = originalWord;

        bool conversionResult = false;
        qint32 integerNumber = word.toInt(&conversionResult);
        if (conversionResult) {
            QNTRACE("note_editor", "Skipping the integer number " << word);
            continue;
        }

        qint64 longIntegerNumber = word.toLongLong(&conversionResult);
        if (conversionResult) {
            QNTRACE(
                "note_editor",
                "Skipping the long long integer number " << word);
            continue;
        }

        Q_UNUSED(integerNumber)
        Q_UNUSED(longIntegerNumber)

        m_stringUtils.removePunctuation(word);
        if (word.isEmpty()) {
            QNTRACE(
                "note_editor",
                "Skipping the word which becomes empty "
                    << "after stripping off the punctuation: " << originalWord);
            continue;
        }

        word = word.trimmed();

        QNTRACE(
            "note_editor",
            "Checking the spelling of \"adjusted\" word " << word);

        if (!m_pSpellChecker->checkSpell(word)) {
            QNTRACE("note_editor", "Misspelled word: \"" << word << "\"");
            word = originalWord;
            m_stringUtils.removePunctuation(word);
            word = word.trimmed();
            Q_UNUSED(m_currentNoteMisSpelledWords.insert(word))
            QNTRACE("note_editor", "Word added to the list: " << word);
        }
    }
}

void NoteEditorPrivate::applySpellCheck(const bool applyToSelection)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::applySpellCheck: "
            << "apply to selection = "
            << (applyToSelection ? "true" : "false"));

    if (m_currentNoteMisSpelledWords.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "The list of current note misspelled words is "
                << "empty, nothing to apply");
        return;
    }

    QString javascript = QStringLiteral(
        "if (window.hasOwnProperty('spellChecker')) "
        "{ spellChecker.apply");

    if (applyToSelection) {
        javascript += QStringLiteral("ToSelection");
    }

    javascript += QStringLiteral("('");
    for (const auto & word: qAsConst(m_currentNoteMisSpelledWords)) {
        javascript += word;
        javascript += QStringLiteral("', '");
    }
    javascript.chop(3); // Remove trailing ", '";
    javascript += QStringLiteral("); }");

    QNTRACE("note_editor", "Script: " << javascript);

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSpellCheckSetOrCleared));
}

void NoteEditorPrivate::removeSpellCheck()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeSpellCheck");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("if (window.hasOwnProperty('spellChecker')) "
                       "{ spellChecker.remove(); }"),
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSpellCheckSetOrCleared));
}

void NoteEditorPrivate::enableDynamicSpellCheck()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::enableDynamicSpellCheck");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("if (window.hasOwnProperty('spellChecker')) "
                       "{ spellChecker.enableDynamic(); }"));
}

void NoteEditorPrivate::disableDynamicSpellCheck()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::disableDynamicSpellCheck");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("if (window.hasOwnProperty('spellChecker')) "
                       "{ spellChecker.disableDynamic(); }"));
}

void NoteEditorPrivate::onSpellCheckSetOrCleared(
    const QVariant & dummy,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onSpellCheckSetOrCleared");

    Q_UNUSED(dummy)
    Q_UNUSED(extraData)

    GET_PAGE()
#ifndef QUENTIER_USE_QT_WEB_ENGINE
    m_htmlCachedMemory = page->mainFrame()->toHtml();
    onPageHtmlReceived(m_htmlCachedMemory);
#else
    page->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));
#endif
}

void NoteEditorPrivate::updateBodyStyle()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::updateBodyStyle");

    QString css = bodyStyleCss();
    escapeStringForJavaScript(css);

    QString javascript = QString::fromUtf8("replaceStyle('%1');").arg(css);
    QNTRACE("note_editor", "Script: " << javascript);

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onBodyStyleUpdated));
}

void NoteEditorPrivate::onBodyStyleUpdated(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onBodyStyleUpdated: " << data);

    Q_UNUSED(extraData)

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of body "
                       "style replacement from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of body "
                           "style replacement from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't replace body style"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }
}

void NoteEditorPrivate::onFontFamilyUpdated(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onFontFamilyUpdated: " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of font family "
                       "update from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of font family "
                           "update from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't update font family"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    m_htmlCachedMemory = page()->mainFrame()->toHtml();
    onPageHtmlReceived(m_htmlCachedMemory);
#else
    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));
#endif

    if (Q_UNLIKELY(extraData.empty())) {
        QNWARNING(
            "note_editor",
            "No font family in extra data in JavaScript "
                << "callback after setting font family");
        setModified();
        pushNoteContentEditUndoCommand();
        return;
    }

    QString fontFamily = extraData[0].second;
    Q_EMIT textFontFamilyChanged(fontFamily);

    auto appliedToIt = resultMap.find(QStringLiteral("appliedTo"));
    if (appliedToIt == resultMap.end()) {
        QNWARNING(
            "note_editor",
            "Can't figure out whether font family was "
                << "applied to body style or to selection, assuming the latter "
                << "option");

        setModified();
        pushNoteContentEditUndoCommand();
        return;
    }

    if (appliedToIt.value().toString() == QStringLiteral("bodyStyle")) {
        QNDEBUG("note_editor", "Font family was set to the default body style");
        return;
    }

    setModified();
    pushNoteContentEditUndoCommand();
}

void NoteEditorPrivate::onFontHeightUpdated(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onFontHeightUpdated: " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of font height "
                       "update from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of font height "
                           "update from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't update font height"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    m_htmlCachedMemory = page()->mainFrame()->toHtml();
    onPageHtmlReceived(m_htmlCachedMemory);
#else
    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));
#endif

    if (Q_UNLIKELY(extraData.empty())) {
        QNWARNING(
            "note_editor",
            "No font height in extra data in JavaScript "
                << "callback after setting font height");
        setModified();
        pushInsertHtmlUndoCommand();
        return;
    }

    int height = extraData[0].second.toInt();
    Q_EMIT textFontSizeChanged(height);

    auto appliedToIt = resultMap.find(QStringLiteral("appliedTo"));
    if (appliedToIt == resultMap.end()) {
        QNWARNING(
            "note_editor",
            "Can't figure out whether font height was "
                << "applied to body style or to selection, assuming the latter "
                << "option");

        setModified();
        pushInsertHtmlUndoCommand();
        return;
    }

    if (appliedToIt.value().toString() == QStringLiteral("bodyStyle")) {
        QNDEBUG("note_editor", "Font height was set to the default body style");
        return;
    }

    setModified();
    pushInsertHtmlUndoCommand();
}

bool NoteEditorPrivate::isNoteReadOnly() const
{
    QNDEBUG("note_editor", "NoteEditorPrivate::isNoteReadOnly");

    if (!m_pNote) {
        QNTRACE("note_editor", "No note is set to the editor");
        return true;
    }

    if (m_pNote->hasNoteRestrictions()) {
        const auto & noteRestrictions = m_pNote->noteRestrictions();
        if (noteRestrictions.noUpdateContent.isSet() &&
            noteRestrictions.noUpdateContent.ref())
        {
            QNTRACE(
                "note_editor",
                "Note has noUpdateContent restriction set "
                    << "to true");
            return true;
        }
    }

    if (!m_pNotebook) {
        QNTRACE("note_editor", "No notebook is set to the editor");
        return true;
    }

    if (!m_pNotebook->hasRestrictions()) {
        QNTRACE("note_editor", "Notebook has no restrictions");
        return false;
    }

    const auto & restrictions = m_pNotebook->restrictions();
    if (restrictions.noUpdateNotes.isSet() && restrictions.noUpdateNotes.ref())
    {
        QNTRACE("note_editor", "Restriction on note updating applies");
        return true;
    }

    return false;
}

void NoteEditorPrivate::setupAddHyperlinkDelegate(
    const quint64 hyperlinkId, const QString & presetHyperlink,
    const QString & replacementLinkText)
{
    auto * delegate =
        new AddHyperlinkToSelectedTextDelegate(*this, hyperlinkId);

    QObject::connect(
        delegate, &AddHyperlinkToSelectedTextDelegate::finished, this,
        &NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateFinished);

    QObject::connect(
        delegate, &AddHyperlinkToSelectedTextDelegate::cancelled, this,
        &NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateCancelled);

    QObject::connect(
        delegate, &AddHyperlinkToSelectedTextDelegate::notifyError, this,
        &NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateError);

    if (presetHyperlink.isEmpty()) {
        delegate->start();
    }
    else {
        delegate->startWithPresetHyperlink(
            presetHyperlink, replacementLinkText);
    }
}

#define COMMAND_TO_JS(command)                                                 \
    QString escapedCommand = command;                                          \
    escapeStringForJavaScript(escapedCommand);                                 \
    QString javascript = QString::fromUtf8("managedPageAction(\"%1\", null)")  \
                             .arg(escapedCommand);                             \
    QNDEBUG("note_editor", "JS command: " << javascript)

#define COMMAND_WITH_ARGS_TO_JS(command, args)                                 \
    QString escapedCommand = command;                                          \
    escapeStringForJavaScript(escapedCommand);                                 \
    QString escapedArgs = args;                                                \
    escapeStringForJavaScript(escapedArgs);                                    \
    QString javascript = QString::fromUtf8("managedPageAction('%1', '%2')")    \
                             .arg(escapedCommand, escapedArgs);                \
    QNDEBUG("note_editor", "JS command: " << javascript)

#ifndef QUENTIER_USE_QT_WEB_ENGINE
QVariant NoteEditorPrivate::execJavascriptCommandWithResult(
    const QString & command)
{
    COMMAND_TO_JS(command);
    QWebFrame * frame = page()->mainFrame();
    QVariant result = frame->evaluateJavaScript(javascript);
    QNTRACE(
        "note_editor",
        "Executed javascript command: " << javascript
                                        << ", result = " << result.toString());
    return result;
}

QVariant NoteEditorPrivate::execJavascriptCommandWithResult(
    const QString & command, const QString & args)
{
    COMMAND_WITH_ARGS_TO_JS(command, args);
    QWebFrame * frame = page()->mainFrame();
    QVariant result = frame->evaluateJavaScript(javascript);
    QNTRACE(
        "note_editor",
        "Executed javascript command: " << javascript
                                        << ", result = " << result.toString());
    return result;
}
#endif

void NoteEditorPrivate::execJavascriptCommand(const QString & command)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::execJavascriptCommand: " << command);

    COMMAND_TO_JS(command);
    GET_PAGE()

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onManagedPageActionFinished);

    page->executeJavaScript(javascript, callback);
}

void NoteEditorPrivate::execJavascriptCommand(
    const QString & command, const QString & args)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::execJavascriptCommand: " << command
                                                     << "; args: " << args);

    COMMAND_WITH_ARGS_TO_JS(command, args);
    GET_PAGE()

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onManagedPageActionFinished);

    page->executeJavaScript(javascript, callback);
}

void NoteEditorPrivate::initialize(
    LocalStorageManagerAsync & localStorageManager, SpellChecker & spellChecker,
    const Account & account, QThread * pBackgroundJobsThread)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::initialize");

    auto & noteEditorLocalStorageBroker =
        NoteEditorLocalStorageBroker::instance();

    noteEditorLocalStorageBroker.setLocalStorageManager(localStorageManager);

    m_pSpellChecker = &spellChecker;

    if (pBackgroundJobsThread) {
        m_pFileIOProcessorAsync->moveToThread(pBackgroundJobsThread);
    }

    setAccount(account);
}

void NoteEditorPrivate::setAccount(const Account & account)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setAccount: " << account.name());

    if (m_pAccount && (m_pAccount->type() == account.type()) &&
        (m_pAccount->name() == account.name()) &&
        (m_pAccount->id() == account.id()))
    {
        QNDEBUG(
            "note_editor",
            "The account's type, name and id were not "
                << "updated so it's the update for the account currently set "
                << "to the note editor");
        *m_pAccount = account;
        return;
    }

    clear();

    if (!m_pAccount) {
        m_pAccount.reset(new Account(account));
    }
    else {
        *m_pAccount = account;
    }

    init();
}

void NoteEditorPrivate::setUndoStack(QUndoStack * pUndoStack)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setUndoStack");

    QUENTIER_CHECK_PTR(
        "note_editor", pUndoStack,
        QStringLiteral("null undo stack passed to note editor"));

    m_pUndoStack = pUndoStack;
}

bool NoteEditorPrivate::print(
    QPrinter & printer, ErrorString & errorDescription)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::print");

    if (Q_UNLIKELY(!m_pNote)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: no note is set to the editor"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution ||
        m_pendingNoteImageResourceTemporaryFiles)
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: the note has not "
                       "been fully loaded into the editor yet, "
                       "please try again in a few seconds"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    QTextDocument doc;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QWebPage * pPage = page();
    if (Q_UNLIKELY(!pPage)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: internal error, "
                       "no note editor page"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    QWebFrame * pFrame = pPage->mainFrame();
    if (Q_UNLIKELY(!pFrame)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: internal error, no main frame was "
                       "found within the note editor page"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    /**
     * QtWebKit-based note editor uses plugins for encrypted text as well as
     * for "generic" resources; these plugins won't be printable within
     * QTextDocument, of course. For this reason need to convert
     * the HTML pieces representing these plugins to plain images
     */

    QString initialHtml = pFrame->toHtml();

    HTMLCleaner htmlCleaner;
    QString initialXml;

    QString htmlToXmlError;
    bool htmlToXmlRes =
        htmlCleaner.htmlToXml(initialHtml, initialXml, htmlToXmlError);

    if (Q_UNLIKELY(!htmlToXmlRes)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: failed to convert "
                       "the note editor's HTML to XML"));
        errorDescription.details() = htmlToXmlError;
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    auto resources = m_pNote->resources();

    QString preprocessedHtml;
    QXmlStreamReader reader(initialXml);
    QXmlStreamWriter writer(&preprocessedHtml);
    writer.setAutoFormatting(true);

    int writeElementCounter = 0;
    bool insideGenericResourceImage = false;
    bool insideEnCryptTag = false;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            ++writeElementCounter;

            QString elementName = reader.name().toString();
            auto elementAttributes = reader.attributes();

            if (!elementAttributes.hasAttribute(QStringLiteral("en-tag"))) {
                writer.writeStartElement(elementName);
                writer.writeAttributes(elementAttributes);
                continue;
            }

            QString enTagAttr =
                elementAttributes.value(QStringLiteral("en-tag")).toString();

            if (enTagAttr == QStringLiteral("en-media")) {
                if (elementName == QStringLiteral("img")) {
                    writer.writeStartElement(elementName);
                    writer.writeAttributes(elementAttributes);
                    continue;
                }

                QString typeAttr =
                    elementAttributes.value(QStringLiteral("type")).toString();

                QString hashAttr =
                    elementAttributes.value(QStringLiteral("hash")).toString();

                if (Q_UNLIKELY(hashAttr.isEmpty())) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't print note: found generic resource "
                                   "image without hash attribute"));
                    QNWARNING("note_editor", errorDescription);
                    return false;
                }

                QByteArray hashAttrByteArray =
                    QByteArray::fromHex(hashAttr.toLocal8Bit());

                QNTRACE(
                    "note_editor",
                    "Will look for resource with hash " << hashAttrByteArray);

                const Resource * pTargetResource = nullptr;
                for (const auto & resource: qAsConst(resources)) {
                    QNTRACE(
                        "note_editor",
                        "Examining resource: data hash = "
                            << (resource.hasDataHash()
                                    ? QString::fromLocal8Bit(
                                          resource.dataHash())
                                    : QStringLiteral("<null>")));

                    if (resource.hasDataHash() &&
                        (resource.dataHash() == hashAttrByteArray)) {
                        pTargetResource = &resource;
                        break;
                    }
                }

                if (Q_UNLIKELY(!pTargetResource)) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't print note: could not find one of "
                                   "resources referenced in the note text "
                                   "within the actual note's resources"));
                    errorDescription.details() = QStringLiteral("hash = ");
                    errorDescription.details() += hashAttr;
                    QNWARNING("note_editor", errorDescription);
                    return false;
                }

                QImage genericResourceImage =
                    buildGenericResourceImage(*pTargetResource);

                if (Q_UNLIKELY(genericResourceImage.isNull())) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't print note: could not generate the "
                                   "generic resource image"));
                    QNWARNING("note_editor", errorDescription);
                    return false;
                }

                writer.writeStartElement(QStringLiteral("img"));

                QXmlStreamAttributes filteredAttributes;

                filteredAttributes.append(
                    QStringLiteral("en-tag"), QStringLiteral("en-media"));

                filteredAttributes.append(QStringLiteral("type"), typeAttr);
                filteredAttributes.append(QStringLiteral("hash"), hashAttr);

                hashAttr.prepend(QStringLiteral(":"));
                filteredAttributes.append(QStringLiteral("src"), hashAttr);

                doc.addResource(
                    QTextDocument::ImageResource, QUrl(hashAttr),
                    genericResourceImage);

                writer.writeAttributes(filteredAttributes);

                insideGenericResourceImage = true;
            }
            else if (enTagAttr == QStringLiteral("en-crypt")) {
                QString enCryptImageFilePath = QStringLiteral(
                    ":/encrypted_area_icons/en-crypt/en-crypt.png");

                QImage encryptedTextImage(enCryptImageFilePath, "PNG");
                enCryptImageFilePath.prepend(QStringLiteral("qrc"));

                writer.writeStartElement(QStringLiteral("img"));

                QXmlStreamAttributes filteredAttributes;

                filteredAttributes.append(
                    QStringLiteral("type"), QStringLiteral("image/png"));

                filteredAttributes.append(
                    QStringLiteral("src"), enCryptImageFilePath);

                filteredAttributes.append(
                    QStringLiteral("en-tag"), QStringLiteral("en-crypt"));

                writer.writeAttributes(filteredAttributes);

                doc.addResource(
                    QTextDocument::ImageResource, QUrl(enCryptImageFilePath),
                    encryptedTextImage);

                insideEnCryptTag = true;
            }
            else {
                writer.writeStartElement(elementName);
                writer.writeAttributes(elementAttributes);
            }
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            if (insideGenericResourceImage) {
                insideGenericResourceImage = false;
                continue;
            }

            if (insideEnCryptTag) {
                insideEnCryptTag = false;
                continue;
            }

            if (reader.isCDATA()) {
                writer.writeCDATA(reader.text().toString());
                QNTRACE(
                    "note_editor", "Wrote CDATA: " << reader.text().toString());
            }
            else {
                writer.writeCharacters(reader.text().toString());
                QNTRACE(
                    "note_editor",
                    "Wrote characters: " << reader.text().toString());
            }
        }

        if ((writeElementCounter > 0) && reader.isEndElement()) {
            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: failed to read the XML translated "
                       "from the note editor's HTML"));
        errorDescription.details() = reader.errorString();
        errorDescription.details() += QStringLiteral("; error code = ");
        errorDescription.details() += QString::number(reader.error());
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    int bodyOpeningTagStartIndex =
        preprocessedHtml.indexOf(QStringLiteral("<body"));

    if (Q_UNLIKELY(bodyOpeningTagStartIndex < 0)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: can't find the body tag within the "
                       "preprocessed HTML prepared for conversion to "
                       "QTextDocument"));
        QNWARNING(
            "note_editor",
            errorDescription << "; preprocessed HTML: " << preprocessedHtml);
        return false;
    }

    int bodyOpeningTagEndIndex =
        preprocessedHtml.indexOf(QStringLiteral(">"), bodyOpeningTagStartIndex);

    if (Q_UNLIKELY(bodyOpeningTagEndIndex < 0)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: can't find the end of the body tag "
                       "within the preprocessed HTML prepared for conversion "
                       "to QTextDocument"));
        QNWARNING(
            "note_editor",
            errorDescription << "; preprocessed HTML: " << preprocessedHtml);
        return false;
    }

    preprocessedHtml.replace(0, bodyOpeningTagEndIndex, noteEditorPagePrefix());

    int bodyClosingTagIndex = preprocessedHtml.indexOf(
        QStringLiteral("</body>"), bodyOpeningTagEndIndex);
    if (Q_UNLIKELY(bodyClosingTagIndex < 0)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: can't find the enclosing body tag "
                       "within the preprocessed HTML prepared for conversion "
                       "to QTextDocument"));
        QNWARNING(
            "note_editor",
            errorDescription << "; preprocessed HTML: " << preprocessedHtml);
        return false;
    }

    preprocessedHtml.insert(bodyClosingTagIndex + 7, QStringLiteral("</html>"));

    preprocessedHtml.replace(
        QStringLiteral("<br></br>"), QStringLiteral("</br>"));

    m_htmlForPrinting = preprocessedHtml;

#else  // QUENTIER_USE_QT_WEB_ENGINE
    m_htmlForPrinting.resize(0);

    QTimer * pConversionTimer = new QTimer(this);
    pConversionTimer->setSingleShot(true);

    EventLoopWithExitStatus eventLoop;

    QObject::connect(
        pConversionTimer, &QTimer::timeout, &eventLoop,
        &EventLoopWithExitStatus::exitAsTimeout);

    QObject::connect(
        this, &NoteEditorPrivate::htmlReadyForPrinting, &eventLoop,
        &EventLoopWithExitStatus::exitAsSuccess);

    pConversionTimer->start(500);

    QTimer::singleShot(0, this, &NoteEditorPrivate::getHtmlForPrinting);

    Q_UNUSED(eventLoop.exec(QEventLoop::ExcludeUserInputEvents))
    auto status = eventLoop.exitStatus();

    pConversionTimer->deleteLater();
    pConversionTimer = nullptr;

    if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: failed to get the note editor page's "
                       "HTML in time"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }
#endif // QUENTIER_USE_QT_WEB_ENGINE

    ErrorString error;
    bool res = m_enmlConverter.htmlToQTextDocument(
        m_htmlForPrinting, doc, error, m_skipRulesForHtmlToEnmlConversion);

    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(QT_TR_NOOP("Can't print note"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    doc.print(&printer);
    return true;
}

bool NoteEditorPrivate::exportToPdf(
    const QString & absoluteFilePath, ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::exportToPdf: " << absoluteFilePath);

    if (Q_UNLIKELY(!m_pNote)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to pdf: no note "
                       "is set to the editor"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution ||
        m_pendingNoteImageResourceTemporaryFiles)
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to pdf: the note has not been fully "
                       "loaded into the editor yet, please try again in a few "
                       "seconds"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    QString filePath = absoluteFilePath;
    if (!filePath.endsWith(QStringLiteral(".pdf"))) {
        filePath += QStringLiteral(".pdf");
    }

    QFileInfo pdfFileInfo(filePath);
    if (pdfFileInfo.exists() && !pdfFileInfo.isWritable()) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to pdf: the output pdf file already "
                       "exists and it is not writable"));
        errorDescription.details() = filePath;
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

#if defined(QUENTIER_USE_QT_WEB_ENGINE) &&                                     \
    (QT_VERSION >= QT_VERSION_CHECK(5, 7, 0))
    QWebEnginePage * pPage = page();
    if (Q_UNLIKELY(!pPage)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to pdf: internal error, no note "
                       "editor page"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    QPageSize pageSize(QPageSize::A4);
    QMarginsF margins(20.0, 20.0, 20.0, 20.0);
    QPageLayout pageLayout(pageSize, QPageLayout::Portrait, margins);

    pPage->printToPdf(filePath, pageLayout);
    return true;
#else
    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPaperSize(QPrinter::A4);
    printer.setOutputFileName(filePath);
    return print(printer, errorDescription);
#endif // QUENTIER_USE_QT_WEB_ENGINE
}

bool NoteEditorPrivate::exportToEnex(
    const QStringList & tagNames, QString & enex,
    ErrorString & errorDescription)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::exportToEnex");

    if (Q_UNLIKELY(!m_pNote)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to enex: no note "
                       "is set to the editor"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution ||
        m_pendingNoteImageResourceTemporaryFiles)
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to enex: the note has not been fully "
                       "loaded into the editor yet, please try again in a few "
                       "seconds"));
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

    if (m_needConversionToNote) {
        // Need to save the editor's content into a note before proceeding
        QTimer * pSaveNoteTimer = new QTimer(this);
        pSaveNoteTimer->setSingleShot(true);

        EventLoopWithExitStatus eventLoop;

        QObject::connect(
            pSaveNoteTimer, &QTimer::timeout, &eventLoop,
            &EventLoopWithExitStatus::exitAsTimeout);

        QObject::connect(
            this, SIGNAL(convertedToNote(Note)), &eventLoop,
            SLOT(exitAsSuccess()));

        QObject::connect(
            this, SIGNAL(cantConvertToNote(ErrorString)), &eventLoop,
            SLOT(exitAsFailure()));

        pSaveNoteTimer->start(500);

        QTimer::singleShot(0, this, &NoteEditorPrivate::convertToNote);

        Q_UNUSED(eventLoop.exec(QEventLoop::ExcludeUserInputEvents))
        auto status = eventLoop.exitStatus();

        if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't export note to enex: failed to save "
                           "the edited note in time"));
            QNWARNING("note_editor", errorDescription);
            return false;
        }
        else if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't export note to enex: failed to save "
                           "the edited note"));
            QNWARNING("note_editor", errorDescription);
            return false;
        }

        QNDEBUG("note_editor", "Successfully saved the edited note");
    }

    QVector<Note> notes;
    notes << *m_pNote;

    notes[0].setTagLocalUids(QStringList());
    QHash<QString, QString> tagNamesByTagLocalUid;

    for (auto it = tagNames.constBegin(), end = tagNames.constEnd(); it != end;
         ++it)
    {
        QString fakeTagLocalUid = UidGenerator::Generate();
        notes[0].addTagLocalUid(fakeTagLocalUid);
        tagNamesByTagLocalUid[fakeTagLocalUid] = *it;
    }

    ENMLConverter::EnexExportTags exportTagsOption =
        (tagNames.isEmpty() ? ENMLConverter::EnexExportTags::No
                            : ENMLConverter::EnexExportTags::Yes);

    return m_enmlConverter.exportNotesToEnex(
        notes, tagNamesByTagLocalUid, exportTagsOption, enex, errorDescription);
}

QString NoteEditorPrivate::currentNoteLocalUid() const
{
    return m_noteLocalUid;
}

void NoteEditorPrivate::setCurrentNoteLocalUid(const QString & noteLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setCurrentNoteLocalUid: "
            << "note local uid = " << noteLocalUid);

    if (m_noteLocalUid == noteLocalUid) {
        QNDEBUG("note_editor", "Already have this note local uid set");
        return;
    }

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);

    clearCurrentNoteInfo();

    m_noteLocalUid = noteLocalUid;
    clearEditorContent(
        m_noteLocalUid.isEmpty() ? BlankPageKind::Initial
                                 : BlankPageKind::NoteLoading);

    if (!m_noteLocalUid.isEmpty()) {
        QNTRACE(
            "note_editor",
            "Emitting the request to find note and notebook "
                << "for note local uid " << m_noteLocalUid);
        Q_EMIT findNoteAndNotebook(m_noteLocalUid);
    }
}

void NoteEditorPrivate::clear()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::clear");

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);
    clearCurrentNoteInfo();
    clearEditorContent();
}

void NoteEditorPrivate::convertToNote()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::convertToNote");

    if (m_pendingConversionToNote) {
        QNDEBUG(
            "note_editor",
            "Already pending the conversion of "
                << "note editor page to HTML");
        return;
    }

    m_pendingConversionToNote = true;

    ErrorString error;
    bool res = htmlToNoteContent(error);
    if (!res) {
        m_pendingConversionToNote = false;
    }
}

void NoteEditorPrivate::saveNoteToLocalStorage()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::saveNoteToLocalStorage");

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't save note to local storage: "
                       "no note is loaded to the editor"));
        QNWARNING("note_editor", errorDescription);
        Q_EMIT failedToSaveNoteToLocalStorage(errorDescription, m_noteLocalUid);
        return;
    }

    if (Q_UNLIKELY(m_pNote->isInkNote())) {
        QNDEBUG(
            "note_editor",
            "Ink notes are read-only so won't save it to "
                << "the local storage, will just pretend it was saved");
        Q_EMIT noteSavedToLocalStorage(m_noteLocalUid);
        return;
    }

    if (m_pendingNoteSavingInLocalStorage) {
        QNDEBUG("note_editor", "Note is already being saved to local storage");

        if (m_needConversionToNote) {
            QNDEBUG(
                "note_editor",
                "It appears the note editor content has "
                    << "been changed since save note request was last issued; "
                       "will "
                    << "repeat the attempt to save the note after the current "
                    << "attempt is finished");

            m_shouldRepeatSavingNoteInLocalStorage = true;
        }

        return;
    }

    if (m_needConversionToNote) {
        m_pendingConversionToNoteForSavingInLocalStorage = true;
        convertToNote();
        return;
    }

    m_pendingNoteSavingInLocalStorage = true;

    QNDEBUG(
        "note_editor",
        "Emitting the request to save the note in the local "
            << "storage");

    QNTRACE("note_editor", *m_pNote);

    Q_EMIT saveNoteToLocalStorageRequest(*m_pNote);
}

void NoteEditorPrivate::setNoteTitle(const QString & noteTitle)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setNoteTitle: " << noteTitle);

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't set title to the note: no note "
                       "is set to the editor"));
        QNWARNING("note_editor", error << ", title to set: " << noteTitle);
        Q_EMIT notifyError(error);
        return;
    }

    if (!m_pNote->hasTitle() && noteTitle.isEmpty()) {
        QNDEBUG("note_editor", "Note title is still empty, nothing to do");
        return;
    }

    if (m_pNote->hasTitle() && (m_pNote->title() == noteTitle)) {
        QNDEBUG("note_editor", "Note title hasn't changed, nothing to do");
        return;
    }

    m_pNote->setTitle(noteTitle);

    if (m_pNote->hasNoteAttributes()) {
        qevercloud::NoteAttributes & attributes = m_pNote->noteAttributes();
        attributes.noteTitleQuality.clear();
    }

    setModified();
}

void NoteEditorPrivate::setTagIds(
    const QStringList & tagLocalUids, const QStringList & tagGuids)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setTagIds: tag local uids: "
            << tagLocalUids.join(QStringLiteral(", "))
            << "; tag guids: " << tagGuids.join(QStringLiteral(", ")));

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't set tags to the note: no note "
                       "is set to the editor"));
        QNWARNING(
            "note_editor",
            error << ", tag local uids: "
                  << tagLocalUids.join(QStringLiteral(", "))
                  << "; tag guids: " << tagGuids.join(QStringLiteral(", ")));
        Q_EMIT notifyError(error);
        return;
    }

    QStringList previousTagLocalUids =
        (m_pNote->hasTagLocalUids() ? m_pNote->tagLocalUids() : QStringList());

    QStringList previousTagGuids =
        (m_pNote->hasTagGuids() ? m_pNote->tagGuids() : QStringList());

    if (!tagLocalUids.isEmpty() && !tagGuids.isEmpty()) {
        if ((tagLocalUids == previousTagLocalUids) &&
            (tagGuids == previousTagGuids)) {
            QNDEBUG(
                "note_editor",
                "The list of tag ids hasn't changed, "
                    << "nothing to do");
            return;
        }

        m_pNote->setTagLocalUids(tagLocalUids);
        m_pNote->setTagGuids(tagGuids);
        setModified();

        return;
    }

    if (!tagLocalUids.isEmpty()) {
        if (tagLocalUids == previousTagLocalUids) {
            QNDEBUG(
                "note_editor",
                "The list of tag local uids hasn't changed, "
                    << "nothing to do");
            return;
        }

        m_pNote->setTagLocalUids(tagLocalUids);
        m_pNote->setTagGuids(QStringList());
        setModified();

        return;
    }

    if (!tagGuids.isEmpty()) {
        if (tagGuids == previousTagGuids) {
            QNDEBUG(
                "note_editor",
                "The list of tag guids hasn't changed, "
                    << "nothing to do");
            return;
        }

        m_pNote->setTagGuids(tagGuids);
        m_pNote->setTagLocalUids(QStringList());
        setModified();

        return;
    }

    if (previousTagLocalUids.isEmpty() && previousTagGuids.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "Tag local uids and/or guids were empty and are "
                << "still empty, nothing to do");
        return;
    }

    m_pNote->setTagLocalUids(QStringList());
    m_pNote->setTagGuids(QStringList());
    setModified();
}

void NoteEditorPrivate::updateFromNote()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::updateFromNote");
    noteToEditorContent();
}

void NoteEditorPrivate::setNoteHtml(const QString & html)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setNoteHtml");

    m_pendingConversionToNote = true;
    onPageHtmlReceived(html);

    writeNotePageFile(html);
}

void NoteEditorPrivate::addResourceToNote(const Resource & resource)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::addResourceToNote");
    QNTRACE("note_editor", resource);

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't add the resource to note: no note "
                       "is set to the editor"));
        QNWARNING("note_editor", error << ", resource to add: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (resource.hasDataHash() && resource.hasRecognitionDataBody()) {
        ResourceRecognitionIndices recoIndices(resource.recognitionDataBody());
        if (!recoIndices.isNull() && recoIndices.isValid()) {
            m_recognitionIndicesByResourceHash[resource.dataHash()] =
                recoIndices;

            QNDEBUG(
                "note_editor",
                "Set recognition indices for new resource: " << recoIndices);
        }
    }

    m_pNote->addResource(resource);
    setModified();
}

void NoteEditorPrivate::removeResourceFromNote(const Resource & resource)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeResourceFromNote");
    QNTRACE("note_editor", resource);

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't remove the resource from note: "
                       "no note is set to the editor"));
        QNWARNING("note_editor", error << ", resource to remove: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    m_pNote->removeResource(resource);
    setModified();

    if (resource.hasDataHash()) {
        auto it = m_recognitionIndicesByResourceHash.find(resource.dataHash());
        if (it != m_recognitionIndicesByResourceHash.end()) {
            Q_UNUSED(m_recognitionIndicesByResourceHash.erase(it));
            highlightRecognizedImageAreas(
                m_lastSearchHighlightedText,
                m_lastSearchHighlightedTextCaseSensitivity);
        }

        auto imageIt = m_genericResourceImageFilePathsByResourceHash.find(
            resource.dataHash());

        if (imageIt != m_genericResourceImageFilePathsByResourceHash.end()) {
            Q_UNUSED(
                m_genericResourceImageFilePathsByResourceHash.erase(imageIt))
        }
    }
}

void NoteEditorPrivate::replaceResourceInNote(const Resource & resource)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::replaceResourceInNote");
    QNTRACE("note_editor", resource);

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't replace the resource within note: "
                       "no note is set to the editor"));
        QNWARNING(
            "note_editor", error << ", replacement resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!m_pNote->hasResources())) {
        ErrorString error(
            QT_TR_NOOP("Can't replace the resource within note: "
                       "note has no resources"));
        QNWARNING(
            "note_editor", error << ", replacement resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    auto resources = m_pNote->resources();
    int resourceIndex = -1;
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const auto & currentResource = qAsConst(resources).at(i);
        if (currentResource.localUid() == resource.localUid()) {
            resourceIndex = i;
            break;
        }
    }

    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("Can't replace the resource within note: "
                       "can't find the resource to be replaced"));
        QNWARNING(
            "note_editor", error << ", replacement resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    const auto & targetResource = qAsConst(resources).at(resourceIndex);
    QByteArray previousResourceHash;
    if (targetResource.hasDataHash()) {
        previousResourceHash = targetResource.dataHash();
    }

    updateResource(targetResource.localUid(), previousResourceHash, resource);
}

void NoteEditorPrivate::setNoteResources(const QList<Resource> & resources)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setNoteResources");

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't set the resources to the note: "
                       "no note is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_pNote->setResources(resources);
    rebuildRecognitionIndicesCache();

    Q_EMIT convertedToNote(*m_pNote);
}

bool NoteEditorPrivate::isModified() const
{
    return m_needConversionToNote || m_needSavingNoteInLocalStorage;
}

bool NoteEditorPrivate::isEditorPageModified() const
{
    return m_needConversionToNote;
}

void NoteEditorPrivate::setFocusToEditor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setFocusToEditor");

#ifdef QUENTIER_USE_QT_WEB_ENGINE
#if QT_VERSION < QT_VERSION_CHECK(5, 9, 0)
    QNDEBUG(
        "note_editor",
        "Working around Qt bug "
            << "https://bugreports.qt.io/browse/QTBUG-58515");

    QWidget * pFocusWidget = qApp->focusWidget();
    if (pFocusWidget) {
        QNDEBUG("note_editor", "Removing focus from widget: " << pFocusWidget);
        pFocusWidget->clearFocus();
    }
#endif
#endif

    setFocus();

#ifdef QUENTIER_USE_QT_WEB_ENGINE
#if (QT_VERSION < QT_VERSION_CHECK(5, 9, 0)) &&                                \
    (QT_VERSION >= QT_VERSION_CHECK(5, 7, 0))
    QRect r = rect();
    QPoint bottomRight = r.bottomRight();
    bottomRight.setX(bottomRight.x() - 1);
    bottomRight.setY(bottomRight.y() - 1);

    QMouseEvent event(
        QEvent::MouseButtonPress, bottomRight, bottomRight,
        mapToGlobal(bottomRight), Qt::LeftButton,
        Qt::MouseButtons(Qt::LeftButton), Qt::NoModifier,
        Qt::MouseEventNotSynthesized);

    QNDEBUG(
        "note_editor",
        "Sending QMouseEvent to the note editor: point x = "
            << bottomRight.x() << ", y = " << bottomRight.y());

    QApplication::sendEvent(this, &event);
#endif
#endif
}

void NoteEditorPrivate::setModified()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setModified");

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG("note_editor", "No note is set to the editor");
        return;
    }

    updateLastInteractionTimestamp();

    if (!m_needConversionToNote && !m_needSavingNoteInLocalStorage) {
        m_needConversionToNote = true;
        m_needSavingNoteInLocalStorage = true;
        QNTRACE("note_editor", "Emitting noteModified signal");
        Q_EMIT noteModified();
    }
}

QString NoteEditorPrivate::noteEditorPagePath() const
{
    QNDEBUG("note_editor", "NoteEditorPrivate::noteEditorPagePath");

    if (!m_pNote) {
        QNDEBUG("note_editor", "No note is set to the editor");
        return m_noteEditorPageFolderPath + QStringLiteral("/index.html");
    }

    return m_noteEditorPageFolderPath + QStringLiteral("/") +
        m_pNote->localUid() + QStringLiteral(".html");
}

void NoteEditorPrivate::setRenameResourceDelegateSubscriptions(
    RenameResourceDelegate & delegate)
{
    QObject::connect(
        &delegate, &RenameResourceDelegate::finished, this,
        &NoteEditorPrivate::onRenameResourceDelegateFinished);

    QObject::connect(
        &delegate, &RenameResourceDelegate::notifyError, this,
        &NoteEditorPrivate::onRenameResourceDelegateError);

    QObject::connect(
        &delegate, &RenameResourceDelegate::cancelled, this,
        &NoteEditorPrivate::onRenameResourceDelegateCancelled);
}

void NoteEditorPrivate::removeSymlinksToImageResourceFile(
    const QString & resourceLocalUid)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::removeSymlinksToImageResourceFile: resource local uid = "
            << resourceLocalUid);

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG(
            "note_editor",
            "Can't remove symlinks to resource image file: "
                << "no note is set to the editor");
        return;
    }

    QString fileStorageDirPath = ResourceDataInTemporaryFileStorageManager::
                                     imageResourceFileStorageFolderPath() +
        QStringLiteral("/") + m_pNote->localUid();

    QString fileStoragePathPrefix =
        fileStorageDirPath + QStringLiteral("/") + resourceLocalUid;

    QDir dir(fileStorageDirPath);
    QNTRACE(
        "note_editor",
        "Resource file storage dir "
            << (dir.exists() ? "exists" : "doesn't exist"));

    QFileInfoList entryList =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    const int numEntries = entryList.size();
    QNTRACE(
        "note_editor",
        "Found " << numEntries << " files in the image resources folder: "
                 << QDir::toNativeSeparators(fileStorageDirPath));

    QString entryFilePath;
    for (int i = 0; i < numEntries; ++i) {
        const QFileInfo & entry = qAsConst(entryList)[i];

        if (!entry.isSymLink()) {
            continue;
        }

        entryFilePath = entry.absoluteFilePath();
        QNTRACE(
            "note_editor",
            "See if we need to remove the symlink to "
                << "resource image file " << entryFilePath);

        if (!entryFilePath.startsWith(fileStoragePathPrefix)) {
            continue;
        }

        Q_UNUSED(removeFile(entryFilePath))
    }
}

QString NoteEditorPrivate::createSymlinkToImageResourceFile(
    const QString & fileStoragePath, const QString & localUid,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::createSymlinkToImageResourceFile: file storage path = "
            << fileStoragePath << ", local uid = " << localUid);

    QString linkFilePath = fileStoragePath;
    linkFilePath.remove(linkFilePath.size() - 4, 4);
    linkFilePath += QStringLiteral("_");
    linkFilePath += QString::number(QDateTime::currentMSecsSinceEpoch());

#ifdef Q_OS_WIN
    linkFilePath += QStringLiteral(".lnk");
#else
    linkFilePath += QStringLiteral(".png");
#endif

    QNTRACE("note_editor", "Link file path = " << linkFilePath);

    removeSymlinksToImageResourceFile(localUid);

    QFile imageResourceFile(fileStoragePath);
    bool res = imageResourceFile.link(linkFilePath);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't process the image resource update: can't create "
                       "a symlink to the resource file"));
        errorDescription.details() = imageResourceFile.errorString();
        errorDescription.details() += QStringLiteral(", error code = ");
        errorDescription.details() +=
            QString::number(imageResourceFile.error());
        return {};
    }

    return linkFilePath;
}

void NoteEditorPrivate::onDropEvent(QDropEvent * pEvent)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onDropEvent");

    if (Q_UNLIKELY(!pEvent)) {
        QNWARNING("note_editor", "Null pointer to drop event was detected");
        return;
    }

    const QMimeData * pMimeData = pEvent->mimeData();
    if (Q_UNLIKELY(!pMimeData)) {
        QNWARNING(
            "note_editor",
            "Null pointer to mime data from drop event "
                << "was detected");
        return;
    }

    auto urls = pMimeData->urls();
    for (const auto & url: qAsConst(urls)) {
        if (Q_UNLIKELY(!url.isLocalFile())) {
            continue;
        }

        QString filePath = url.toLocalFile();
        dropFile(filePath);
    }

    pEvent->acceptProposedAction();
}

const Account * NoteEditorPrivate::accountPtr() const
{
    return m_pAccount.get();
}

const Resource NoteEditorPrivate::attachResourceToNote(
    const QByteArray & data, const QByteArray & dataHash,
    const QMimeType & mimeType, const QString & filename,
    const QString & sourceUrl)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::attachResourceToNote: hash = "
            << dataHash.toHex() << ", mime type = " << mimeType.name()
            << ", filename = " << filename << ", source url = " << sourceUrl);

    Resource resource;
    QString resourceLocalUid = resource.localUid();

    // Force the resource to have empty local uid for now
    resource.setLocalUid(QString());

    if (Q_UNLIKELY(!m_pNote)) {
        QNINFO(
            "note_editor",
            "Can't attach resource to note editor: no actual "
                << "note was selected");
        return resource;
    }

    // Now can return the local uid back to the resource
    resource.setLocalUid(resourceLocalUid);

    resource.setDataBody(data);

    if (!dataHash.isEmpty()) {
        resource.setDataHash(dataHash);
    }

    resource.setDataSize(data.size());
    resource.setMime(mimeType.name());
    resource.setDirty(true);

    if (!filename.isEmpty()) {
        if (!resource.hasResourceAttributes()) {
            resource.setResourceAttributes(qevercloud::ResourceAttributes());
        }

        auto & attributes = resource.resourceAttributes();
        attributes.fileName = filename;
    }

    if (!sourceUrl.isEmpty()) {
        if (!resource.hasResourceAttributes()) {
            resource.setResourceAttributes(qevercloud::ResourceAttributes());
        }

        auto & attributes = resource.resourceAttributes();
        attributes.sourceURL = sourceUrl;
    }

    resource.setNoteLocalUid(m_pNote->localUid());
    if (m_pNote->hasGuid()) {
        resource.setNoteGuid(m_pNote->guid());
    }

    m_pNote->addResource(resource);
    Q_EMIT convertedToNote(*m_pNote);

    return resource;
}

template <typename T>
QString NoteEditorPrivate::composeHtmlTable(
    const T width, const T singleColumnWidth, const int rows, const int columns,
    const bool relative)
{
    // Table header
    QString htmlTable = QStringLiteral(
        "<div><table style=\"border-collapse: "
        "collapse; margin-left: 0px; "
        "table-layout: fixed; width: ");

    htmlTable += QString::number(width);
    if (relative) {
        htmlTable += QStringLiteral("%");
    }
    else {
        htmlTable += QStringLiteral("px");
    }
    htmlTable += QStringLiteral(";\" ><tbody>");

    for (int i = 0; i < rows; ++i) {
        // Row header
        htmlTable += QStringLiteral("<tr>");

        for (int j = 0; j < columns; ++j) {
            // Column header
            htmlTable += QStringLiteral(
                "<td style=\"border: 1px solid "
                "rgb(219, 219, 219); padding: 10 px; "
                "margin: 0px; width: ");

            htmlTable += QString::number(singleColumnWidth);
            if (relative) {
                htmlTable += QStringLiteral("%");
            }
            else {
                htmlTable += QStringLiteral("px");
            }
            htmlTable += QStringLiteral(";\">");

            // Blank line to preserve the size
            htmlTable += QStringLiteral("<div><br></div>");

            // End column
            htmlTable += QStringLiteral("</td>");
        }

        // End row
        htmlTable += QStringLiteral("</tr>");
    }

    // End table
    htmlTable += QStringLiteral("</tbody></table></div>");
    return htmlTable;
}

void NoteEditorPrivate::undo()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::undo");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't perform undo"))

    if (m_pUndoStack->canUndo()) {
        m_pUndoStack->undo();
        setModified();
    }
}

void NoteEditorPrivate::redo()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::redo");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't perform redo"))

    if (m_pUndoStack->canRedo()) {
        m_pUndoStack->redo();
        setModified();
    }
}

void NoteEditorPrivate::undoPageAction()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::undoPageAction");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't undo page action"))
    GET_PAGE()

    page->executeJavaScript(
        QStringLiteral("textEditingUndoRedoManager.undo()"));
    setModified();
    updateJavaScriptBindings();
}

void NoteEditorPrivate::redoPageAction()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::redoPageAction");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't redo page action"))
    GET_PAGE()

    page->executeJavaScript(
        QStringLiteral("textEditingUndoRedoManager.redo()"));
    setModified();
    updateJavaScriptBindings();
}

void NoteEditorPrivate::flipEnToDoCheckboxState(const quint64 enToDoIdNumber)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::flipEnToDoCheckboxState: " << enToDoIdNumber);

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't flip the todo checkbox state"))
    GET_PAGE()

    QString javascript = QString::fromUtf8("flipEnToDoCheckboxState(%1);")
                             .arg(QString::number(enToDoIdNumber));

    page->executeJavaScript(javascript);
    setModified();
}

void NoteEditorPrivate::updateLastInteractionTimestamp()
{
    m_lastInteractionTimestamp = QDateTime::currentMSecsSinceEpoch();
}

qint64 NoteEditorPrivate::noteResourcesSize() const
{
    QNTRACE("note_editor", "NoteEditorPrivate::noteResourcesSize");

    if (Q_UNLIKELY(!m_pNote)) {
        QNTRACE("note_editor", "No note - returning zero");
        return qint64(0);
    }

    if (Q_UNLIKELY(!m_pNote->hasResources())) {
        QNTRACE("note_editor", "Note has no resources - returning zero");
        return qint64(0);
    }

    qint64 size = 0;
    QList<Resource> resources = m_pNote->resources();
    for (const auto & resource: qAsConst(resources)) {
        QNTRACE(
            "note_editor",
            "Computing size contributions for resource: " << resource);

        if (resource.hasDataSize()) {
            size += resource.dataSize();
        }

        if (resource.hasAlternateDataSize()) {
            size += resource.alternateDataSize();
        }

        if (resource.hasRecognitionDataSize()) {
            size += resource.recognitionDataSize();
        }
    }

    QNTRACE("note_editor", "Computed note resources size: " << size);
    return size;
}

qint64 NoteEditorPrivate::noteContentSize() const
{
    if (Q_UNLIKELY(!m_pNote)) {
        return qint64(0);
    }

    if (Q_UNLIKELY(!m_pNote->hasContent())) {
        return qint64(0);
    }

    return static_cast<qint64>(m_pNote->content().size());
}

qint64 NoteEditorPrivate::noteSize() const
{
    return noteContentSize() + noteResourcesSize();
}

void NoteEditorPrivate::onSpellCheckCorrectionAction()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onSpellCheckCorrectionAction");

    if (!m_spellCheckerEnabled) {
        QNDEBUG("note_editor", "Not enabled, won't do anything");
        return;
    }

    auto * pAction = qobject_cast<QAction *>(sender());
    if (Q_UNLIKELY(!pAction)) {
        ErrorString error(
            QT_TR_NOOP("Can't get the action which has toggled "
                       "the spelling correction"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString correction = pAction->text();
    if (Q_UNLIKELY(correction.isEmpty())) {
        QNWARNING("note_editor", "No correction specified");
        return;
    }

    correction.remove(QStringLiteral("&"));

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("spellChecker.correctSpelling('") + correction +
            QStringLiteral("');"),
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSpellCheckCorrectionActionDone));
}

void NoteEditorPrivate::onSpellCheckIgnoreWordAction()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onSpellCheckIgnoreWordAction");

    if (!m_spellCheckerEnabled) {
        QNDEBUG("note_editor", "Not enabled, won't do anything");
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNDEBUG("note_editor", "Spell checker is null, won't do anything");
        return;
    }

    m_pSpellChecker->ignoreWord(m_lastMisSpelledWord);
    m_currentNoteMisSpelledWords.remove(m_lastMisSpelledWord);
    applySpellCheck();

    auto * pCommand = new SpellCheckIgnoreWordUndoCommand(
        *this, m_lastMisSpelledWord, m_pSpellChecker);

    QObject::connect(
        pCommand, &SpellCheckIgnoreWordUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::onSpellCheckAddWordToUserDictionaryAction()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSpellCheckAddWordToUserDictionaryAction");

    if (!m_spellCheckerEnabled) {
        QNDEBUG("note_editor", "Not enabled, won't do anything");
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNDEBUG("note_editor", "Spell checker is null, won't do anything");
        return;
    }

    m_pSpellChecker->addToUserWordlist(m_lastMisSpelledWord);
    m_currentNoteMisSpelledWords.remove(m_lastMisSpelledWord);
    applySpellCheck();

    auto * pCommand = new SpellCheckAddToUserWordListUndoCommand(
        *this, m_lastMisSpelledWord, m_pSpellChecker);

    QObject::connect(
        pCommand, &SpellCheckAddToUserWordListUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::onSpellCheckCorrectionActionDone(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSpellCheckCorrectionActionDone: " << data);

    Q_UNUSED(extraData)

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of spelling "
                       "correction from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of spelling "
                           "correction from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't correct spelling"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto * pCommand = new SpellCorrectionUndoCommand(
        *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSpellCheckCorrectionUndoRedoFinished));

    QObject::connect(
        pCommand, &SpellCorrectionUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    applySpellCheck();
    convertToNote();
}

void NoteEditorPrivate::onSpellCheckCorrectionUndoRedoFinished(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSpellCheckCorrectionUndoRedoFinished");

    Q_UNUSED(extraData)

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of spelling "
                       "correction undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of spelling "
                           "correction undo/redo from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't undo/redo the spelling correction"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    convertToNote();
}

void NoteEditorPrivate::onSpellCheckerDynamicHelperUpdate(QStringList words)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onSpellCheckerDynamicHelperUpdate: "
            << words.join(QStringLiteral(";")));

    if (!m_spellCheckerEnabled) {
        QNTRACE("note_editor", "No spell checking is enabled, nothing to do");
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNDEBUG("note_editor", "Spell checker is null, won't do anything");
        return;
    }

    for (auto word: qAsConst(words)) {
        word = word.trimmed();
        m_stringUtils.removePunctuation(word);

        if (m_pSpellChecker->checkSpell(word)) {
            QNTRACE("note_editor", "No misspelling detected");
            continue;
        }

        Q_UNUSED(m_currentNoteMisSpelledWords.insert(word))
    }

    QNTRACE(
        "note_editor",
        "Current note's misspelled words: " << m_currentNoteMisSpelledWords);

    applySpellCheck(/* apply to selection = */ true);
}

void NoteEditorPrivate::onSpellCheckerReady()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onSpellCheckerReady");

    QObject::disconnect(
        m_pSpellChecker, &SpellChecker::ready, this,
        &NoteEditorPrivate::onSpellCheckerReady);

    if (m_spellCheckerEnabled) {
        enableSpellCheck();
    }
    else {
        disableSpellCheck();
    }

    Q_EMIT spellCheckerReady();
}

void NoteEditorPrivate::onImageResourceResized(bool pushUndoCommand)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onImageResourceResized: "
            << "push undo command = " << (pushUndoCommand ? "true" : "false"));

    if (pushUndoCommand) {
        auto * pCommand = new ImageResizeUndoCommand(*this);

        QObject::connect(
            pCommand, &ImageResizeUndoCommand::notifyError, this,
            &NoteEditorPrivate::onUndoCommandError);

        m_pUndoStack->push(pCommand);
    }

    convertToNote();
}

void NoteEditorPrivate::copy()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::copy");
    GET_PAGE()
    page->triggerAction(WebPage::Copy);
}

void NoteEditorPrivate::paste()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::paste");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't paste"))
    GET_PAGE()

    QClipboard * pClipboard = QApplication::clipboard();
    if (Q_UNLIKELY(!pClipboard)) {
        QNWARNING(
            "note_editor",
            "Can't access the application clipboard "
                << "to analyze the pasted content");
        execJavascriptCommand(QStringLiteral("insertText"));
        setModified();
        return;
    }

    const QMimeData * pMimeData = pClipboard->mimeData(QClipboard::Clipboard);
    if (pMimeData) {
        QNTRACE(
            "note_editor",
            "Mime data to paste: "
                << pMimeData << "\nMime data has html: "
                << (pMimeData->hasHtml() ? "true" : "false")
                << ", html: " << pMimeData->html() << ", mime data has text: "
                << (pMimeData->hasText() ? "true" : "false")
                << ", text: " << pMimeData->text() << ", mime data has image: "
                << (pMimeData->hasImage() ? "true" : "false"));

        if (pMimeData->hasImage()) {
            pasteImageData(*pMimeData);
            return;
        }

        if (pMimeData->hasHtml()) {
            QString html = pMimeData->html();
            QNDEBUG("note_editor", "HTML from mime data: " << html);

            auto * pInsertHtmlDelegate = new InsertHtmlDelegate(
                html, *this, m_enmlConverter,
                m_pResourceDataInTemporaryFileStorageManager,
                m_resourceFileStoragePathsByResourceLocalUid, m_resourceInfo,
                this);

            QObject::connect(
                pInsertHtmlDelegate, &InsertHtmlDelegate::finished, this,
                &NoteEditorPrivate::onInsertHtmlDelegateFinished);

            QObject::connect(
                pInsertHtmlDelegate, &InsertHtmlDelegate::notifyError, this,
                &NoteEditorPrivate::onInsertHtmlDelegateError);

            pInsertHtmlDelegate->start();
            return;
        }
    }
    else {
        QNDEBUG(
            "note_editor",
            "Unable to retrieve the mime data from "
                << "the clipboard");
    }

    QString textToPaste = pClipboard->text();
    QNTRACE("note_editor", "Text to paste: " << textToPaste);

    if (textToPaste.isEmpty()) {
        QNDEBUG("note_editor", "The text to paste is empty");
        return;
    }

    bool shouldBeHyperlink =
        textToPaste.startsWith(QStringLiteral("http://")) ||
        textToPaste.startsWith(QStringLiteral("https://")) ||
        textToPaste.startsWith(QStringLiteral("mailto:")) ||
        textToPaste.startsWith(QStringLiteral("ftp://"));

    bool shouldBeAttachment = textToPaste.startsWith(QStringLiteral("file://"));

    bool shouldBeInAppLink =
        textToPaste.startsWith(QStringLiteral("evernote://"));

    if (!shouldBeHyperlink && !shouldBeAttachment && !shouldBeInAppLink) {
        QNTRACE(
            "note_editor",
            "The pasted text doesn't appear to be a url "
                << "of hyperlink or attachment");
        execJavascriptCommand(QStringLiteral("insertText"), textToPaste);
        return;
    }

    QUrl url(textToPaste);
    if (shouldBeAttachment) {
        if (!url.isValid()) {
            QNTRACE(
                "note_editor",
                "The pasted text seemed like file url but "
                    << "the url isn't valid after all, fallback to simple "
                       "paste");
            execJavascriptCommand(QStringLiteral("insertText"), textToPaste);
            setModified();
        }
        else {
            dropFile(url.toLocalFile());
        }

        return;
    }

    if (!url.isValid()) {
        url.setScheme(QStringLiteral("evernote"));
    }

    if (!url.isValid()) {
        QNDEBUG("note_editor", "It appears we don't paste a url");
        execJavascriptCommand(QStringLiteral("insertText"), textToPaste);
        setModified();
        return;
    }

    QNDEBUG(
        "note_editor",
        "Was able to create the url from pasted text, "
            << "inserting a hyperlink");

    if (shouldBeInAppLink) {
        QString userId, shardId, noteGuid;
        ErrorString errorDescription;
        bool res = parseInAppLink(
            textToPaste, userId, shardId, noteGuid, errorDescription);

        if (!res) {
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        if (Q_UNLIKELY(!checkGuid(noteGuid))) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't insert in-app note link: "
                           "note guid is invalid"));
            errorDescription.details() = noteGuid;
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QNTRACE(
            "note_editor",
            "Parsed in-app note link: user id = "
                << userId << ", shard id = " << shardId
                << ", note guid = " << noteGuid);

        Q_EMIT inAppNoteLinkPasteRequested(
            textToPaste, userId, shardId, noteGuid);

        return;
    }

    textToPaste = url.toString(QUrl::FullyEncoded);

    quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
    setupAddHyperlinkDelegate(hyperlinkId, textToPaste);
}

void NoteEditorPrivate::pasteUnformatted()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::pasteUnformatted");
    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't paste the unformatted text"));

    QClipboard * pClipboard = QApplication::clipboard();
    if (Q_UNLIKELY(!pClipboard)) {
        QNWARNING(
            "note_editor",
            "Can't access the application clipboard "
                << "to analyze the pasted content");

        execJavascriptCommand(QStringLiteral("insertText"));
        setModified();
        return;
    }

    QString textToPaste = pClipboard->text();
    QNTRACE("note_editor", "Text to paste: " << textToPaste);
    if (textToPaste.isEmpty()) {
        return;
    }

    execJavascriptCommand(QStringLiteral("insertText"), textToPaste);
    setModified();
}

void NoteEditorPrivate::selectAll()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::selectAll");

    GET_PAGE()
    page->triggerAction(WebPage::SelectAll);
}

void NoteEditorPrivate::formatSelectionAsSourceCode()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::formatSelectionAsSourceCode");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("sourceCodeFormatter.format()"),
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onSelectionFormattedAsSourceCode));
}

void NoteEditorPrivate::fontMenu()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::fontMenu");

    bool fontWasChosen = false;
    QFont chosenFont = QFontDialog::getFont(&fontWasChosen, m_font, this);
    if (!fontWasChosen) {
        return;
    }

    setFont(chosenFont);

    textBold();
    Q_EMIT textBoldState(chosenFont.bold());

    textItalic();
    Q_EMIT textItalicState(chosenFont.italic());

    textUnderline();
    Q_EMIT textUnderlineState(chosenFont.underline());

    textStrikethrough();
    Q_EMIT textStrikethroughState(chosenFont.strikeOut());
}

void NoteEditorPrivate::cut()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::cut");

    GET_PAGE()
    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't cut note content"))

    // NOTE: managed action can't properly copy the current selection into
    // the clipboard on all platforms, so triggering copy action first
    page->triggerAction(WebPage::Copy);

    execJavascriptCommand(QStringLiteral("cut"));
    setModified();
}

void NoteEditorPrivate::textBold()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::textBold");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't toggle bold text"))
    execJavascriptCommand(QStringLiteral("bold"));
    setModified();
}

void NoteEditorPrivate::textItalic()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::textItalic");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't toggle italic text"))
    execJavascriptCommand(QStringLiteral("italic"));
    setModified();
}

void NoteEditorPrivate::textUnderline()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::textUnderline");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't toggle underline text"))
    execJavascriptCommand(QStringLiteral("underline"));
    setModified();
}

void NoteEditorPrivate::textStrikethrough()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::textStrikethrough");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't toggle strikethrough text"))
    execJavascriptCommand(QStringLiteral("strikethrough"));
    setModified();
}

void NoteEditorPrivate::textHighlight()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::textHighlight");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't highlight text"))
    setBackgroundColor(QColor(255, 255, 127));
    setModified();
}

void NoteEditorPrivate::alignLeft()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::alignLeft");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't justify the text to the left"))
    execJavascriptCommand(QStringLiteral("justifyleft"));
    setModified();
}

void NoteEditorPrivate::alignCenter()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::alignCenter");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't justify the text to the center"))
    execJavascriptCommand(QStringLiteral("justifycenter"));
    setModified();
}

void NoteEditorPrivate::alignRight()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::alignRight");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't justify the text to the right"))
    execJavascriptCommand(QStringLiteral("justifyright"));
    setModified();
}

void NoteEditorPrivate::alignFull()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::alignFull");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't do full text justification"))
    execJavascriptCommand(QStringLiteral("justifyfull"));
    setModified();
}

QString NoteEditorPrivate::selectedText() const
{
    return page()->selectedText();
}

bool NoteEditorPrivate::hasSelection() const
{
    return page()->hasSelection();
}

void NoteEditorPrivate::findNext(
    const QString & text, const bool matchCase) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::findNext: " << text << "; match case = "
                                        << (matchCase ? "true" : "false"));

    findText(text, matchCase);
}

void NoteEditorPrivate::findPrevious(
    const QString & text, const bool matchCase) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::findPrevious: " << text << "; match case = "
                                            << (matchCase ? "true" : "false"));

    findText(text, matchCase, /* search backward = */ true);
}

void NoteEditorPrivate::replace(
    const QString & textToReplace, const QString & replacementText,
    const bool matchCase)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::replace: text to replace = "
            << textToReplace << "; replacement text = " << replacementText
            << "; match case = " << (matchCase ? "true" : "false"));

    GET_PAGE()
    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't replace text"))

    QString escapedTextToReplace = textToReplace;
    escapeStringForJavaScript(escapedTextToReplace);

    QString escapedReplacementText = replacementText;
    escapeStringForJavaScript(escapedReplacementText);

    QString javascript =
        QString::fromUtf8("findReplaceManager.replace('%1', '%2', %3);")
            .arg(
                escapedTextToReplace, escapedReplacementText,
                (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    page->executeJavaScript(javascript, ReplaceCallback(this));

    auto * pCommand = new ReplaceUndoCommand(
        textToReplace, matchCase, *this, ReplaceCallback(this));

    QObject::connect(
        pCommand, &ReplaceUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    setSearchHighlight(textToReplace, matchCase, /* force = */ true);
    findNext(textToReplace, matchCase);
}

void NoteEditorPrivate::replaceAll(
    const QString & textToReplace, const QString & replacementText,
    const bool matchCase)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::replaceAll: text to replace = "
            << textToReplace << "; replacement text = " << replacementText
            << "; match case = " << (matchCase ? "true" : "false"));

    GET_PAGE()
    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't replace all occurrences"))

    QString escapedTextToReplace = textToReplace;
    escapeStringForJavaScript(escapedTextToReplace);

    QString escapedReplacementText = replacementText;
    escapeStringForJavaScript(escapedReplacementText);

    QString javascript =
        QString::fromUtf8("findReplaceManager.replaceAll('%1', '%2', %3);")
            .arg(
                escapedTextToReplace, escapedReplacementText,
                (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    page->executeJavaScript(javascript, ReplaceCallback(this));

    auto * pCommand = new ReplaceAllUndoCommand(
        textToReplace, matchCase, *this, ReplaceCallback(this));

    QObject::connect(
        pCommand, &ReplaceAllUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);

    setSearchHighlight(textToReplace, matchCase, /* force = */ true);
}

void NoteEditorPrivate::onReplaceJavaScriptDone(const QVariant & data)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onReplaceJavaScriptDone");

    Q_UNUSED(data)

    setModified();
    convertToNote();
}

void NoteEditorPrivate::insertToDoCheckbox()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertToDoCheckbox");

    GET_PAGE()
    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert checkbox"))

    QString javascript =
        QString::fromUtf8("toDoCheckboxAutomaticInserter.insertToDo(%1);")
            .arg(m_lastFreeEnToDoIdNumber++);

    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onToDoCheckboxInserted));
}

void NoteEditorPrivate::insertInAppNoteLink(
    const QString & userId, const QString & shardId, const QString & noteGuid,
    const QString & linkText)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::insertInAppNoteLink: user id = "
            << userId << ", shard id = " << shardId
            << ", note guid = " << noteGuid);

    QString urlString = QStringLiteral("evernote:///view/") + userId +
        QStringLiteral("/") + shardId + QStringLiteral("/") + noteGuid +
        QStringLiteral("/") + noteGuid;

    quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
    setupAddHyperlinkDelegate(hyperlinkId, urlString, linkText);
}

void NoteEditorPrivate::setSpellcheck(const bool enabled)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setSpellcheck: enabled = "
            << (enabled ? "true" : "false"));

    if (m_spellCheckerEnabled == enabled) {
        QNTRACE("note_editor", "Spell checker enabled flag didn't change");
        return;
    }

    m_spellCheckerEnabled = enabled;
    if (m_spellCheckerEnabled) {
        enableSpellCheck();
    }
    else {
        disableSpellCheck();
    }
}

bool NoteEditorPrivate::spellCheckEnabled() const
{
    return m_spellCheckerEnabled;
}

void NoteEditorPrivate::setFont(const QFont & font)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setFont: "
            << font.family() << ", point size = " << font.pointSize()
            << ", previous font family = " << m_font.family()
            << ", previous font point size = " << m_font.pointSize());

    if (m_font.family() == font.family()) {
        QNTRACE("note_editor", "Font family hasn't changed, nothing to to do");
        return;
    }

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't change font"))

    m_font = font;
    QString fontFamily = font.family();

    QString javascript =
        QString::fromUtf8("setFontFamily('%1');").arg(fontFamily);

    QNTRACE("note_editor", "Script: " << javascript);

    QVector<std::pair<QString, QString>> extraData;
    extraData.push_back(
        std::make_pair(QStringLiteral("fontFamily"), fontFamily));

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onFontFamilyUpdated, extraData));
}

void NoteEditorPrivate::setFontHeight(const int height)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setFontHeight: " << height);

    if (height <= 0) {
        ErrorString error(QT_TR_NOOP("Detected incorrect font size"));
        error.details() = QString::number(height);
        QNINFO("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't change the font height"))

    m_font.setPointSize(height);
    QString javascript = QString::fromUtf8("setFontSize('%1');").arg(height);
    QNTRACE("note_editor", "Script: " << javascript);

    QVector<std::pair<QString, QString>> extraData;
    extraData.push_back(
        std::make_pair(QStringLiteral("fontSize"), QString::number(height)));

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onFontHeightUpdated, extraData));
}

void NoteEditorPrivate::setFontColor(const QColor & color)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setFontColor: "
            << color.name() << ", rgb: " << QString::number(color.rgb(), 16));

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't set the font color"))

    if (!color.isValid()) {
        ErrorString error(QT_TR_NOOP("Detected invalid font color"));
        error.details() = color.name();
        QNINFO("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    execJavascriptCommand(QStringLiteral("foreColor"), color.name());

    if (hasSelection()) {
        setModified();
    }
}

void NoteEditorPrivate::setBackgroundColor(const QColor & color)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setBackgroundColor: "
            << color.name() << ", rgb: " << QString::number(color.rgb(), 16));

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't set the background color"))

    if (!color.isValid()) {
        ErrorString error(QT_TR_NOOP("Detected invalid background color"));
        error.details() = color.name();
        QNINFO("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    execJavascriptCommand(QStringLiteral("hiliteColor"), color.name());

    if (hasSelection()) {
        setModified();
    }
}

QPalette NoteEditorPrivate::defaultPalette() const
{
    QPalette pal = palette();

    if (m_pPalette) {
        QColor fontColor = m_pPalette->color(QPalette::WindowText);
        if (fontColor.isValid()) {
            pal.setColor(QPalette::WindowText, fontColor);
        }

        QColor backgroundColor = m_pPalette->color(QPalette::Base);
        if (backgroundColor.isValid()) {
            pal.setColor(QPalette::Base, backgroundColor);
        }

        QColor highlightColor = m_pPalette->color(QPalette::Highlight);
        if (highlightColor.isValid()) {
            pal.setColor(QPalette::Highlight, highlightColor);
        }

        QColor highlightedTextColor =
            m_pPalette->color(QPalette::HighlightedText);

        if (highlightedTextColor.isValid()) {
            pal.setColor(QPalette::HighlightedText, highlightedTextColor);
        }
    }

    return pal;
}

void NoteEditorPrivate::setDefaultPalette(const QPalette & pal)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setDefaultPalette");

    if (!m_pPalette) {
        m_pPalette.reset(new QPalette(pal));
    }
    else {
        if (*m_pPalette == pal) {
            QNTRACE("note_editor", "Palette did not change");
            return;
        }

        *m_pPalette = pal;
    }

    if (Q_UNLIKELY(!m_pNote)) {
        return;
    }

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution)
    {
        m_pendingBodyStyleUpdate = true;
        return;
    }

    updateBodyStyle();
}

const QFont * NoteEditorPrivate::defaultFont() const
{
    return m_pDefaultFont.get();
}

void NoteEditorPrivate::setDefaultFont(const QFont & font)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setDefaultFont: " << font.toString());

    if (m_pDefaultFont && *m_pDefaultFont == font) {
        QNDEBUG("note_editor", "Font is already set");
        return;
    }

    if (!m_pDefaultFont) {
        m_pDefaultFont.reset(new QFont(font));
    }
    else {
        *m_pDefaultFont = font;
    }

    if (Q_UNLIKELY(!m_pNote)) {
        return;
    }

    if (m_pendingNotePageLoad || m_pendingIndexHtmlWritingToFile ||
        m_pendingJavaScriptExecution)
    {
        m_pendingBodyStyleUpdate = true;
        return;
    }

    updateBodyStyle();
}

void NoteEditorPrivate::insertHorizontalLine()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertHorizontalLine");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a horizontal line"))
    execJavascriptCommand(QStringLiteral("insertHorizontalRule"));
    setModified();
}

void NoteEditorPrivate::increaseFontSize()
{
    changeFontSize(/* increase = */ true);
}

void NoteEditorPrivate::decreaseFontSize()
{
    changeFontSize(/* decrease = */ false);
}

void NoteEditorPrivate::increaseIndentation()
{
    changeIndentation(/* increase = */ true);
}

void NoteEditorPrivate::decreaseIndentation()
{
    changeIndentation(/* increase = */ false);
}

void NoteEditorPrivate::insertBulletedList()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertBulletedList");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert an unordered list"))
    execJavascriptCommand(QStringLiteral("insertUnorderedList"));
    setModified();
}

void NoteEditorPrivate::insertNumberedList()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertNumberedList");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a numbered list"))
    execJavascriptCommand(QStringLiteral("insertOrderedList"));
    setModified();
}

void NoteEditorPrivate::insertTableDialog()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertTableDialog");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a table"))
    Q_EMIT insertTableDialogRequested();
}

#define CHECK_NUM_COLUMNS()                                                    \
    if (columns <= 0) {                                                        \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "NoteEditorPrivate",                                               \
            "Detected attempt to insert a "                                    \
            "table with negative or zero "                                     \
            "number of columns"));                                             \
        error.details() = QString::number(columns);                            \
        QNWARNING("note_editor", error);                                       \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

#define CHECK_NUM_ROWS()                                                       \
    if (rows <= 0) {                                                           \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "NoteEditorPrivate",                                               \
            "Detected attempt to insert a "                                    \
            "table with negative or zero "                                     \
            "number of rows"));                                                \
        error.details() = QString::number(rows);                               \
        QNWARNING("note_editor", error);                                       \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

void NoteEditorPrivate::insertFixedWidthTable(
    const int rows, const int columns, const int widthInPixels)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::insertFixedWidthTable: rows = "
            << rows << ", columns = " << columns
            << ", width in pixels = " << widthInPixels);

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a fixed width table"))

    CHECK_NUM_COLUMNS();
    CHECK_NUM_ROWS();

    int pageWidth = geometry().width();
    if (widthInPixels > 2 * pageWidth) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, width is too large "
                       "(more than twice the page width)"));
        error.details() = QString::number(widthInPixels);
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }
    else if (widthInPixels <= 0) {
        ErrorString error(QT_TR_NOOP("Can't insert table, bad width"));
        error.details() = QString::number(widthInPixels);
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    int singleColumnWidth = widthInPixels / columns;
    if (singleColumnWidth == 0) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, bad width for specified "
                       "number of columns (single column width is zero)"));
        error.details() = QString::number(widthInPixels);
        error.details() += QStringLiteral(", ");
        error.details() += QString::number(columns);
        error.details() += QStringLiteral("columns");
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString htmlTable = composeHtmlTable(
        widthInPixels, singleColumnWidth, rows, columns,
        /* relative = */ false);

    execJavascriptCommand(QStringLiteral("insertHTML"), htmlTable);
    setModified();
    updateColResizableTableBindings();
}

void NoteEditorPrivate::insertRelativeWidthTable(
    const int rows, const int columns, const double relativeWidth)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::insertRelativeWidthTable: "
            << "rows = " << rows << ", columns = " << columns
            << ", relative width = " << relativeWidth);

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a relative width table"))

    CHECK_NUM_COLUMNS();
    CHECK_NUM_ROWS();

    if (relativeWidth <= 0.01) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, relative width is too small"));
        error.details() = QString::number(relativeWidth);
        error.details() += QStringLiteral("%");
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }
    else if (relativeWidth > 100.0 + 1.0e-9) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, relative width is too large"));
        error.details() = QString::number(relativeWidth);
        error.details() += QStringLiteral("%");
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    double singleColumnWidth = relativeWidth / columns;

    QString htmlTable = composeHtmlTable(
        relativeWidth, singleColumnWidth, rows, columns,
        /* relative = */ true);

    execJavascriptCommand(QStringLiteral("insertHTML"), htmlTable);
    setModified();
    updateColResizableTableBindings();
}

void NoteEditorPrivate::insertTableRow()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertTableRow");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a table row"))

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onTableActionDone);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("tableManager.insertRow();"), callback);

    pushTableActionUndoCommand(tr("Insert row"), callback);
}

void NoteEditorPrivate::insertTableColumn()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::insertTableColumn");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't insert a table column"))

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onTableActionDone);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("tableManager.insertColumn();"), callback);

    pushTableActionUndoCommand(tr("Insert column"), callback);
}

void NoteEditorPrivate::removeTableRow()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeTableRow");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't remove the table row"))

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onTableActionDone);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("tableManager.removeRow();"), callback);

    pushTableActionUndoCommand(tr("Remove row"), callback);
}

void NoteEditorPrivate::removeTableColumn()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeTableColumn");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't remove the table column"))

    NoteEditorCallbackFunctor<QVariant> callback(
        this, &NoteEditorPrivate::onTableActionDone);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("tableManager.removeColumn();"), callback);

    pushTableActionUndoCommand(tr("Remove column"), callback);
}

void NoteEditorPrivate::addAttachmentDialog()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::addAttachmentDialog");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't add an attachment"))

    CHECK_ACCOUNT(QT_TR_NOOP("Internal error, can't add an attachment"))

    QString addAttachmentInitialFolderPath;
    ApplicationSettings appSettings(*m_pAccount, NOTE_EDITOR_SETTINGS_NAME);

    QVariant lastAttachmentAddLocation =
        appSettings.value(NOTE_EDITOR_LAST_ATTACHMENT_ADD_LOCATION_KEY);

    if (!lastAttachmentAddLocation.isNull() &&
        lastAttachmentAddLocation.isValid()) {
        QNTRACE(
            "note_editor",
            "Found last attachment add location: "
                << lastAttachmentAddLocation);
        QFileInfo lastAttachmentAddDirInfo(
            lastAttachmentAddLocation.toString());
        if (!lastAttachmentAddDirInfo.exists()) {
            QNTRACE(
                "note_editor",
                "Cached last attachment add directory does "
                    << "not exist");
        }
        else if (!lastAttachmentAddDirInfo.isDir()) {
            QNTRACE(
                "note_editor",
                "Cached last attachment add directory path "
                    << "is not a directory really");
        }
        else if (!lastAttachmentAddDirInfo.isWritable()) {
            QNTRACE(
                "note_editor",
                "Cached last attachment add directory path "
                    << "is not writable");
        }
        else {
            addAttachmentInitialFolderPath =
                lastAttachmentAddDirInfo.absolutePath();
        }
    }

    QString absoluteFilePath = QFileDialog::getOpenFileName(
        this, tr("Add attachment") + QStringLiteral("..."),
        addAttachmentInitialFolderPath);

    if (absoluteFilePath.isEmpty()) {
        QNTRACE("note_editor", "User cancelled adding the attachment");
        return;
    }

    QNTRACE(
        "note_editor",
        "Absolute file path of chosen attachment: " << absoluteFilePath);

    QFileInfo fileInfo(absoluteFilePath);
    QString absoluteDirPath = fileInfo.absoluteDir().absolutePath();
    if (!absoluteDirPath.isEmpty()) {
        appSettings.setValue(
            NOTE_EDITOR_LAST_ATTACHMENT_ADD_LOCATION_KEY, absoluteDirPath);

        QNTRACE(
            "note_editor",
            "Updated last attachment add location to " << absoluteDirPath);
    }

    dropFile(absoluteFilePath);
}

void NoteEditorPrivate::saveAttachmentDialog(const QByteArray & resourceHash)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::saveAttachmentDialog");
    onSaveResourceRequest(resourceHash);
}

void NoteEditorPrivate::saveAttachmentUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::saveAttachmentUnderCursor");

    if ((m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("ImageResource")) &&
        (m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("NonImageResource")))
    {
        ErrorString error(
            QT_TR_NOOP("can't save attachment under cursor: wrong "
                       "current context menu extra data's content type"));
        QNWARNING(
            "note_editor",
            error << ": content type = "
                  << m_currentContextMenuExtraData.m_contentType);
        Q_EMIT notifyError(error);
        return;
    }

    saveAttachmentDialog(m_currentContextMenuExtraData.m_resourceHash);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::openAttachment(const QByteArray & resourceHash)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::openAttachment");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't open the attachment"))
    onOpenResourceRequest(resourceHash);
}

void NoteEditorPrivate::openAttachmentUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::openAttachmentUnderCursor");

    if ((m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("ImageResource")) &&
        (m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("NonImageResource")))
    {
        ErrorString error(
            QT_TR_NOOP("Can't open attachment under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING(
            "note_editor",
            error << ": content type = "
                  << m_currentContextMenuExtraData.m_contentType);
        Q_EMIT notifyError(error);
        return;
    }

    openAttachment(m_currentContextMenuExtraData.m_resourceHash);
    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::copyAttachment(const QByteArray & resourceHash)
{
    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment: no note "
                       "is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QList<Resource> resources = m_pNote->resources();
    int resourceIndex = resourceIndexByHash(resources, resourceHash);
    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("The attachment to be copied was not found "
                       "within the note"));
        QNWARNING(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    const Resource & resource = qAsConst(resources).at(resourceIndex);

    if (Q_UNLIKELY(!resource.hasDataBody() && !resource.hasAlternateDataBody()))
    {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment as it has neither "
                       "data body nor alternate data body"));
        QNWARNING(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.hasMime())) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment as it has no mime type"));
        QNWARNING(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    const QByteArray & data =
        (resource.hasDataBody() ? resource.dataBody()
                                : resource.alternateDataBody());

    const QString & mimeType = resource.mime();

    QClipboard * pClipboard = QApplication::clipboard();
    if (Q_UNLIKELY(!pClipboard)) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment: can't get access "
                       "to clipboard"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QMimeData * pMimeData = new QMimeData;
    pMimeData->setData(mimeType, data);
    pClipboard->setMimeData(pMimeData);
}

void NoteEditorPrivate::copyAttachmentUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::copyAttachmentUnderCursor");

    if ((m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("ImageResource")) &&
        (m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("NonImageResource")))
    {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment under cursor: wrong current "
                       "context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    copyAttachment(m_currentContextMenuExtraData.m_resourceHash);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::removeAttachment(const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::removeAttachment: hash = " << resourceHash.toHex());

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't remove the attachment by hash: "
                       "no note is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't remove the attachment"))

    bool foundResourceToRemove = false;
    QList<Resource> resources = m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const Resource & resource = qAsConst(resources).at(i);
        if (resource.hasDataHash() && (resource.dataHash() == resourceHash)) {
            m_resourceInfo.removeResourceInfo(resource.dataHash());

            auto & noteEditorLocalStorageBroker =
                NoteEditorLocalStorageBroker::instance();

            auto * pLocalStorageManager =
                noteEditorLocalStorageBroker.localStorageManager();

            if (Q_UNLIKELY(!pLocalStorageManager)) {
                ErrorString error(
                    QT_TR_NOOP("Can't remove the attachment: note "
                               "editor is not initialized properly"));
                QNWARNING("note_editor", error);
                Q_EMIT notifyError(error);
                return;
            }

            auto * delegate = new RemoveResourceDelegate(
                resource, *this, *pLocalStorageManager);

            QObject::connect(
                delegate, &RemoveResourceDelegate::finished, this,
                &NoteEditorPrivate::onRemoveResourceDelegateFinished);

            QObject::connect(
                delegate, &RemoveResourceDelegate::cancelled, this,
                &NoteEditorPrivate::onRemoveResourceDelegateCancelled);

            QObject::connect(
                delegate, &RemoveResourceDelegate::notifyError, this,
                &NoteEditorPrivate::onRemoveResourceDelegateError);

            delegate->start();

            foundResourceToRemove = true;
            break;
        }
    }

    if (Q_UNLIKELY(!foundResourceToRemove)) {
        ErrorString error(
            QT_TR_NOOP("Can't remove the attachment by hash: no resource with "
                       "such hash was found within the note"));
        error.details() = QString::fromUtf8(resourceHash.toHex());
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
    }
}

void NoteEditorPrivate::removeAttachmentUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeAttachmentUnderCursor");

    if ((m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("ImageResource")) &&
        (m_currentContextMenuExtraData.m_contentType !=
         QStringLiteral("NonImageResource")))
    {
        ErrorString error(
            QT_TR_NOOP("Can't remove the attachment under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    removeAttachment(m_currentContextMenuExtraData.m_resourceHash);
    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::renameAttachmentUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::renameAttachmentUnderCursor");

    if (m_currentContextMenuExtraData.m_contentType !=
        QStringLiteral("NonImageResource"))
    {
        ErrorString error(
            QT_TR_NOOP("Can't rename the attachment under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    renameAttachment(m_currentContextMenuExtraData.m_resourceHash);
    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::renameAttachment(const QByteArray & resourceHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::renameAttachment: "
            << "resource hash = " << resourceHash.toHex());

    ErrorString errorPrefix(QT_TR_NOOP("Can't rename the attachment"));
    CHECK_NOTE_EDITABLE(errorPrefix)

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error = errorPrefix;
        error.appendBase(QT_TR_NOOP("No note is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    int targetResourceIndex = -1;
    auto resources = m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const auto & resource = qAsConst(resources).at(i);
        if (!resource.hasDataHash() || (resource.dataHash() != resourceHash)) {
            continue;
        }

        targetResourceIndex = i;
        break;
    }

    if (Q_UNLIKELY(targetResourceIndex < 0)) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("Can't find the corresponding resource in the note"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto & resource = resources[targetResourceIndex];
    if (Q_UNLIKELY(!resource.hasDataBody())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The resource doesn't have the data body set"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.hasDataHash())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The resource doesn't have the data hash set"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    auto * delegate = new RenameResourceDelegate(
        resource, *this, m_pGenericResourceImageManager,
        m_genericResourceImageFilePathsByResourceHash);

    setRenameResourceDelegateSubscriptions(*delegate);
    delegate->start();
}

void NoteEditorPrivate::rotateImageAttachment(
    const QByteArray & resourceHash, const Rotation rotationDirection)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::rotateImageAttachment: "
            << "resource hash = " << resourceHash.toHex()
            << ", rotation: " << rotationDirection);

    ErrorString errorPrefix(QT_TR_NOOP("Can't rotate the image attachment"));
    CHECK_NOTE_EDITABLE(errorPrefix)

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error = errorPrefix;
        error.appendBase(QT_TR_NOOP("No note is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    int targetResourceIndex = -1;
    auto resources = m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const auto & resource = qAsConst(resources)[i];
        if (!resource.hasDataHash() || (resource.dataHash() != resourceHash)) {
            continue;
        }

        if (Q_UNLIKELY(!resource.hasMime())) {
            ErrorString error = errorPrefix;
            error.appendBase(
                QT_TR_NOOP("The corresponding attachment's "
                           "mime type is not set"));
            QNWARNING("note_editor", error << ", resource: " << resource);
            Q_EMIT notifyError(error);
            return;
        }

        if (Q_UNLIKELY(!resource.mime().startsWith(QStringLiteral("image/")))) {
            ErrorString error = errorPrefix;
            error.appendBase(
                QT_TR_NOOP("The corresponding attachment's mime type "
                           "indicates it is not an image"));
            error.details() = resource.mime();
            QNWARNING("note_editor", error << ", resource: " << resource);
            Q_EMIT notifyError(error);
            return;
        }

        targetResourceIndex = i;
        break;
    }

    if (Q_UNLIKELY(targetResourceIndex < 0)) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("Can't find the corresponding attachment "
                       "within the note"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto & resource = resources[targetResourceIndex];
    if (!resource.hasDataBody()) {
        QNDEBUG(
            "note_editor",
            "The resource to be rotated doesn't have data "
                << "body set, requesting it from NoteEditorLocalStorageBroker");

        QString resourceLocalUid = resource.localUid();

        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage
            [resourceLocalUid] = rotationDirection;

        Q_EMIT findResourceData(resourceLocalUid);
        return;
    }

    if (Q_UNLIKELY(!resource.hasDataHash())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The attachment doesn't have the data hash set"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    auto * delegate = new ImageResourceRotationDelegate(
        resource.dataHash(), rotationDirection, *this, m_resourceInfo,
        *m_pResourceDataInTemporaryFileStorageManager,
        m_resourceFileStoragePathsByResourceLocalUid);

    QObject::connect(
        delegate, &ImageResourceRotationDelegate::finished, this,
        &NoteEditorPrivate::onImageResourceRotationDelegateFinished);

    QObject::connect(
        delegate, &ImageResourceRotationDelegate::notifyError, this,
        &NoteEditorPrivate::onImageResourceRotationDelegateError);

    delegate->start();
}

void NoteEditorPrivate::rotateImageAttachmentUnderCursor(
    const Rotation rotationDirection)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::rotateImageAttachmentUnderCursor: rotation: "
            << rotationDirection);

    if (m_currentContextMenuExtraData.m_contentType !=
        QStringLiteral("ImageResource"))
    {
        ErrorString error(
            QT_TR_NOOP("Can't rotate the image attachment under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    rotateImageAttachment(
        m_currentContextMenuExtraData.m_resourceHash, rotationDirection);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::rotateImageAttachmentUnderCursorClockwise()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::rotateImageAttachmentUnderCursorClockwise");

    rotateImageAttachmentUnderCursor(Rotation::Clockwise);
}

void NoteEditorPrivate::rotateImageAttachmentUnderCursorCounterclockwise()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::rotateImageAttachmentUnderCursorCounterclockwise");

    rotateImageAttachmentUnderCursor(Rotation::Counterclockwise);
}

void NoteEditorPrivate::encryptSelectedText()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::encryptSelectedText");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't encrypt the selected text"))

    auto * delegate = new EncryptSelectedTextDelegate(
        this, m_encryptionManager, m_decryptedTextManager);

    QObject::connect(
        delegate, &EncryptSelectedTextDelegate::finished, this,
        &NoteEditorPrivate::onEncryptSelectedTextDelegateFinished);

    QObject::connect(
        delegate, &EncryptSelectedTextDelegate::notifyError, this,
        &NoteEditorPrivate::onEncryptSelectedTextDelegateError);

    QObject::connect(
        delegate, &EncryptSelectedTextDelegate::cancelled, this,
        &NoteEditorPrivate::onEncryptSelectedTextDelegateCancelled);

    delegate->start(m_lastSelectedHtml);
}

void NoteEditorPrivate::decryptEncryptedTextUnderCursor()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::decryptEncryptedTextUnderCursor");

    if (Q_UNLIKELY(
            m_currentContextMenuExtraData.m_contentType !=
            QStringLiteral("EncryptedText")))
    {
        ErrorString error(
            QT_TR_NOOP("Can't decrypt the encrypted text under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    decryptEncryptedText(
        m_currentContextMenuExtraData.m_encryptedText,
        m_currentContextMenuExtraData.m_cipher,
        m_currentContextMenuExtraData.m_keyLength,
        m_currentContextMenuExtraData.m_hint,
        m_currentContextMenuExtraData.m_id);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::decryptEncryptedText(
    QString encryptedText, QString cipher, QString length, QString hint,
    QString enCryptIndex)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::decryptEncryptedText");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't decrypt the encrypted text"))

    auto * delegate = new DecryptEncryptedTextDelegate(
        enCryptIndex, encryptedText, cipher, length, hint, this,
        m_encryptionManager, m_decryptedTextManager);

    QObject::connect(
        delegate, &DecryptEncryptedTextDelegate::finished, this,
        &NoteEditorPrivate::onDecryptEncryptedTextDelegateFinished);

    QObject::connect(
        delegate, &DecryptEncryptedTextDelegate::cancelled, this,
        &NoteEditorPrivate::onDecryptEncryptedTextDelegateCancelled);

    QObject::connect(
        delegate, &DecryptEncryptedTextDelegate::notifyError, this,
        &NoteEditorPrivate::onDecryptEncryptedTextDelegateError);

    delegate->start();
}

void NoteEditorPrivate::hideDecryptedTextUnderCursor()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::hideDecryptedTextUnderCursor");

    if (Q_UNLIKELY(
            m_currentContextMenuExtraData.m_contentType !=
            QStringLiteral("GenericText")))
    {
        ErrorString error(
            QT_TR_NOOP("Can't hide the decrypted text under cursor: wrong "
                       "current context menu extra data's content type"));
        error.details() = m_currentContextMenuExtraData.m_contentType;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!m_currentContextMenuExtraData.m_insideDecryptedText)) {
        ErrorString error(
            QT_TR_NOOP("Can't hide the decrypted text under cursor: "
                       "the cursor doesn't appear to be inside "
                       "the decrypted text area"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    hideDecryptedText(
        m_currentContextMenuExtraData.m_encryptedText,
        m_currentContextMenuExtraData.m_decryptedText,
        m_currentContextMenuExtraData.m_cipher,
        m_currentContextMenuExtraData.m_keyLength,
        m_currentContextMenuExtraData.m_hint,
        m_currentContextMenuExtraData.m_id);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::hideDecryptedText(
    QString encryptedText, QString decryptedText, QString cipher,
    QString keyLength, QString hint, QString id)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::hideDecryptedText");

    bool conversionResult = false;
    size_t keyLengthInt =
        static_cast<size_t>(keyLength.toInt(&conversionResult));
    if (Q_UNLIKELY(!conversionResult)) {
        ErrorString error(
            QT_TR_NOOP("Can't hide the decrypted text: can't convert "
                       "the key length attribute to an integer"));
        error.details() = keyLength;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool rememberForSession = false;
    QString originalDecryptedText;
    bool foundOriginalDecryptedText =
        m_decryptedTextManager->findDecryptedTextByEncryptedText(
            encryptedText, originalDecryptedText, rememberForSession);
    if (foundOriginalDecryptedText && (decryptedText != originalDecryptedText))
    {
        QNDEBUG(
            "note_editor",
            "The original decrypted text doesn't match "
                << "the newer one, will return-encrypt "
                << "the decrypted text");
        QString newEncryptedText;
        bool reEncryptedText = m_decryptedTextManager->modifyDecryptedText(
            encryptedText, decryptedText, newEncryptedText);
        if (Q_UNLIKELY(!reEncryptedText)) {
            ErrorString error(
                QT_TR_NOOP("Can't hide the decrypted text: "
                           "the decrypted text was modified "
                           "but it failed to get return-encrypted"));
            error.details() = keyLength;
            QNWARNING("note_editor", error);
            Q_EMIT notifyError(error);
            return;
        }

        QNDEBUG(
            "note_editor",
            "Old encrypted text = " << encryptedText
                                    << ", new encrypted text = "
                                    << newEncryptedText);
        encryptedText = newEncryptedText;
    }

    quint64 enCryptIndex = m_lastFreeEnCryptIdNumber++;

    QString html = ENMLConverter::encryptedTextHtml(
        encryptedText, hint, cipher, keyLengthInt, enCryptIndex);

    escapeStringForJavaScript(html);

    QString javascript =
        QStringLiteral(
            "encryptDecryptManager.replaceDecryptedTextWithEncryptedText('") +
        id + QStringLiteral("', '") + html + QStringLiteral("');");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onHideDecryptedTextFinished));
}

void NoteEditorPrivate::editHyperlinkDialog()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::editHyperlinkDialog");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't edit the hyperlink"))

    // NOTE: when adding new hyperlink, the selected html can be empty, it's ok
    m_lastSelectedHtmlForHyperlink = m_lastSelectedHtml;

    QString javascript =
        QStringLiteral("hyperlinkManager.findSelectedHyperlinkId();");

    GET_PAGE()

    page->executeJavaScript(
        javascript,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onFoundSelectedHyperlinkId));
}

void NoteEditorPrivate::copyHyperlink()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::copyHyperlink");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("hyperlinkManager.getSelectedHyperlinkData();"),
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onFoundHyperlinkToCopy));
}

void NoteEditorPrivate::removeHyperlink()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::removeHyperlink");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't remove the hyperlink"))

    auto * delegate = new RemoveHyperlinkDelegate(*this);

    QObject::connect(
        delegate, &RemoveHyperlinkDelegate::finished, this,
        &NoteEditorPrivate::onRemoveHyperlinkDelegateFinished);

    QObject::connect(
        delegate, &RemoveHyperlinkDelegate::notifyError, this,
        &NoteEditorPrivate::onRemoveHyperlinkDelegateError);

    delegate->start();
}

void NoteEditorPrivate::onNoteLoadCancelled()
{
    stop();

    QNINFO("note_editor", "Note load has been cancelled");

    // TODO: add some overlay widget for NoteEditor to properly indicate
    // visually that the note load has been cancelled
}

void NoteEditorPrivate::onTableResized()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onTableResized");
    convertToNote();
}

void NoteEditorPrivate::onFoundSelectedHyperlinkId(
    const QVariant & hyperlinkData,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFoundSelectedHyperlinkId: " << hyperlinkData);
    Q_UNUSED(extraData)

    auto resultMap = hyperlinkData.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of the attempt "
                       "to find the hyperlink data by id from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        QNTRACE(
            "note_editor",
            "No hyperlink id under cursor was found, "
                << "assuming we're adding the new hyperlink to the selected "
                   "text");

        GET_PAGE()

        quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
        setupAddHyperlinkDelegate(hyperlinkId);
        return;
    }

    auto dataIt = resultMap.find(QStringLiteral("data"));
    if (Q_UNLIKELY(dataIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the seemingly positive result of "
                       "the attempt to find the hyperlink data by id from "
                       "JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString hyperlinkDataStr = dataIt.value().toString();

    bool conversionResult = false;
    quint64 hyperlinkId = hyperlinkDataStr.toULongLong(&conversionResult);
    if (!conversionResult) {
        ErrorString error(
            QT_TR_NOOP("Can't add or edit hyperlink under cursor: "
                       "can't convert hyperlink id number to unsigned int"));
        error.details() = hyperlinkDataStr;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QNTRACE("note_editor", "Will edit the hyperlink with id " << hyperlinkId);

    auto * delegate = new EditHyperlinkDelegate(*this, hyperlinkId);

    QObject::connect(
        delegate, &EditHyperlinkDelegate::finished, this,
        &NoteEditorPrivate::onEditHyperlinkDelegateFinished);

    QObject::connect(
        delegate, &EditHyperlinkDelegate::cancelled, this,
        &NoteEditorPrivate::onEditHyperlinkDelegateCancelled);

    QObject::connect(
        delegate, &EditHyperlinkDelegate::notifyError, this,
        &NoteEditorPrivate::onEditHyperlinkDelegateError);

    delegate->start();
}

void NoteEditorPrivate::onFoundHyperlinkToCopy(
    const QVariant & hyperlinkData,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFoundHyperlinkToCopy: " << hyperlinkData);

    Q_UNUSED(extraData);

    QStringList hyperlinkDataList = hyperlinkData.toStringList();
    if (hyperlinkDataList.isEmpty()) {
        QNTRACE("note_editor", "Hyperlink data to copy was not found");
        return;
    }

    if (hyperlinkDataList.size() != 3) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the hyperlink: can't get text "
                       "and hyperlink from JavaScript"));
        error.details() = hyperlinkDataList.join(QStringLiteral(","));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    QClipboard * pClipboard = QApplication::clipboard();
    if (Q_UNLIKELY(!pClipboard)) {
        QNWARNING("note_editor", "Unable to get window system clipboard");
    }
    else {
        pClipboard->setText(hyperlinkDataList[1]);
    }
}

void NoteEditorPrivate::dropFile(const QString & filePath)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::dropFile: " << filePath);

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't add the attachment via drag'n'drop"))

    auto * delegate = new AddResourceDelegate(
        filePath, *this, m_pResourceDataInTemporaryFileStorageManager,
        m_pFileIOProcessorAsync, m_pGenericResourceImageManager,
        m_genericResourceImageFilePathsByResourceHash);

    QObject::connect(
        delegate, &AddResourceDelegate::finished, this,
        &NoteEditorPrivate::onAddResourceDelegateFinished);

    QObject::connect(
        delegate, &AddResourceDelegate::notifyError, this,
        &NoteEditorPrivate::onAddResourceDelegateError);

    delegate->start();
}

void NoteEditorPrivate::pasteImageData(const QMimeData & mimeData)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::pasteImageData");

    QImage image = qvariant_cast<QImage>(mimeData.imageData());
    QByteArray data;
    QBuffer imageDataBuffer(&data);
    imageDataBuffer.open(QIODevice::WriteOnly);
    image.save(&imageDataBuffer, "PNG");

    QString mimeType = QStringLiteral("image/png");

    auto * delegate = new AddResourceDelegate(
        data, mimeType, *this, m_pResourceDataInTemporaryFileStorageManager,
        m_pFileIOProcessorAsync, m_pGenericResourceImageManager,
        m_genericResourceImageFilePathsByResourceHash);

    QObject::connect(
        delegate, &AddResourceDelegate::finished, this,
        &NoteEditorPrivate::onAddResourceDelegateFinished);

    QObject::connect(
        delegate, &AddResourceDelegate::notifyError, this,
        &NoteEditorPrivate::onAddResourceDelegateError);

    delegate->start();
}

void NoteEditorPrivate::escapeStringForJavaScript(QString & str) const
{
    // Escape all escape sequences to avoid syntax errors
    str.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    str.replace(QStringLiteral("\b"), QStringLiteral("\\b"));
    str.replace(QStringLiteral("\f"), QStringLiteral("\\f"));
    str.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    str.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
    str.replace(QStringLiteral("\t"), QStringLiteral("\\t"));
    str.replace(QStringLiteral("\v"), QStringLiteral("\\v"));
    str.replace(QStringLiteral("\?"), QStringLiteral("\\?"));

    // Escape single and double quotes
    ENMLConverter::escapeString(str, /* simplify = */ false);
}

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(QDebug & dbg, const NoteEditorPrivate::BlankPageKind kind)
{
    using BlankPageKind = NoteEditorPrivate::BlankPageKind;

    switch (kind) {
    case BlankPageKind::Initial:
        dbg << "Initial";
        break;
    case BlankPageKind::NoteNotFound:
        dbg << "Note not found";
        break;
    case BlankPageKind::NoteDeleted:
        dbg << "Note deleted";
        break;
    case BlankPageKind::NoteLoading:
        dbg << "Note loading";
        break;
    case BlankPageKind::InternalError:
        dbg << "Internal error";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(kind) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier

////////////////////////////////////////////////////////////////////////////////

void initNoteEditorResources()
{
    Q_INIT_RESOURCE(underline);
    Q_INIT_RESOURCE(css);
    Q_INIT_RESOURCE(checkbox_icons);
    Q_INIT_RESOURCE(encrypted_area_icons);
    Q_INIT_RESOURCE(generic_resource_icons);
    Q_INIT_RESOURCE(jquery);
    Q_INIT_RESOURCE(colResizable);
    Q_INIT_RESOURCE(debounce);
    Q_INIT_RESOURCE(rangy);
    Q_INIT_RESOURCE(scripts);
    Q_INIT_RESOURCE(hilitor);

    QNDEBUG("note_editor", "Initialized NoteEditor's qrc resources");
}
