/*
 * Copyright 2016-2025 Dmitry Ivanov
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
#include "WebSocketClientWrapper.h"
#include "WebSocketTransport.h"

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
#include "javascript_glue/EnCryptElementOnClickHandler.h"
#include "javascript_glue/GenericResourceImageJavaScriptHandler.h"
#include "javascript_glue/GenericResourceOpenAndSaveButtonsOnClickHandler.h"
#include "javascript_glue/HyperlinkClickJavaScriptHandler.h"
#include "javascript_glue/PageMutationHandler.h"
#include "javascript_glue/ResizableImageJavaScriptHandler.h"
#include "javascript_glue/ResourceInfoJavaScriptHandler.h"
#include "javascript_glue/SpellCheckerDynamicHelper.h"
#include "javascript_glue/TableResizeJavaScriptHandler.h"
#include "javascript_glue/TextCursorPositionJavaScriptHandler.h"
#include "javascript_glue/ToDoCheckboxAutomaticInsertionHandler.h"
#include "javascript_glue/ToDoCheckboxOnClickHandler.h"
#include "javascript_glue/WebSocketWaiter.h"

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

#include <quentier/enml/Factory.h>
#include <quentier/enml/HtmlUtils.h>
#include <quentier/enml/IConverter.h>
#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/enml/IENMLTagsConverter.h>
#include <quentier/enml/IHtmlData.h>
#include <quentier/enml/conversion_rules/Factory.h>
#include <quentier/enml/conversion_rules/ISkipRuleBuilder.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/note_editor/SpellChecker.h>
#include <quentier/types/Account.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/types/ResourceRecognitionIndexItem.h>
#include <quentier/types/ResourceUtils.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Checks.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/Factory.h>
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/ShortcutManager.h>
#include <quentier/utility/Size.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMimeDatabase>
#include <QPageLayout>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QTransform>
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebSocketServer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

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

[[nodiscard]] std::optional<IEncryptor::Cipher> parseCipher(
    const QString & cipherStr) noexcept
{
    if (cipherStr == QStringLiteral("AES")) {
        return IEncryptor::Cipher::AES;
    }

    if (cipherStr == QStringLiteral("RC2")) {
        return IEncryptor::Cipher::RC2;
    }

    return std::nullopt;
}

[[nodiscard]] int fontMetricsWidth(
    const QFontMetrics & fontMetrics, const QString & text, int len = -1)
{
    return fontMetrics.horizontalAdvance(text, len);
}

} // namespace

NoteEditorPrivate::NoteEditorPrivate(NoteEditor & noteEditor) :
    INoteEditorBackend(&noteEditor),
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
    m_encryptor(createOpenSslEncryptor()),
    m_enmlTagsConverter(enml::createEnmlTagsConverter()),
    m_enmlConverter(enml::createConverter(m_enmlTagsConverter)),
    m_pFileIOProcessorAsync(new FileIOProcessorAsync),
    m_pResourceInfoJavaScriptHandler(
        new ResourceInfoJavaScriptHandler(m_resourceInfo, this)),
    m_pGenericResoureImageJavaScriptHandler(
        new GenericResourceImageJavaScriptHandler(
            m_genericResourceImageFilePathsByResourceHash, this)),
    q_ptr(&noteEditor)
{
    setupSkipRulesForHtmlToEnmlConversion();
    setupTextCursorPositionJavaScriptHandlerConnections();
    setupGeneralSignalSlotConnections();
    setupScripts();
    setAcceptDrops(false);
}

NoteEditorPrivate::~NoteEditorPrivate() noexcept
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
    if (m_pNote->active() && !*m_pNote->active()) {
        QNDEBUG(
            "note_editor",
            "Current note is not active, setting it to " << "read-only state");
        editable = false;
    }
    else if (isInkNote(*m_pNote)) {
        QNDEBUG(
            "note_editor",
            "Current note is an ink note, setting it to " << "read-only state");
        editable = false;
    }
    else if (m_pNotebook->restrictions()) {
        const auto & restrictions = *m_pNotebook->restrictions();
        if (restrictions.noUpdateNotes() && *restrictions.noUpdateNotes()) {
            QNDEBUG(
                "note_editor",
                "Notebook restrictions forbid the note modification, setting "
                    << "note's content to read-only state");
            editable = false;
        }
    }
    else if (
        m_pNote->attributes() && m_pNote->attributes()->contentClass() &&
        !m_pNote->attributes()->contentClass()->isEmpty())
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

    provideSrcAndOnClickScriptForImgEnCryptTags();
    page->executeJavaScript(m_setupTextCursorPositionTrackingJs);
    setupTextCursorPositionTracking();
    setupGenericResourceImages();

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
    page->executeJavaScript(m_setupActionsJs);

    // NOTE: executing page mutation observer's script last
    // so that it doesn't catch the mutations originating from the above scripts
    page->executeJavaScript(m_pageMutationObserverJs);

    if (m_spellCheckerEnabled) {
        applySpellCheck();
    }

    QNTRACE(
        "note_editor",
        "Sent commands to execute all the page's necessary " << "scripts");

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
    QString resourceLocalId, QString fileStoragePath, // NOLINT
    QByteArray resourceData, QByteArray resourceDataHash)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onResourceFileChanged: "
            << "resource local id = " << resourceLocalId
            << ", file storage path: " << fileStoragePath
            << ", new resource data size = "
            << humanReadableSize(static_cast<quint64>(
                   std::max<qsizetype>(resourceData.size(), 0)))
            << ", resource data hash = " << resourceDataHash.toHex());

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG(
            "note_editor",
            "Can't process resource file change: no note is "
                << "set to the editor");
        return;
    }

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const auto resourceIt = std::find_if(
        resources.begin(), resources.end(),
        [&resourceLocalId](const qevercloud::Resource & resource) {
            return resource.localId() == resourceLocalId;
        });

    if (Q_UNLIKELY(resourceIt == resources.end())) {
        QNDEBUG(
            "note_editor",
            "Can't process resource file change: can't find "
                << "the resource by local id within note's resources");
        return;
    }

    auto & resource = *resourceIt;

    const QByteArray previousResourceHash =
        (resource.data() && resource.data()->bodyHash())
        ? *resource.data()->bodyHash()
        : QByteArray();

    QNTRACE(
        "note_editor",
        "Previous resource hash = " << previousResourceHash.toHex());

    if (!previousResourceHash.isEmpty() &&
        (previousResourceHash == resourceDataHash) && resource.data() &&
        resource.data()->size() &&
        (*resource.data()->size() == resourceData.size()))
    {
        QNDEBUG(
            "note_editor",
            "Neither resource hash nor binary data size has changed -> the "
                << "resource data has not actually changed, nothing to do");
        return;
    }

    if (!resource.data()) {
        resource.setData(qevercloud::Data{});
    }

    resource.mutableData()->setBody(resourceData);
    resource.mutableData()->setBodyHash(resourceDataHash);
    resource.mutableData()->setSize(resourceData.size());

    // Need to clear any existing recognition data as the resource's contents
    // were changed
    resource.setRecognition(std::nullopt);

    const QString resourceMimeTypeName =
        (resource.mime() ? *resource.mime() : QString());

    const QString displayName = resourceDisplayName(resource);

    const QString displaySize =
        humanReadableSize(static_cast<quint64>(resourceData.size()));

    QNTRACE("note_editor", "Updating resource within the note: " << resource);

    setModified();

    if (!previousResourceHash.isEmpty() &&
        (previousResourceHash != resourceDataHash))
    {
        QSize resourceImageSize;
        if (resource.height() && resource.width()) {
            resourceImageSize.setHeight(*resource.height());
            resourceImageSize.setWidth(*resource.width());
        }

        Q_UNUSED(m_resourceInfo.removeResourceInfo(previousResourceHash))

        m_resourceInfo.cacheResourceInfo(
            resourceDataHash, displayName, displaySize, fileStoragePath,
            resourceImageSize);

        updateHashForResourceTag(previousResourceHash, resourceDataHash);
    }

    if (resourceMimeTypeName.startsWith(QStringLiteral("image/"))) {
        removeSymlinksToImageResourceFile(resourceLocalId);

        ErrorString errorDescription;

        const QString linkFilePath = createSymlinkToImageResourceFile(
            fileStoragePath, resourceLocalId, errorDescription);

        if (linkFilePath.isEmpty()) {
            QNWARNING("note_editor", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        m_resourceFileStoragePathsByResourceLocalId[resourceLocalId] =
            linkFilePath;

        QSize resourceImageSize;
        if (resource.height() && resource.width()) {
            resourceImageSize.setHeight(*resource.height());
            resourceImageSize.setWidth(*resource.width());
        }

        m_resourceInfo.cacheResourceInfo(
            resourceDataHash, displayName, displaySize, linkFilePath,
            resourceImageSize);

        if (!m_pendingNotePageLoad) {
            GET_PAGE()
            page->executeJavaScript(
                QStringLiteral("updateImageResourceSrc('") +
                QString::fromLocal8Bit(resourceDataHash.toHex()) +
                QStringLiteral("', '") + linkFilePath + QStringLiteral("', ") +
                QString::number(
                    resource.height() ? *resource.height() : qint16(0)) +
                QStringLiteral(", ") +
                QString::number(
                    resource.width() ? *resource.width() : qint16(0)) +
                QStringLiteral(");"));
        }
    }
    else {
        QImage image = buildGenericResourceImage(resource);
        saveGenericResourceImage(resource, image);
    }
}

void NoteEditorPrivate::onGenericResourceImageSaved(
    bool success, QByteArray resourceActualHash, QString filePath, // NOLINT
    ErrorString errorDescription, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onGenericResourceImageSaved: "
            << "success = " << (success ? "true" : "false")
            << ", resource actual hash = " << resourceActualHash.toHex()
            << ", file path = " << filePath << ", error description = "
            << errorDescription << ", requestId = " << requestId);

    const auto it = m_saveGenericResourceImageToFileRequestIds.find(requestId);
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

void NoteEditorPrivate::onHyperlinkClicked(QString url) // NOLINT
{
    handleHyperlinkClicked(QUrl(url));
}

void NoteEditorPrivate::onWebSocketReady()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onWebSocketReady");

    m_webSocketReady = true;
    onNoteLoadFinished(true);
}

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

void NoteEditorPrivate::onToDoCheckboxClickHandlerError( // NOLINT
    ErrorString error)                                   // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate" << "::onToDoCheckboxClickHandlerError: " << error);

    Q_EMIT notifyError(error);
}

void NoteEditorPrivate::onToDoCheckboxInserted(
    const QVariant & data,
    const QVector<std::pair<QString, QString>> & extraData)
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::onToDoCheckboxInserted: " << data);

    Q_UNUSED(extraData)

    const QMap<QString, QVariant> resultMap = data.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of ToDo checkbox "
                       "insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate" << "::onToDoCheckboxAutomaticInsertion");

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

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of ToDo checkbox "
                       "automatic insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    auto * pSenderPage = qobject_cast<NoteEditorPage *>(sender());
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

        page->toHtml(NoteEditorCallbackFunctor<QString>(
            this, &NoteEditorPrivate::onPageHtmlReceived));

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

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const int resourceIndex = resourceIndexByHash(resources, resourceHash);
    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("The resource to be opened was not found "
                       "within the note"));
        QNWARNING("note_editor", error << ", resource hash = " << resourceHash);
        Q_EMIT notifyError(error);
        return;
    }

    const auto & resource = std::as_const(resources)[resourceIndex];
    const QString & resourceLocalId = resource.localId();

    const auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalId](const auto & pair) {
            return pair.first == resourceLocalId;
        });

    if (it == m_prepareResourceForOpeningProgressDialogs.end()) {
        auto * pProgressDialog = new QProgressDialog(
            tr("Preparing to open attachment") + QStringLiteral("..."),
            QString(),
            /* min = */ 0,                      // NOLINT
            /* max = */ 100, this, Qt::Dialog); // NOLINT

        pProgressDialog->setWindowModality(Qt::WindowModal);
        pProgressDialog->setMinimumDuration(2000);

        m_prepareResourceForOpeningProgressDialogs.emplace_back(
            resourceLocalId, pProgressDialog);
    }

    QNTRACE(
        "note_editor",
        "Emitting the request to open resource with local id "
            << resourceLocalId);

    Q_EMIT openResourceFile(resourceLocalId);
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

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const int resourceIndex = resourceIndexByHash(resources, resourceHash);
    if (Q_UNLIKELY(resourceIndex < 0)) {
        ErrorString error(
            QT_TR_NOOP("The resource to be saved was not found "
                       "within the note"));
        QNINFO(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        return;
    }

    const auto & resource = std::as_const(resources)[resourceIndex];

    if ((!resource.data() || !resource.data()->body()) &&
        (!resource.alternateData() || !resource.alternateData()->body()))
    {
        QNTRACE(
            "note_editor",
            "The resource meant to be saved to a local file "
                << "has neither data body nor alternate data body, "
                << "need to request these from the local storage");
        Q_UNUSED(m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile
                     .insert(resource.localId()))
        Q_EMIT findResourceData(resource.localId());
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
    QString contentType, QString selectedHtml, // NOLINT
    bool insideDecryptedTextFragment, QStringList extraData,
    quint64 sequenceNumber)
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
            "Sequence number is not valid, not doing " << "anything");
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

        const auto resourceHash =
            QByteArray::fromHex(extraData[0].toLocal8Bit());

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
        const bool res = parseEncryptedTextContextMenuExtraData(
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
        "NoteEditorPrivate::onTextCursorUnderlineStateChanged: "
            << (state ? "underline" : "not underline"));

    m_currentTextFormattingState.m_underline = state;
    Q_EMIT textUnderlineState(state);
}

void NoteEditorPrivate::onTextCursorStrikethgouthStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorStrikethgouthStateChanged: "
            << (state ? "strikethrough" : "not strikethrough"));

    m_currentTextFormattingState.m_strikethrough = state;
    Q_EMIT textStrikethroughState(state);
}

void NoteEditorPrivate::onTextCursorAlignLeftStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorAlignLeftStateChanged: "
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
        "NoteEditorPrivate::onTextCursorAlignCenterStateChanged: "
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
        "NoteEditorPrivate::onTextCursorAlignRightStateChanged: "
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
        "NoteEditorPrivate::onTextCursorAlignFullStateChanged: "
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
        "NoteEditorPrivate::onTextCursorInsideOrderedListStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideOrderedList = state;
    Q_EMIT textInsideOrderedListState(state);
}

void NoteEditorPrivate::onTextCursorInsideUnorderedListStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorInsideUnorderedListStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideUnorderedList = state;
    Q_EMIT textInsideUnorderedListState(state);
}

void NoteEditorPrivate::onTextCursorInsideTableStateChanged(bool state)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorInsideTableStateChanged: "
            << (state ? "true" : "false"));

    m_currentTextFormattingState.m_insideTable = state;
    Q_EMIT textInsideTableState(state);
}

void NoteEditorPrivate::onTextCursorOnImageResourceStateChanged(
    bool state, QByteArray resourceHash) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorOnImageResourceStateChanged: "
            << (state ? "yes" : "no")
            << ", resource hash = " << resourceHash.toHex());

    m_currentTextFormattingState.m_onImageResource = state;
    if (state) {
        m_currentTextFormattingState.m_resourceHash =
            QString::fromLocal8Bit(resourceHash);
    }
}

void NoteEditorPrivate::onTextCursorOnNonImageResourceStateChanged(
    bool state, QByteArray resourceHash) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorOnNonImageResourceStateChanged: "
            << (state ? "yes" : "no")
            << ", resource hash = " << resourceHash.toHex());

    m_currentTextFormattingState.m_onNonImageResource = state;
    if (state) {
        m_currentTextFormattingState.m_resourceHash =
            QString::fromLocal8Bit(resourceHash);
    }
}

void NoteEditorPrivate::onTextCursorOnEnCryptTagStateChanged(
    bool state, QString encryptedText, QString cipher, QString length) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorOnEnCryptTagStateChanged: "
            << (state ? "yes" : "no") << ", encrypted text = " << encryptedText
            << ", cipher = " << cipher << ", length = " << length);

    m_currentTextFormattingState.m_onEnCryptTag = state;
    if (state) {
        m_currentTextFormattingState.m_encryptedText = encryptedText;
        m_currentTextFormattingState.m_cipher = cipher;
        m_currentTextFormattingState.m_length = length;
    }
}

void NoteEditorPrivate::onTextCursorFontNameChanged(QString fontName) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorFontNameChanged: " << "font name = "
                                                           << fontName);
    Q_EMIT textFontFamilyChanged(fontName);
}

void NoteEditorPrivate::onTextCursorFontSizeChanged(int fontSize)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onTextCursorFontSizeChanged: " << "font size = "
                                                           << fontSize);
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
            page()->setUrl(url);
            m_pendingNotePageLoadMethodExit = false;
            QNDEBUG(
                "note_editor",
                "After having started to load the url " << "into the page: "
                                                        << url);

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

    const auto manualSaveResourceIt =
        m_manualSaveResourceToFileRequestIds.find(requestId);

    if (manualSaveResourceIt != m_manualSaveResourceToFileRequestIds.end()) {
        if (success) {
            QNDEBUG(
                "note_editor",
                "Successfully saved resource to file for " << "request id "
                                                           << requestId);
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
    const QMap<QString, QVariant> resultMap = response.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't find the status within the result "
                       "of selection formatting as source code"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;
        const auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Internal error: can't parse the error of "
                           "selection formatting as source code from "
                           "JavaScript"));
        }
        else {
            const QString errorValue = errorIt.value().toString();
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
                const auto feedbackIt =
                    resultMap.find(QStringLiteral("feedback"));

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
    qevercloud::Resource addedResource, // NOLINT
    QString resourceFileStoragePath)    // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onAddResourceDelegateFinished: "
            << "resource file storage path = " << resourceFileStoragePath);

    QNTRACE("note_editor", addedResource);

    if (!addedResource.data() || !addedResource.data()->bodyHash()) {
        ErrorString error(
            QT_TR_NOOP("The added resource doesn't contain the data hash"));
        QNWARNING("note_editor", error);
        removeResourceFromNote(addedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!addedResource.data() || !addedResource.data()->size()) {
        ErrorString error(
            QT_TR_NOOP("The added resource doesn't contain the data size"));
        QNWARNING("note_editor", error);
        removeResourceFromNote(addedResource);
        Q_EMIT notifyError(error);
        return;
    }

    m_resourceFileStoragePathsByResourceLocalId[addedResource.localId()] =
        resourceFileStoragePath;

    QSize resourceImageSize;
    if (addedResource.height() && addedResource.width()) {
        resourceImageSize.setHeight(*addedResource.height());
        resourceImageSize.setWidth(*addedResource.width());
    }

    m_resourceInfo.cacheResourceInfo(
        *addedResource.data()->bodyHash(), resourceDisplayName(addedResource),
        humanReadableSize(static_cast<quint64>(*addedResource.data()->size())),
        resourceFileStoragePath, resourceImageSize);

    setupGenericResourceImages();
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

void NoteEditorPrivate::onAddResourceDelegateError(ErrorString error) // NOLINT
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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of new resource "
                       "html insertion undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
    qevercloud::Resource removedResource, bool reversible) // NOLINT
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
    QString resourceLocalId) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onRemoveResourceDelegateCancelled: "
            << "resource local id = " << resourceLocalId);

    auto * delegate = qobject_cast<RemoveResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRemoveResourceDelegateError(
    ErrorString error) // NOLINT
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
        "NoteEditorPrivate::onRemoveResourceUndoRedoFinished: " << data);

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
    QString oldResourceName, QString newResourceName,   // NOLINT
    qevercloud::Resource resource, bool performingUndo) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onRenameResourceDelegateFinished: "
            << "old resource name = " << oldResourceName
            << ", new resource name = " << newResourceName
            << ", performing undo = " << (performingUndo ? "true" : "false"));

    QNTRACE("note_editor", "Resource: " << resource);

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
        "NoteEditorPrivate" << "::onRenameResourceDelegateCancelled");

    auto * delegate = qobject_cast<RenameResourceDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onRenameResourceDelegateError(
    ErrorString error) // NOLINT
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
    QByteArray resourceDataBefore, QByteArray resourceHashBefore, // NOLINT
    QByteArray resourceRecognitionDataBefore,                     // NOLINT
    QByteArray resourceRecognitionDataHashBefore,                 // NOLINT
    QSize resourceImageSizeBefore,
    qevercloud::Resource resourceAfter, // NOLINT
    INoteEditorBackend::Rotation rotationDirection)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onImageResourceRotationDelegateFinished: "
            << "previous resource hash = " << resourceHashBefore.toHex()
            << ", resource local id = " << resourceAfter.localId()
            << ", rotation direction = " << rotationDirection);

    auto * pCommand = new ImageResourceRotationUndoCommand(
        std::move(resourceDataBefore), std::move(resourceHashBefore),
        std::move(resourceRecognitionDataBefore),
        std::move(resourceRecognitionDataHashBefore), resourceImageSizeBefore,
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
        if (m_pNote->resources()) {
            auto it = std::find_if(
                m_pNote->mutableResources()->begin(),
                m_pNote->mutableResources()->end(),
                [localId = resourceAfter.localId()](
                    const qevercloud::Resource & resource) {
                    return resource.localId() == localId;
                });
            if (it != m_pNote->mutableResources()->end()) {
                *it = resourceAfter;
            }
            else {
                m_pNote->mutableResources()->push_back(resourceAfter);
            }
        }
        else {
            m_pNote->setResources(QList<qevercloud::Resource>());
            m_pNote->mutableResources()->push_back(resourceAfter);
        }
    }

    highlightRecognizedImageAreas(
        m_lastSearchHighlightedText,
        m_lastSearchHighlightedTextCaseSensitivity);

    setModified();

    m_pendingConversionToNoteForSavingInLocalStorage = true;
    convertToNote();
}

void NoteEditorPrivate::onImageResourceRotationDelegateError(
    ErrorString error) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate" << "::onImageResourceRotationDelegateError");

    Q_EMIT notifyError(std::move(error));

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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of decrypted text "
                       "hiding from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
    provideSrcAndOnClickScriptForImgEnCryptTags();

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
        "NoteEditorPrivate::onHideDecryptedTextUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of decrypted text "
                       "hiding undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    provideSrcAndOnClickScriptForImgEnCryptTags();
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateFinished()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onEncryptSelectedTextDelegateFinished");

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

    provideSrcAndOnClickScriptForImgEnCryptTags();
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateCancelled()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onEncryptSelectedTextDelegateCancelled");

    auto * delegate = qobject_cast<EncryptSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEncryptSelectedTextDelegateError(
    ErrorString error) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onEncryptSelectedTextDelegateError: " << error);

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
        "NoteEditorPrivate::onEncryptSelectedTextUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of encryption "
                       "undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    provideSrcAndOnClickScriptForImgEnCryptTags();
}

void NoteEditorPrivate::onDecryptEncryptedTextDelegateFinished(
    QString encryptedText, const IEncryptor::Cipher cipher, QString hint,
    QString decryptedText, QString passphrase, bool rememberForSession,
    bool decryptPermanently)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onDecryptEncryptedTextDelegateFinished");

    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Can't decrypt text"))

    setModified();

    EncryptDecryptUndoCommandInfo info;
    info.m_encryptedText = std::move(encryptedText);
    info.m_decryptedText = std::move(decryptedText);
    info.m_passphrase = std::move(passphrase);
    info.m_cipher = cipher;
    info.m_hint = std::move(hint);
    info.m_rememberForSession = rememberForSession;
    info.m_decryptPermanently = decryptPermanently;

    QVector<std::pair<QString, QString>> extraData;
    extraData << std::make_pair(
        QStringLiteral("decryptPermanently"),
        (decryptPermanently ? QStringLiteral("true")
                            : QStringLiteral("false")));

    auto * command = new DecryptUndoCommand(
        info, m_decryptedTextCache, *this,
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onDecryptEncryptedTextUndoRedoFinished,
            extraData));

    QObject::connect(
        command, &DecryptUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(command);

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
        "NoteEditorPrivate::onDecryptEncryptedTextDelegateCancelled");

    auto * delegate = qobject_cast<DecryptEncryptedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onDecryptEncryptedTextDelegateError(
    ErrorString error) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onDecryptEncryptedTextDelegateError: " << error);

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
        "NoteEditorPrivate::onDecryptEncryptedTextUndoRedoFinished: " << data);

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of encrypted text "
                       "decryption undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateFinished");

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
        "NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateCancelled");

    auto * delegate =
        qobject_cast<AddHyperlinkToSelectedTextDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateError(
    ErrorString error) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onAddHyperlinkToSelectedTextDelegateError");

    Q_EMIT notifyError(std::move(error));

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
        "NoteEditorPrivate::onAddHyperlinkToSelectedTextUndoRedoFinished: "
            << data);

    Q_UNUSED(extraData)

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "addition undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate" << "::onEditHyperlinkDelegateCancelled");

    auto * delegate = qobject_cast<EditHyperlinkDelegate *>(sender());
    if (Q_LIKELY(delegate)) {
        delegate->deleteLater();
    }
}

void NoteEditorPrivate::onEditHyperlinkDelegateError(
    ErrorString error) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate" << "::onEditHyperlinkDelegateError: " << error);

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
        "NoteEditorPrivate" << "::onEditHyperlinkUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "edit undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate" << "::onRemoveHyperlinkDelegateFinished");

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

void NoteEditorPrivate::onRemoveHyperlinkDelegateError(
    ErrorString error) // NOLINT
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
        "NoteEditorPrivate" << "::onRemoveHyperlinkUndoRedoFinished: " << data);

    Q_UNUSED(extraData)

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "removal undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
    QList<qevercloud::Resource> addedResources,
    QStringList resourceFileStoragePaths)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onInsertHtmlDelegateFinished: "
            << "num added resources = " << addedResources.size());

    setModified();

    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QNTRACE("note_editor", "Added resources: ");
        for (const auto & resource: std::as_const(addedResources)) {
            QNTRACE("note_editor", resource);
        }

        QNTRACE("note_editor", "Resource file storage paths: ");
        for (const auto & path: std::as_const(resourceFileStoragePaths)) {
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

void NoteEditorPrivate::onInsertHtmlDelegateError(ErrorString error) // NOLINT
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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of html insertion "
                       "undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate" << "::onSourceCodeFormatUndoRedoFinished: "
                            << data);

    Q_UNUSED(extraData)

    setModified();

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of source code "
                       "formatting undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

void NoteEditorPrivate::onUndoCommandError(ErrorString error) // NOLINT
{
    QNDEBUG("note_editor", "NoteEditorPrivate::onUndoCommandError: " << error);
    Q_EMIT notifyError(error);
}

void NoteEditorPrivate::onSpellCheckerDictionaryEnabledOrDisabled(bool checked)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onSpellCheckerDictionaryEnabledOrDisabled: "
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

void NoteEditorPrivate::clearCurrentNoteInfo()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::clearCurrentNoteInfo");

    // Remove the no longer needed html file with the note editor page
    if (m_pNote) {
        const QFileInfo noteEditorPageFileInfo(noteEditorPagePath());
        if (noteEditorPageFileInfo.exists() && noteEditorPageFileInfo.isFile())
        {
            Q_UNUSED(removeFile(noteEditorPageFileInfo.absoluteFilePath()))
        }
    }

    m_resourceInfo.clear();
    m_resourceFileStoragePathsByResourceLocalId.clear();
    m_genericResourceImageFilePathsByResourceHash.clear();
    m_saveGenericResourceImageToFileRequestIds.clear();
    m_recognitionIndicesByResourceHash.clear();

    if (m_decryptedTextCache) {
        m_decryptedTextCache->clearNonRememberedForSessionEntries();
    }

    m_lastSearchHighlightedText.resize(0);
    m_lastSearchHighlightedTextCaseSensitivity = false;

    m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile.clear();
    m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.clear();

    m_noteWasNotFound = false;
    m_noteWasDeleted = false;

    m_pendingConversionToNote = false;
    m_pendingConversionToNoteForSavingInLocalStorage = false;

    m_pendingNoteSavingInLocalStorage = false;
    m_shouldRepeatSavingNoteInLocalStorage = false;

    m_pendingNoteImageResourceTemporaryFiles = false;

    m_lastInteractionTimestamp = -1;

    m_webSocketServerPort = 0;

    for (auto & pair: m_prepareResourceForOpeningProgressDialogs) {
        pair.second->accept();
        pair.second->deleteLater();
    }
    m_prepareResourceForOpeningProgressDialogs.clear();
}

void NoteEditorPrivate::reloadCurrentNote()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::reloadCurrentNote");

    if (Q_UNLIKELY(m_noteLocalId.isEmpty())) {
        QNWARNING(
            "note_editor",
            "Can't reload current note - no note is set " << "to the editor");
        return;
    }

    if (Q_UNLIKELY(!(m_pNote && m_pNotebook))) {
        QString noteLocalId = m_noteLocalId;
        m_noteLocalId.clear();
        setCurrentNoteLocalId(noteLocalId);
        return;
    }

    auto note = *m_pNote;
    auto notebook = *m_pNotebook;
    clearCurrentNoteInfo();
    onFoundNoteAndNotebook(note, notebook);
}

void NoteEditorPrivate::clearPrepareResourceForOpeningProgressDialog(
    const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::clearPrepareResourceForOpeningProgressDialog: "
            << "resource local id = " << resourceLocalId);

    const auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalId](const auto & pair) {
            return pair.first == resourceLocalId;
        });

    if (Q_UNLIKELY(it == m_prepareResourceForOpeningProgressDialogs.end())) {
        QNDEBUG(
            "note_editor",
            "Haven't found QProgressDialog for this " << "resource");
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

        QNTRACE(
            "note_editor",
            "Looks like the note editing has stopped for a while, will convert "
                << "the note editor page's content to ENML");

        ErrorString error;
        if (!htmlToNoteContent(error)) {
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

    const auto urls = pMimeData->urls();
    if (urls.isEmpty()) {
        return;
    }

    pEvent->acceptProposedAction();
}

void NoteEditorPrivate::dropEvent(QDropEvent * pEvent)
{
    onDropEvent(pEvent);
}

void NoteEditorPrivate::getHtmlForPrinting()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::getHtmlForPrinting");

    GET_PAGE()

    page->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceivedForPrinting));
}

void NoteEditorPrivate::onFoundResourceData(
    qevercloud::Resource resource) // NOLINT
{
    const QString & resourceLocalId = resource.localId();

    const auto sit =
        m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile.find(
            resourceLocalId);

    if (sit !=
        m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFoundResourceData: " << "resource local id = "
                                                       << resourceLocalId);
        QNTRACE("note_editor", resource);

        Q_UNUSED(m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile
                     .erase(sit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        auto resources =
            (m_pNote->resources() ? *m_pNote->resources()
                                  : QList<qevercloud::Resource>());

        auto resourceIt = std::find_if(
            resources.begin(), resources.end(),
            [&resourceLocalId](const qevercloud::Resource & resource) {
                return resource.localId() == resourceLocalId;
            });
        if (Q_UNLIKELY(resourceIt == resources.end())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't save attachment data to a file: "
                           "the attachment to be saved was not found "
                           "within the note"));
            QNWARNING(
                "note_editor",
                errorDescription << ", resource local id = "
                                 << resourceLocalId);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QNTRACE("note_editor", "Updating the resource within the note");
        *resourceIt = resource;
        m_pNote->setResources(resources);
        Q_EMIT currentNoteChanged(*m_pNote);

        manualSaveResourceToFile(resource);
    }

    auto iit =
        m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.find(
            resourceLocalId);
    if (iit !=
        m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFoundResourceData: " << "resource local id = "
                                                       << resourceLocalId);
        QNTRACE("note_editor", resource);

        Rotation rotationDirection = iit.value();
        Q_UNUSED(
            m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.erase(
                iit))

        if (Q_UNLIKELY(!m_pNote)) {
            QNDEBUG("note_editor", "No note is set to the editor");
            return;
        }

        if (Q_UNLIKELY(
                !(resource.data() && resource.data()->body() &&
                  resource.data()->bodyHash())))
        {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't rotate image attachment: the image "
                           "attachment has neither data nor data hash"));
            QNWARNING(
                "note_editor", errorDescription << ", resource: " << resource);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        auto resources =
            (m_pNote->resources() ? *m_pNote->resources()
                                  : QList<qevercloud::Resource>());

        auto resourceIt = std::find_if(
            resources.begin(), resources.end(),
            [&resourceLocalId](const qevercloud::Resource & resource) {
                return resource.localId() == resourceLocalId;
            });
        if (Q_UNLIKELY(resourceIt == resources.end())) {
            ErrorString errorDescription(
                QT_TR_NOOP("Can't rotate image attachment: the attachment to "
                           "be rotated was not found within the note"));
            QNWARNING(
                "note_editor",
                errorDescription << ", resource local id = "
                                 << resourceLocalId);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        *resourceIt = resource;
        m_pNote->setResources(resources);

        const QByteArray dataHash =
            (resource.data() && resource.data()->bodyHash())
            ? *resource.data()->bodyHash()
            : QCryptographicHash::hash(
                  *resource.data()->body(), QCryptographicHash::Md5);

        rotateImageAttachment(dataHash, rotationDirection);
    }
}

void NoteEditorPrivate::onFailedToFindResourceData(
    QString resourceLocalId, ErrorString errorDescription) // NOLINT
{
    auto sit =
        m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile.find(
            resourceLocalId);

    if (sit !=
        m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFailedToFindResourceData: "
                << "resource local id = " << resourceLocalId);

        Q_UNUSED(m_resourceLocalIdsPendingFindDataInLocalStorageForSavingToFile
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
            error << ", resource local id = " << resourceLocalId);
        Q_EMIT notifyError(error);
    }

    auto iit =
        m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.find(
            resourceLocalId);

    if (iit !=
        m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.end())
    {
        QNDEBUG(
            "note_editor",
            "NoteEditorPrivate::onFailedToFindResourceData: "
                << "resource local id = " << resourceLocalId);

        Q_UNUSED(
            m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage.erase(
                iit))

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
            error << ", resource local id = " << resourceLocalId);

        Q_EMIT notifyError(error);
    }
}

void NoteEditorPrivate::onFailedToPutResourceDataInTemporaryFile(
    QString resourceLocalId, QString noteLocalId, // NOLINT
    ErrorString errorDescription)                 // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFailedToPutResourceDataInTemporaryFile: "
            << "resource local id = " << resourceLocalId << ", note local id = "
            << noteLocalId << ", error description: " << errorDescription);

    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationProgress(
    double progress, QString noteLocalId) // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationProgress: "
            << "progress = " << progress
            << ", note local id = " << noteLocalId);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationError(
    QString noteLocalId, ErrorString errorDescription) // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteResourceTemporaryFilesPreparationError: "
            << "note local id = " << noteLocalId
            << ", error description: " << errorDescription);

    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onNoteResourceTemporaryFilesReady(
    QString noteLocalId) // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteResourceTemporaryFilesReady: note local id = "
            << noteLocalId);

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

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const QString imageResourceMimePrefix = QStringLiteral("image/");
    for (const auto & resource: std::as_const(resources)) {
        QNTRACE("note_editor", "Processing resource: " << resource);

        if (!resource.mime() ||
            !resource.mime()->startsWith(imageResourceMimePrefix))
        {
            QNTRACE(
                "note_editor",
                "Skipping the resource with inappropriate "
                    << "mime type: "
                    << (resource.mime() ? *resource.mime()
                                        : QStringLiteral("<not set>")));
            continue;
        }

        if (Q_UNLIKELY(!(resource.data() && resource.data()->bodyHash()))) {
            QNTRACE("note_editor", "Skipping the resource without data hash");
            continue;
        }

        if (Q_UNLIKELY(!(resource.data() && resource.data()->size()))) {
            QNTRACE("note_editor", "Skipping the resource without data size");
            continue;
        }

        const QString & resourceLocalId = resource.localId();

        const QString fileStoragePath =
            ResourceDataInTemporaryFileStorageManager::
                imageResourceFileStorageFolderPath() +
            QStringLiteral("/") + noteLocalId + QStringLiteral("/") +
            resourceLocalId + QStringLiteral(".dat");

        ErrorString errorDescription;
        QString linkFilePath = createSymlinkToImageResourceFile(
            fileStoragePath, resourceLocalId, errorDescription);

        if (Q_UNLIKELY(linkFilePath.isEmpty())) {
            QNWARNING("note_editor", errorDescription);
            // Since the proper way has failed, use the improper one as a
            // fallback
            linkFilePath = fileStoragePath;
        }

        m_resourceFileStoragePathsByResourceLocalId[resourceLocalId] =
            linkFilePath;

        const QString displayName = resourceDisplayName(resource);

        const QString displaySize = humanReadableSize(static_cast<quint64>(
            std::max(*resource.data()->size(), qint32(0))));

        QSize resourceImageSize;
        if (resource.height() && resource.width()) {
            resourceImageSize.setHeight(*resource.height());
            resourceImageSize.setWidth(*resource.width());
        }

        m_resourceInfo.cacheResourceInfo(
            *resource.data()->bodyHash(), displayName, displaySize,
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
    double progress, QString resourceLocalId, QString noteLocalId) // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onOpenResourceInExternalEditorPreparationProgress: "
            << "progress = " << progress << ", resource local id = "
            << resourceLocalId << ", note local id = " << noteLocalId);

    const auto it = std::find_if(
        m_prepareResourceForOpeningProgressDialogs.begin(),
        m_prepareResourceForOpeningProgressDialogs.end(),
        [&resourceLocalId](const auto & pair) {
            return pair.first == resourceLocalId;
        });

    if (Q_UNLIKELY(it == m_prepareResourceForOpeningProgressDialogs.end())) {
        QNDEBUG(
            "note_editor", "Haven't found QProgressDialog for this resource");
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
    QString resourceLocalId, QString noteLocalId, // NOLINT
    ErrorString errorDescription)                 // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::onFailedToOpenResourceInExternalEditor: resource local id = "
            << resourceLocalId << ", note local id = " << noteLocalId
            << ", error description = " << errorDescription);

    clearPrepareResourceForOpeningProgressDialog(resourceLocalId);
    Q_EMIT notifyError(errorDescription);
}

void NoteEditorPrivate::onOpenedResourceInExternalEditor(
    QString resourceLocalId, QString noteLocalId) // NOLINT
{
    if (!m_pNote || (m_pNote->localId() != noteLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onOpenedResourceInExternalEditor: "
            << "resource local id = " << resourceLocalId
            << ", note local id = " << noteLocalId);

    clearPrepareResourceForOpeningProgressDialog(resourceLocalId);
}

void NoteEditorPrivate::init()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::init");

    CHECK_ACCOUNT(QT_TR_NOOP("Can't initialize the note editor"))

    const QString accountName = m_pAccount->name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't initialize the note editor: account "
                       "name is empty"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    const QString storagePath = accountPersistentStoragePath(*m_pAccount);
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

void NoteEditorPrivate::onNoteSavedToLocalStorage(QString noteLocalId) // NOLINT
{
    if (!m_pendingNoteSavingInLocalStorage || !m_pNote ||
        (m_pNote->localId() != noteLocalId))
    {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNoteSavedToLocalStorage: " << "note local id = "
                                                         << noteLocalId);

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

    Q_EMIT noteSavedToLocalStorage(noteLocalId);
}

void NoteEditorPrivate::onFailedToSaveNoteToLocalStorage(
    QString noteLocalId, ErrorString errorDescription) // NOLINT
{
    if (!m_pendingNoteSavingInLocalStorage || !m_pNote ||
        (m_pNote->localId() != noteLocalId))
    {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFailedToSaveNoteToLocalStorage: note local id = "
            << noteLocalId << ", error description: " << errorDescription);

    m_pendingNoteSavingInLocalStorage = false;
    m_shouldRepeatSavingNoteInLocalStorage = false;

    Q_EMIT failedToSaveNoteToLocalStorage(errorDescription, noteLocalId);
}

void NoteEditorPrivate::onFoundNoteAndNotebook(
    qevercloud::Note note, qevercloud::Notebook notebook) // NOLINT
{
    if (note.localId() != m_noteLocalId) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onFoundNoteAndNotebook: note = "
            << note << "\nNotebook = " << notebook);

    m_pNotebook = std::make_unique<qevercloud::Notebook>(notebook);
    m_pNote = std::make_unique<qevercloud::Note>(note);

    rebuildRecognitionIndicesCache();

    if (m_webSocketServerPort == 0) {
        setupWebSocketServer();
    }

    if (!m_setUpJavaScriptObjects) {
        setupJavaScriptObjects();
    }

    Q_EMIT noteAndNotebookFoundInLocalStorage(*m_pNote, *m_pNotebook);

    Q_EMIT currentNoteChanged(*m_pNote);
    noteToEditorContent();
    QNTRACE("note_editor", "Done setting the current note and notebook");
}

void NoteEditorPrivate::onFailedToFindNoteOrNotebook(
    QString noteLocalId, ErrorString errorDescription) // NOLINT
{
    if (noteLocalId != m_noteLocalId) {
        return;
    }

    QNWARNING(
        "note_editor",
        "NoteEditorPrivate::onFailedToFindNoteOrNotebook: "
            << "note local id = " << noteLocalId
            << ", error description: " << errorDescription);

    m_noteLocalId.clear();
    m_noteWasNotFound = true;
    Q_EMIT noteNotFound(noteLocalId);

    clearEditorContent(BlankPageKind::NoteNotFound);
}

void NoteEditorPrivate::onNoteUpdated(qevercloud::Note note) // NOLINT
{
    if (note.localId() != m_noteLocalId) {
        return;
    }

    QNDEBUG("note_editor", "NoteEditorPrivate::onNoteUpdated: " << note);

    if (Q_UNLIKELY(!m_pNote)) {
        if (m_pNotebook) {
            QNDEBUG(
                "note_editor",
                "Current note is unexpectedly empty on note "
                    << "update, acting as if the note has just been found");
            qevercloud::Notebook notebook = *m_pNotebook;
            onFoundNoteAndNotebook(note, notebook);
        }
        else {
            QNWARNING(
                "note_editor",
                "Can't handle the update of note: note editor contains neither "
                    << "note nor notebook");
            // Trying to recover through re-requesting note and notebook
            // from the local storage
            m_noteLocalId.clear();
            setCurrentNoteLocalId(note.localId());
        }

        return;
    }

    if (Q_UNLIKELY(note.notebookLocalId().isEmpty())) {
        QNWARNING(
            "note_editor",
            "Can't handle the update of a note: the updated note has no "
                << "notebook local id: " << note);
        return;
    }

    if (Q_UNLIKELY(!m_pNotebook) ||
        (m_pNotebook->localId() != note.notebookLocalId()))
    {
        QNDEBUG(
            "note_editor",
            "Note's notebook has changed: new notebook local id = "
                << note.notebookLocalId());

        // Re-requesting both note and notebook from
        // NoteEditorLocalStorageBroker
        QString noteLocalId = m_noteLocalId;
        clearCurrentNoteInfo();
        Q_EMIT findNoteAndNotebook(noteLocalId);
        return;
    }

    bool noteChanged = (m_pNote->content() != note.content()) ||
        (m_pNote->resources() != note.resources());

    if (!noteChanged && m_pNote->resources() && note.resources()) {
        const auto currentResources = *m_pNote->resources();
        const auto updatedResources = *note.resources();

        noteChanged = (currentResources.size() != updatedResources.size());
        if (!noteChanged) {
            // NOTE: clearing out data bodies before comparing resources
            // to speed up the comparison
            for (auto it = currentResources.constBegin(),
                      uit = updatedResources.constBegin(),
                      end = currentResources.constEnd(),
                      uend = updatedResources.constEnd();
                 it != end && uit != uend; ++it, ++uit)
            {
                auto currentResource = *it;

                if (currentResource.data()) {
                    currentResource.mutableData()->setBody(std::nullopt);
                }
                if (currentResource.alternateData()) {
                    currentResource.mutableAlternateData()->setBody(
                        std::nullopt);
                }

                auto updatedResource = *uit;
                if (updatedResource.data()) {
                    updatedResource.mutableData()->setBody(std::nullopt);
                }
                if (updatedResource.alternateData()) {
                    updatedResource.mutableAlternateData()->setBody(
                        std::nullopt);
                }

                if (currentResource != updatedResource) {
                    noteChanged = true;
                    break;
                }
            }
        }
    }

    if (!noteChanged) {
        QNDEBUG(
            "note_editor",
            "Haven't found the updates within the note which would be "
                << "sufficient enough to reload the note in the editor");
        *m_pNote = note;
        return;
    }

    // FIXME: if the note was modified, need to let the user choose what to do -
    // either continue to edit the note or reload it

    QNDEBUG(
        "note_editor",
        "Note has changed substantially, need to reload the editor");
    *m_pNote = note;
    reloadCurrentNote();
}

void NoteEditorPrivate::onNotebookUpdated(
    qevercloud::Notebook notebook) // NOLINT
{
    if (!m_pNotebook || (m_pNotebook->localId() != notebook.localId())) {
        return;
    }

    QNDEBUG("note_editor", "NoteEditorPrivate::onNotebookUpdated");

    bool restrictionsChanged = false;
    if (m_pNotebook->restrictions() != notebook.restrictions()) {
        restrictionsChanged = true;
    }
    else if (m_pNotebook->restrictions() && notebook.restrictions()) {
        const auto & previousRestrictions = *m_pNotebook->restrictions();
        const bool previousCanUpdateNote =
            (!previousRestrictions.noUpdateNotes() ||
             !*previousRestrictions.noUpdateNotes());

        const auto & newRestrictions = *notebook.restrictions();
        const bool newCanUpdateNote =
            (!newRestrictions.noUpdateNotes() ||
             !*newRestrictions.noUpdateNotes());

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
    if (m_pNotebook->restrictions()) {
        const auto & restrictions = *notebook.restrictions();
        canUpdateNote =
            (!restrictions.noUpdateNotes() || !*restrictions.noUpdateNotes());
    }

    if (!canUpdateNote && m_isPageEditable) {
        QNDEBUG("note_editor", "Note has become non-editable");
        setPageEditable(false);
        return;
    }

    if (canUpdateNote && !m_isPageEditable) {
        if (m_pNote->active() && !*m_pNote->active()) {
            QNDEBUG(
                "note_editor",
                "Notebook no longer restricts the update of "
                    << "a note but the note is not active");
            return;
        }

        if (isInkNote(*m_pNote)) {
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

void NoteEditorPrivate::onNoteDeleted(QString noteLocalId) // NOLINT
{
    if (m_noteLocalId != noteLocalId) {
        return;
    }

    QNDEBUG("note_editor", "NoteEditorPrivate::onNoteDeleted: " << noteLocalId);

    Q_EMIT noteDeleted(m_noteLocalId);

    // FIXME: need to display the dedicated note editor page about the fact that
    // the note has been deleted
    // FIXME: if the note editor has been marked as modified, need to offer the
    // option to the user to save their edits as a new note

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);
    m_noteLocalId = QString();
    clearCurrentNoteInfo();
    m_noteWasDeleted = true;
    clearEditorContent(BlankPageKind::NoteDeleted);
}

void NoteEditorPrivate::onNotebookDeleted(QString notebookLocalId) // NOLINT
{
    if (!m_pNotebook || (m_pNotebook->localId() != notebookLocalId)) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::onNotebookDeleted: " << notebookLocalId);

    Q_EMIT noteDeleted(m_noteLocalId);

    // FIXME: need to display the dedicated note editor page about the fact that
    // the note has been deleted
    // FIXME: if the note editor has been marked as modified, need to offer the
    // option to the user to save their edits as a new note

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);
    m_noteLocalId = QString();
    clearCurrentNoteInfo();
    m_noteWasDeleted = true;
    clearEditorContent(BlankPageKind::NoteDeleted);
}

void NoteEditorPrivate::handleHyperlinkClicked(const QUrl & url)
{
    const QString urlString = url.toString();

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
    if (!parseInAppLink(urlString, userId, shardId, noteGuid, errorDescription))
    {
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

    static const QRegularExpression regex{
        QStringLiteral("evernote:///view/([^/]+)/([^/]+)/([^/]+)(/.*)?")};

    const auto match = regex.match(urlString);
    if (!match.hasMatch()) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't process the in-app note link: "
                       "failed to parse the note guid from the link"));
        errorDescription.details() = urlString;
        return false;
    }

    const QStringList capturedTexts = match.capturedTexts();
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

    const qint64 noteSize = noteResourcesSize() + newNoteContent.size();

    QNTRACE(
        "note_editor",
        "New note content size = " << newNoteContent.size()
                                   << ", total note size = " << noteSize);

    if (m_pNote->limits()) {
        const qevercloud::NoteLimits & noteLimits = *m_pNote->limits();
        QNTRACE(
            "note_editor",
            "Note has its own limits, will use them to "
                << "check the note size: " << noteLimits);

        if (noteLimits.noteSizeMax() &&
            (Q_UNLIKELY(*noteLimits.noteSizeMax() < noteSize)))
        {
            errorDescription.setBase(
                QT_TR_NOOP("Note size (text + resources) "
                           "exceeds the allowed maximum"));
            errorDescription.details() = humanReadableSize(
                static_cast<quint64>(*noteLimits.noteSizeMax()));
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

    if (Q_UNLIKELY(!m_pUndoStack)) {
        QNWARNING(
            "note_editor",
            "Ignoring the content changed signal as the undo stack "
                << "is not set");
        return;
    }

    if (Q_UNLIKELY(!m_pNote)) {
        QNINFO(
            "note_editor",
            "Ignoring the content changed signal as the note "
                << "pointer is null");
        return;
    }

    const auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    auto * pCommand = new NoteEditorContentEditUndoCommand(*this, resources);
    QObject::connect(
        pCommand, &NoteEditorContentEditUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::pushTableActionUndoCommand(
    const QString & name, NoteEditorPage::Callback callback)
{
    auto * pCommand =
        new TableActionUndoCommand(*this, name, std::move(callback));

    QObject::connect(
        pCommand, &TableActionUndoCommand::notifyError, this,
        &NoteEditorPrivate::onUndoCommandError);

    m_pUndoStack->push(pCommand);
}

void NoteEditorPrivate::pushInsertHtmlUndoCommand(
    const QList<qevercloud::Resource> & addedResources,
    const QStringList & resourceFileStoragePaths)
{
    auto * pCommand = new InsertHtmlUndoCommand(
        NoteEditorCallbackFunctor<QVariant>(
            this, &NoteEditorPrivate::onInsertHtmlUndoRedoFinished),
        *this, m_resourceFileStoragePathsByResourceLocalId, m_resourceInfo,
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

    const auto resultMap = result.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of managed page "
                       "action execution attempt"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        QString errorMessage;
        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    provideSrcAndOnClickScriptForImgEnCryptTags();
    setupGenericResourceImages();

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
        fontSizes = QFontDatabase::standardSizes();
    }

    auto fontSizeIndex = fontSizes.indexOf(fontSize);
    if (fontSizeIndex < 0) {
        QNTRACE(
            "note_editor",
            "Couldn't find font size "
                << fontSize << " within the available sizes, will take "
                << "the closest one instead");
        const auto numFontSizes = fontSizes.size();
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

    QString escapedTextToFind = textToFind;
    escapeStringForJavaScript(escapedTextToFind);

    // The order of used parameters to window.find: text to find, match case
    // (bool), search backwards (bool), wrap the search around (bool)
    const QString javascript = QStringLiteral("window.find('") +
        escapedTextToFind + QStringLiteral("', ") +
        (matchCase ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral(", ") +
        (searchBackward ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral(", true);");

    page->executeJavaScript(javascript, std::move(callback));

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

        const auto recoIndexItems = recoIndices.items();
        for (const auto & recoIndexItem: std::as_const(recoIndexItems)) {
            const auto textItems = recoIndexItem.textItems();
            const auto textItemIt = std::find_if(
                textItems.constBegin(), textItems.constEnd(),
                [&](const ResourceRecognitionIndexItem::ITextItemPtr &
                        textItem) {
                    if (Q_UNLIKELY(!textItem)) {
                        QNWARNING(
                            "note_editor",
                            "Detected null resource recognition indeex item");
                        return false;
                    }

                    return textItem->text().contains(
                        textToFind,
                        matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
                });

            if (textItemIt != textItems.constEnd()) {
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

    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Cannot fetch note content"))

    if (isInkNote(*m_pNote)) {
        inkNoteToEditorContent();
        return;
    }

    QString noteContent;
    if (m_pNote->content()) {
        noteContent = *m_pNote->content();
    }
    else {
        QNDEBUG(
            "note_editor",
            "Note without content was inserted into "
                << "the NoteEditor, setting up the empty note content");
        noteContent = QStringLiteral("<en-note><div></div></en-note>");
    }

    m_htmlCachedMemory.resize(0);

    const auto res =
        m_enmlConverter->convertEnmlToHtml(noteContent, *m_decryptedTextCache);
    if (!res.isValid()) {
        QNWARNING("note_editor", res.error());
        clearEditorContent(BlankPageKind::InternalError, res.error());
        Q_EMIT notifyError(res.error());
        return;
    }

    const auto & htmlData = res.get();
    Q_ASSERT(htmlData);

    m_lastFreeEnToDoIdNumber = htmlData->numEnToDoNodes() + 1;
    m_lastFreeHyperlinkIdNumber = htmlData->numHyperlinkNodes() + 1;
    m_lastFreeEnCryptIdNumber = htmlData->numEnCryptNodes() + 1;
    m_lastFreeEnDecryptedIdNumber = htmlData->numEnDecryptedNodes() + 1;

    m_htmlCachedMemory = htmlData->html();

    const auto bodyTagIndex =
        m_htmlCachedMemory.indexOf(QStringLiteral("<body"));

    if (bodyTagIndex < 0) {
        ErrorString error{
            QT_TR_NOOP("Can't find <body> tag in the result of note "
                       "to HTML conversion")};
        QNWARNING(
            "note_editor",
            error << ", note content: " << *m_pNote->content()
                  << ", html: " << m_htmlCachedMemory);
        clearEditorContent(BlankPageKind::InternalError, error);
        Q_EMIT notifyError(error);
        return;
    }

    QString pagePrefix = noteEditorPagePrefix();
    m_htmlCachedMemory.replace(0, bodyTagIndex, pagePrefix);

    const auto bodyClosingTagIndex =
        m_htmlCachedMemory.indexOf(QStringLiteral("</body>"));

    if (bodyClosingTagIndex < 0) {
        ErrorString error{
            QT_TR_NOOP("Can't find </body> tag in the result of note "
                       "to HTML conversion")};
        QNWARNING(
            "note_editor",
            error << ", note content: " << *m_pNote->content()
                  << ", html: " << m_htmlCachedMemory);
        clearEditorContent(BlankPageKind::InternalError, error);
        Q_EMIT notifyError(error);
        return;
    }

    m_htmlCachedMemory.insert(
        bodyClosingTagIndex + 7, QStringLiteral("</html>"));

    m_htmlCachedMemory.replace(
        QStringLiteral("<br></br>"), QStringLiteral("</br>"));

    QNTRACE("note_editor", "Note page HTML: " << m_htmlCachedMemory);
    writeNotePageFile(m_htmlCachedMemory);
}

void NoteEditorPrivate::updateColResizableTableBindings()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::updateColResizableTableBindings");

    const bool readOnly = !isPageEditable();

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

    const auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    QString inkNoteHtml = noteEditorPagePrefix();
    inkNoteHtml += QStringLiteral("<body>");

    for (const auto & resource: std::as_const(resources)) {
        if (!resource.guid()) {
            QNWARNING(
                "note_editor",
                "Detected ink note which has at least one "
                    << "resource without guid: note = " << *m_pNote
                    << "\nResource: " << resource);
            problemDetected = true;
            break;
        }

        if (!resource.data() || !resource.data()->bodyHash()) {
            QNWARNING(
                "note_editor",
                "Detected ink note which has at least one "
                    << "resource without data hash: note = " << *m_pNote
                    << "\nResource: " << resource);
            problemDetected = true;
            break;
        }

        const QFileInfo inkNoteImageFileInfo(
            m_noteEditorPageFolderPath + QStringLiteral("/inkNoteImages/") +
            *resource.guid() + QStringLiteral(".png"));

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
                "Unable to escape the ink note image file path: "
                    << inkNoteImageFileInfo.absoluteFilePath());
            problemDetected = true;
            break;
        }

        inkNoteHtml += QStringLiteral("<img src=\"");
        inkNoteHtml += inkNoteImageFilePath;
        inkNoteHtml += QStringLiteral("\" ");

        if (resource.height()) {
            inkNoteHtml += QStringLiteral("height=");
            inkNoteHtml += QString::number(*resource.height());
            inkNoteHtml += QStringLiteral(" ");
        }

        if (resource.width()) {
            inkNoteHtml += QStringLiteral("width=");
            inkNoteHtml += QString::number(*resource.width());
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

    if (m_pNote->active() && !*m_pNote->active()) {
        errorDescription.setBase(
            QT_TR_NOOP("Current note is marked as read-only, "
                       "the changes won't be saved"));

        QNINFO(
            "note_editor",
            errorDescription << ", note: local id = " << m_pNote->localId()
                             << ", guid = "
                             << (m_pNote->guid() ? *m_pNote->guid()
                                                 : QStringLiteral("<null>"))
                             << ", title = "
                             << (m_pNote->title() ? *m_pNote->title()
                                                  : QStringLiteral("<null>")));

        Q_EMIT cantConvertToNote(errorDescription);
        return false;
    }

    if (m_pNotebook && m_pNotebook->restrictions()) {
        const auto & restrictions = *m_pNotebook->restrictions();
        if (restrictions.noUpdateNotes() && *restrictions.noUpdateNotes()) {
            errorDescription.setBase(
                QT_TR_NOOP("The notebook the current note belongs to doesn't "
                           "allow notes modification, the changes won't be "
                           "saved"));

            QNINFO(
                "note_editor",
                errorDescription
                    << ", note: local id = " << m_pNote->localId()
                    << ", guid = "
                    << (m_pNote->guid() ? *m_pNote->guid()
                                        : QStringLiteral("<null>"))
                    << ", title = "
                    << (m_pNote->title() ? *m_pNote->title()
                                         : QStringLiteral("<null>"))
                    << ", notebook: local id = " << m_pNotebook->localId()
                    << ", guid = "
                    << (m_pNotebook->guid() ? *m_pNotebook->guid()
                                            : QStringLiteral("<null>"))
                    << ", name = "
                    << (m_pNotebook->name() ? *m_pNotebook->name()
                                            : QStringLiteral("<null>")));
            Q_EMIT cantConvertToNote(errorDescription);
            return false;
        }
    }

    m_pendingConversionToNote = true;

    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));

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

void NoteEditorPrivate::manualSaveResourceToFile(
    const qevercloud::Resource & resource)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::manualSaveResourceToFile");

    if (Q_UNLIKELY(
            !((resource.data() && resource.data()->body()) ||
              (resource.alternateData() && resource.alternateData()->body()))))
    {
        ErrorString error(
            QT_TR_NOOP("Can't save resource to file: resource has "
                       "neither data body nor alternate data body"));
        QNINFO("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.mime())) {
        ErrorString error(
            QT_TR_NOOP("Can't save resource to file: resource has "
                       "no mime type"));
        QNINFO("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    const QString resourcePreferredSuffix = preferredFileSuffix(resource);
    QString resourcePreferredFilterString;
    if (!resourcePreferredSuffix.isEmpty()) {
        resourcePreferredFilterString = QStringLiteral("(*.") +
            resourcePreferredSuffix + QStringLiteral(")");
    }

    const QString & mimeTypeName = *resource.mime();

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
            for (const auto & suffix: std::as_const(suffixes)) {
                if (resourcePreferredSuffix.contains(suffix)) {
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
        const QStringList childGroups = appSettings.childGroups();
        const auto attachmentsSaveLocGroupIndex =
            childGroups.indexOf(NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY);
        if (attachmentsSaveLocGroupIndex >= 0) {
            QNTRACE(
                "note_editor",
                "Found cached attachment save location "
                    << "group within application settings");

            appSettings.beginGroup(NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY);
            const auto cachedFileSuffixes = appSettings.childKeys();
            for (const auto & preferredSuffix: std::as_const(preferredSuffixes))
            {
                const auto indexInCache =
                    cachedFileSuffixes.indexOf(preferredSuffix);

                if (indexInCache < 0) {
                    QNTRACE(
                        "note_editor",
                        "Haven't found cached attachment "
                            << "save directory for file suffix "
                            << preferredSuffix);
                    continue;
                }

                const QVariant dirValue = appSettings.value(preferredSuffix);
                if (dirValue.isNull() || !dirValue.isValid()) {
                    QNTRACE(
                        "note_editor",
                        "Found inappropriate attachment "
                            << "save directory for file suffix "
                            << preferredSuffix);
                    continue;
                }

                const QFileInfo dirInfo{dirValue.toString()};
                if (!dirInfo.exists()) {
                    QNTRACE(
                        "note_editor",
                        "Cached attachment save directory "
                            << "for file suffix " << preferredSuffix
                            << " does not exist: " << dirInfo.absolutePath());
                    continue;
                }

                if (!dirInfo.isDir()) {
                    QNTRACE(
                        "note_editor",
                        "Cached attachment save directory "
                            << "for file suffix " << preferredSuffix
                            << " is not a directory: "
                            << dirInfo.absolutePath());
                    continue;
                }

                if (!dirInfo.isWritable()) {
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
    for (const auto & currentSuffix: std::as_const(preferredSuffixes)) {
        if (absoluteFilePath.endsWith(currentSuffix, Qt::CaseInsensitive)) {
            foundSuffix = true;
            break;
        }
    }

    if (!foundSuffix) {
        absoluteFilePath += QStringLiteral(".") + preferredSuffix;
    }

    QUuid saveResourceToFileRequestId = QUuid::createUuid();

    const QByteArray & data = (resource.data() && resource.data()->body())
        ? *resource.data()->body()
        : resource.alternateData().value().body().value();

    Q_UNUSED(m_manualSaveResourceToFileRequestIds.insert(
        saveResourceToFileRequestId));

    Q_EMIT saveResourceToFile(
        absoluteFilePath, data, saveResourceToFileRequestId,
        /* append = */ false);

    QNDEBUG(
        "note_editor",
        "Sent request to manually save resource to file, "
            << "request id = " << saveResourceToFileRequestId
            << ", resource local id = " << resource.localId());
}

QImage NoteEditorPrivate::buildGenericResourceImage(
    const qevercloud::Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::buildGenericResourceImage: "
            << "resource local id = " << resource.localId());

    QString displayName = resourceDisplayName(resource);
    if (Q_UNLIKELY(displayName.isEmpty())) {
        displayName = tr("Attachment");
    }

    QNTRACE("note_editor", "Resource display name = " << displayName);

    QFont font = m_font;
    font.setPointSize(10);

    QString originalResourceDisplayName = displayName;

    const int maxResourceDisplayNameWidth = 146;
    QFontMetrics fontMetrics(font);
    int width = fontMetricsWidth(fontMetrics, displayName);

    const int singleCharWidth =
        fontMetricsWidth(fontMetrics, QStringLiteral("n"));

    const int ellipsisWidth =
        fontMetricsWidth(fontMetrics, QStringLiteral("..."));

    bool smartReplaceWorked = true;
    int previousWidth = width + 1;

    while (width > maxResourceDisplayNameWidth) {
        if (width >= previousWidth) {
            smartReplaceWorked = false;
            break;
        }

        previousWidth = width;

        const int widthOverflow = width - maxResourceDisplayNameWidth;
        const int numCharsToSkip =
            (widthOverflow + ellipsisWidth) / singleCharWidth + 1;

        const auto dotIndex = displayName.lastIndexOf(QStringLiteral("."));
        if (dotIndex != 0 && (dotIndex > displayName.size() / 2)) {
            // Try to shorten the name while preserving the file extension.
            // Need to skip some chars before the dot index
            auto startSkipPos = dotIndex - numCharsToSkip;
            if (startSkipPos >= 0) {
                displayName.replace(
                    startSkipPos, numCharsToSkip, QStringLiteral("..."));
                width = fontMetricsWidth(fontMetrics, displayName);
                continue;
            }
        }

        // Either no file extension or name contains a dot, skip some chars
        // without attempt to preserve the file extension
        displayName.replace(
            displayName.size() - numCharsToSkip, numCharsToSkip,
            QStringLiteral("..."));

        width = fontMetricsWidth(fontMetrics, displayName);
    }

    if (!smartReplaceWorked) {
        QNTRACE(
            "note_editor",
            "Wasn't able to shorten the resource name "
                << "nicely, will try to shorten it just somehow");

        width = fontMetricsWidth(fontMetrics, originalResourceDisplayName);
        const int widthOverflow = width - maxResourceDisplayNameWidth;
        const int numCharsToSkip =
            (widthOverflow + ellipsisWidth) / singleCharWidth + 1;
        displayName = originalResourceDisplayName;

        if (displayName.size() > numCharsToSkip) {
            displayName.replace(
                displayName.size() - numCharsToSkip, numCharsToSkip,
                QStringLiteral("..."));
        }
        else {
            displayName = QStringLiteral("Attachment...");
        }
    }

    QNTRACE(
        "note_editor",
        "(possibly) shortened resource display name: "
            << displayName
            << ", width = " << fontMetricsWidth(fontMetrics, displayName));

    QString resourceHumanReadableSize;
    if ((resource.data() && resource.data()->size()) ||
        (resource.alternateData() && resource.alternateData()->size()))
    {
        resourceHumanReadableSize = humanReadableSize(
            (resource.data() && resource.data()->size())
                ? static_cast<quint64>(*resource.data()->size())
                : static_cast<quint64>(*resource.alternateData()->size()));
    }

    QIcon resourceIcon;
    bool useFallbackGenericResourceIcon = false;

    if (resource.mime()) {
        const auto & resourceMimeTypeName = *resource.mime();
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
    painter.drawText(QPoint(28, 14), displayName);

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

void NoteEditorPrivate::saveGenericResourceImage(
    const qevercloud::Resource & resource, const QImage & image)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::saveGenericResourceImage: "
            << "resource local id = " << resource.localId());

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't save the generic resource image: "
                       "no note is set to the editor"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(
            !((resource.data() && resource.data()->bodyHash()) ||
              (resource.alternateData() &&
               resource.alternateData()->bodyHash()))))
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
            << "for resource with local id " << resource.localId()
            << ", request id " << requestId);

    Q_EMIT saveGenericResourceImageToFile(
        m_pNote->localId(), resource.localId(), imageData,
        QStringLiteral("png"),
        ((resource.data() && resource.data()->bodyHash())
             ? *resource.data()->bodyHash()
             : resource.alternateData().value().bodyHash().value()),
        resourceDisplayName(resource), requestId);
}

void NoteEditorPrivate::provideSrcAndOnClickScriptForImgEnCryptTags()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::provideSrcAndOnClickScriptForImgEnCryptTags");

    if (Q_UNLIKELY(!m_pNote)) {
        QNTRACE("note_editor", "No note is set for the editor");
        return;
    }

    const QString iconPath =
        QStringLiteral("qrc:/encrypted_area_icons/en-crypt/en-crypt.png");

    const QString javascript =
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

    if (!m_pNote->resources() || m_pNote->resources()->isEmpty()) {
        QNDEBUG("note_editor", "Note has no resources, nothing to do");
        return;
    }

    QString mimeTypeName;
    size_t resourceImagesCounter = 0;
    bool shouldWaitForResourceImagesToSave = false;

    auto resources = *m_pNote->resources();
    for (const auto & resource: std::as_const(resources)) {
        if (resource.mime()) {
            mimeTypeName = *resource.mime();
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
    const qevercloud::Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::findOrBuildGenericResourceImage: " << resource);

    if ((!resource.data() || !resource.data()->bodyHash()) &&
        (!resource.alternateData() || !resource.alternateData()->bodyHash()))
    {
        ErrorString errorDescription(
            QT_TR_NOOP("Found resource without either data hash or alternate "
                       "data hash"));
        QNWARNING("note_editor", errorDescription << ": " << resource);
        Q_EMIT notifyError(errorDescription);
        return true;
    }

    const QString & localId = resource.localId();

    const QByteArray & resourceHash =
        (resource.data() && resource.data()->bodyHash())
        ? *resource.data()->bodyHash()
        : resource.alternateData().value().bodyHash().value();

    QNTRACE(
        "note_editor",
        "Looking for existing generic resource image file "
            << "for resource with hash " << resourceHash.toHex());

    const auto it =
        m_genericResourceImageFilePathsByResourceHash.find(resourceHash);

    if (it != m_genericResourceImageFilePathsByResourceHash.end()) {
        QNTRACE(
            "note_editor",
            "Found generic resource image file path "
                << "for resource with hash " << resourceHash.toHex()
                << " and local id " << localId << ": " << it.value());
        return false;
    }

    const QImage img = buildGenericResourceImage(resource);
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
        "note_editor", "NoteEditorPrivate::provideSrcForGenericResourceImages");

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("provideSrcForGenericResourceImages();"));
}

void NoteEditorPrivate::setupGenericResourceOnClickHandler()
{
    QNDEBUG(
        "note_editor", "NoteEditorPrivate::setupGenericResourceOnClickHandler");

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
            "Closed the already established web socket " << "server");
        m_webSocketReady = false;
    }

    if (!m_pWebSocketServer->listen(QHostAddress::LocalHost, 0)) {
        ErrorString error(QT_TR_NOOP("Can't open web socket server"));
        error.details() = m_pWebSocketServer->errorString();
        QNERROR("note_editor", error);
        throw RuntimeError(error);
    }

    m_webSocketServerPort = m_pWebSocketServer->serverPort();
    QNDEBUG(
        "note_editor",
        "Using automatically selected websocket server port "
            << m_webSocketServerPort);

    QObject::connect(
        m_pWebSocketClientWrapper, &WebSocketClientWrapper::clientConnected,
        m_pWebChannel, &QWebChannel::connectTo,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::DirectConnection));
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

void NoteEditorPrivate::updateResource(
    const QString & resourceLocalId, const QByteArray & previousResourceHash,
    qevercloud::Resource updatedResource)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::updateResource: resource local id = "
            << resourceLocalId
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

    if (Q_UNLIKELY(!m_pNote->resources() || m_pNote->resources()->isEmpty()))
    { // NOLINT
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: no resources within "
                       "the note in the note editor"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(updatedResource.noteLocalId().isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated "
                       "resource has no note local id"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!updatedResource.mime())) {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated "
                       "resource has no mime type"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!(updatedResource.data() && updatedResource.data()->body())))
    {
        ErrorString error(
            QT_TR_NOOP("Can't update the resource: the updated resource "
                       "contains no data body"));
        QNWARNING(
            "note_editor", error << ", updated resource: " << updatedResource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!updatedResource.data()->bodyHash()) {
        updatedResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *updatedResource.data()->body(), QCryptographicHash::Md5));

        QNDEBUG(
            "note_editor",
            "Set updated resource's data hash to "
                << updatedResource.data()->bodyHash()->toHex());
    }

    if (!updatedResource.data()->size()) {
        updatedResource.mutableData()->setSize(
            updatedResource.data()->body()->size());

        QNDEBUG(
            "note_editor",
            "Set updated resource's data size to "
                << *updatedResource.data()->size());
    }

    const auto resourceIt = std::find_if(
        m_pNote->mutableResources()->begin(),
        m_pNote->mutableResources()->end(),
        [localId =
             updatedResource.localId()](const qevercloud::Resource & resource) {
            return resource.localId() == localId;
        });

    if (Q_UNLIKELY(resourceIt == m_pNote->mutableResources()->end())) {
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

    *resourceIt = updatedResource;

    Q_UNUSED(m_resourceInfo.removeResourceInfo(previousResourceHash))

    auto recoIt = m_recognitionIndicesByResourceHash.find(previousResourceHash);
    if (recoIt != m_recognitionIndicesByResourceHash.end()) {
        Q_UNUSED(m_recognitionIndicesByResourceHash.erase(recoIt));
    }

    updateHashForResourceTag(
        previousResourceHash, *updatedResource.data()->bodyHash());

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
    for (const auto & item: std::as_const(extraData)) {
        if (!item.startsWith(QStringLiteral("MisSpelledWord_"))) {
            continue;
        }

        misSpelledWord = item.mid(15);
        break;
    }

    if (!misSpelledWord.isEmpty()) {
        m_lastMisSpelledWord = misSpelledWord;

        QStringList correctionSuggestions;
        if (m_pSpellChecker) {
            correctionSuggestions =
                m_pSpellChecker->spellCorrectionSuggestions(misSpelledWord);
        }

        if (!correctionSuggestions.isEmpty()) {
            for (const auto & correctionSuggestion:
                 std::as_const(correctionSuggestions))
            {
                if (Q_UNLIKELY(correctionSuggestion.isEmpty())) {
                    continue;
                }

                auto * action = new QAction(
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
        const bool res = parseEncryptedTextContextMenuExtraData(
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
        "NoteEditorPrivate::setupNonImageResourceContextMenu: resource hash = "
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
            "Clipboard buffer has something, adding paste " << "action");

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
            "Can't set shortcut to the action: no account is set to the note "
                << "editor");
        return;
    }

    ShortcutManager shortcutManager;

    const QKeySequence shortcut =
        shortcutManager.shortcut(key, *m_pAccount, context);

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
}

void NoteEditorPrivate::setupSpellChecker()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupSpellChecker");

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNWARNING(
            "note_editor",
            "Cannot setup spell checker as it was not passed to note editor");
        return;
    }

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

    SETUP_SCRIPT("javascript/scripts/setupActions.js", m_setupActionsJs);

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

    SETUP_SCRIPT("qtwebchannel/qwebchannel.js", m_qWebChannelJs);

    SETUP_SCRIPT(
        "javascript/scripts/qWebChannelSetup.js", m_qWebChannelSetupJs);

    SETUP_SCRIPT("javascript/scripts/enToDoTagsSetup.js", m_setupEnToDoTagsJs);

    SETUP_SCRIPT(
        "javascript/scripts/flipEnToDoCheckboxState.js",
        m_flipEnToDoCheckboxStateJs);

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
#undef SETUP_SCRIPT
}

void NoteEditorPrivate::setupGeneralSignalSlotConnections()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate" << "::setupGeneralSignalSlotConnections");

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

    QObject::connect(
        m_pActionsWatcher, &ActionsWatcher::undoActionToggled, this,
        &NoteEditorPrivate::undo);

    QObject::connect(
        m_pActionsWatcher, &ActionsWatcher::redoActionToggled, this,
        &NoteEditorPrivate::redo);

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

    auto * page = new NoteEditorPage(*this);

    page->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    page->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    setupNoteEditorPageConnections(page);
    setPage(page);

    QNTRACE("note_editor", "Done setting up new note editor page");
}

void NoteEditorPrivate::setupNoteEditorPageConnections( // NOLINT
    NoteEditorPage * page)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setupNoteEditorPageConnections");

    QObject::connect(
        page, &NoteEditorPage::javaScriptLoaded, this,
        &NoteEditorPrivate::onJavaScriptLoaded);

    QObject::connect(
        page, &NoteEditorPage::loadFinished, this,
        &NoteEditorPrivate::onNoteLoadFinished);

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
         << "<style id=\"bodyStyleTag\" type=\"text/css\">"; // NOLINT
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

    const int pointSize = m_pDefaultFont->pointSize();
    if (pointSize >= 0) {
        strm << pointSize << "pt";
    }
    else {
        strm << m_pDefaultFont->pixelSize() << "px";
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
        "NoteEditorPrivate::setupSkipRulesForHtmlToEnmlConversion");

    m_skipRulesForHtmlToEnmlConversion.reserve(7);

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::StartsWith)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(true)
               .setValue(QStringLiteral("JCLRgrip"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseInsensitive)
               .setIncludeContents(true)
               .setValue(QStringLiteral("hilitorHelper"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(true)
               .setValue(QStringLiteral("image-area-hilitor"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(true)
               .setValue(QStringLiteral("misspell"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(false)
               .setValue(QStringLiteral("rangySelectionBoundary"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(false)
               .setValue(QStringLiteral("ui-resizable-handle"))
               .build();

    m_skipRulesForHtmlToEnmlConversion
        << enml::conversion_rules::createSkipRuleBuilder()
               ->setTarget(
                   enml::conversion_rules::ISkipRule::Target::AttributeValue)
               .setMatchMode(enml::conversion_rules::MatchMode::Contains)
               .setCaseSensitivity(Qt::CaseSensitive)
               .setIncludeContents(true)
               .setValue(QStringLiteral("ui-wrapper"))
               .build();
}

QString NoteEditorPrivate::noteNotFoundPageHtml() const
{
    if (!m_noteNotFoundPageHtml.isEmpty()) {
        return m_noteNotFoundPageHtml;
    }

    const QString text = tr("Failed to find the note in the local storage");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::noteDeletedPageHtml() const
{
    if (!m_noteDeletedPageHtml.isEmpty()) {
        return m_noteDeletedPageHtml;
    }

    const QString text = tr("Note was deleted");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::noteLoadingPageHtml() const
{
    if (!m_noteLoadingPageHtml.isEmpty()) {
        return m_noteLoadingPageHtml;
    }

    const QString text = tr("Loading note...");
    return composeBlankPageHtml(text);
}

QString NoteEditorPrivate::initialPageHtml() const
{
    if (!m_initialPageHtml.isEmpty()) {
        return m_initialPageHtml;
    }

    const QString text =
        tr("Please select some existing note or create a new one");

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

    const QPalette & pal = palette();
    const QColor backgroundColor = pal.color(QPalette::Window).darker(115);

    strm << backgroundColor.name();

    strm << "; color: ";
    const QColor & foregroundColor = pal.color(QPalette::WindowText);
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
         << "<body><div class=\"outer\"><div class=\"middle\">" // NOLINT
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
        "NoteEditorPrivate::determineStatesForCurrentTextCursorPosition");

    const QString javascript = QStringLiteral(
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

    const QString javascript =
        QStringLiteral("determineContextMenuEventTarget(") +
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

    const QString javascript =
        QStringLiteral("document.body.contentEditable='") +
        (editable ? QStringLiteral("true") : QStringLiteral("false")) +
        QStringLiteral("'; document.designMode='") +
        (editable ? QStringLiteral("on") : QStringLiteral("off")) +
        QStringLiteral("'; void 0;");

    page->executeJavaScript(javascript);

    QNTRACE(
        "note_editor",
        "Queued javascript to make page "
            << (editable ? "editable" : "non-editable") << ": " << javascript);

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
    QNTRACE("note_editor", html);
    Q_UNUSED(extraData)

    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Cannot fetch note content"))

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
            Q_EMIT failedToSaveNoteToLocalStorage(error, m_noteLocalId);
        }

        return;
    }

    if (Q_UNLIKELY(isInkNote(*m_pNote))) {
        m_pendingConversionToNote = false;

        QNINFO(
            "note_editor",
            "Currently selected note is an ink note, it's not editable hence "
                << "won't respond to the unexpected change of its HTML");

        Q_EMIT convertedToNote(*m_pNote);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;
            // Pretend the note was actually saved to local storage
            Q_EMIT noteSavedToLocalStorage(m_noteLocalId);
        }

        return;
    }

    m_lastSelectedHtml.resize(0);

    m_htmlCachedMemory = html;
    m_enmlCachedMemory.resize(0);

    const auto res = m_enmlConverter->convertHtmlToEnml(
        m_htmlCachedMemory, *m_decryptedTextCache,
        m_skipRulesForHtmlToEnmlConversion);
    if (!res.isValid()) {
        ErrorString errorDescription{
            QT_TR_NOOP("Can't convert note editor page's content to ENML")};
        const auto & error = res.error();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        Q_EMIT notifyError(errorDescription);

        m_pendingConversionToNote = false;
        Q_EMIT cantConvertToNote(errorDescription);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;

            Q_EMIT failedToSaveNoteToLocalStorage(
                errorDescription, m_noteLocalId);
        }

        return;
    }

    m_enmlCachedMemory = res.get();

    ErrorString errorDescription;
    if (!checkNoteSize(m_enmlCachedMemory, errorDescription)) {
        m_pendingConversionToNote = false;
        Q_EMIT cantConvertToNote(errorDescription);

        if (m_pendingConversionToNoteForSavingInLocalStorage) {
            m_pendingConversionToNoteForSavingInLocalStorage = false;

            Q_EMIT failedToSaveNoteToLocalStorage(
                errorDescription, m_noteLocalId);
        }

        return;
    }

    m_pNote->setContent(m_enmlCachedMemory);

    if (m_pendingConversionToNoteForSavingInLocalStorage) {
        m_pendingConversionToNoteForSavingInLocalStorage = false;

        if (m_needConversionToNote) {
            m_pNote->setLocallyModified(true);

            m_pNote->setUpdated(QDateTime::currentMSecsSinceEpoch());
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

    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));

    provideSrcAndOnClickScriptForImgEnCryptTags();
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
    const QList<qevercloud::Resource> & resources,
    const QByteArray & resourceHash) const
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::resourceIndexByHash: hash = "
            << resourceHash.toHex());

    const auto resourceIt = std::find_if(
        resources.constBegin(), resources.constEnd(),
        [&resourceHash](const qevercloud::Resource & resource) {
            return resource.data() && resource.data()->bodyHash() &&
                *resource.data()->bodyHash() == resourceHash;
        });
    if (resourceIt != resources.constEnd()) {
        // FIXME: switch to qsizetype after full migration to Qt6
        return static_cast<int>(
            std::distance(resources.constBegin(), resourceIt));
    }

    return -1;
}

void NoteEditorPrivate::writeNotePageFile(const QString & html)
{
    m_writeNoteHtmlToFileRequestId = QUuid::createUuid();
    m_pendingIndexHtmlWritingToFile = true;
    const QString pagePath = noteEditorPagePath();

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

    const auto extraDataSize = extraData.size();
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
        "note_editor", "NoteEditorPrivate::setupPasteGenericTextMenuActions");

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    bool clipboardHasHtml = false;
    bool clipboardHasText = false;
    bool clipboardHasImage = false;
    bool clipboardHasUrls = false;

    const QClipboard * pClipboard = QApplication::clipboard();

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
            "Clipboard buffer has something, adding paste " << "action");

        ADD_ACTION_WITH_SHORTCUT(
            QKeySequence::Paste, tr("Paste"), m_pGenericTextContextMenu, paste,
            m_isPageEditable);
    }

    if (clipboardHasHtml) {
        QNTRACE(
            "note_editor",
            "Clipboard buffer has html, adding paste " << "unformatted action");

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
        "NoteEditorPrivate::setupParagraphSubMenuForGenericTextMenu: "
            << "selected html = " << selectedHtml);

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    if (!isPageEditable()) {
        QNDEBUG(
            "note_editor",
            "Note is not editable, no paragraph sub-menu actions are allowed");
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
        "NoteEditorPrivate::setupStyleSubMenuForGenericTextMenu");

    if (Q_UNLIKELY(!m_pGenericTextContextMenu)) {
        QNDEBUG("note_editor", "No generic text context menu, nothing to do");
        return;
    }

    if (!isPageEditable()) {
        QNDEBUG(
            "note_editor",
            "Note is not editable, no style sub-menu actions are allowed");
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
            "note_editor", "No spell checker was set up for the note editor");
        return;
    }

    const auto availableDictionaries =
        m_pSpellChecker->listAvailableDictionaries();

    if (Q_UNLIKELY(availableDictionaries.isEmpty())) {
        QNDEBUG("note_editor", "The list of available dictionaries is empty");
        return;
    }

    auto * pSpellCheckerDictionariesSubMenu =
        m_pGenericTextContextMenu->addMenu(tr("Spell checker dictionaries"));

    for (const auto & pair: std::as_const(availableDictionaries)) {
        const QString & name = pair.first;

        auto * pAction = new QAction(name, pSpellCheckerDictionariesSubMenu);
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

    if (!m_pNote->resources() || m_pNote->resources()->isEmpty()) {
        QNTRACE("note_editor", "The note has no resources");
        return;
    }

    const auto resources = *m_pNote->resources();
    for (const auto & resource: std::as_const(resources)) {
        if (Q_UNLIKELY(!(resource.data() && resource.data()->bodyHash()))) {
            QNDEBUG(
                "note_editor",
                "Skipping the resource without the data hash: " << resource);
            continue;
        }

        if (!(resource.recognition() && resource.recognition()->body())) {
            QNTRACE(
                "note_editor",
                "Skipping the resource without recognition data body");
            continue;
        }

        const ResourceRecognitionIndices recoIndices{
            *resource.recognition()->body()};

        if (recoIndices.isNull() || !recoIndices.isValid()) {
            QNTRACE(
                "note_editor",
                "Skipping null/invalid resource recognition indices");
            continue;
        }

        m_recognitionIndicesByResourceHash[*resource.data()->bodyHash()] =
            recoIndices;
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
    const QStringList words =
        (m_pNote->content()
             ? noteContentToListOfWords(*m_pNote->content(), &error)
             : QStringList{});

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

    for (const auto & originalWord: std::as_const(words)) {
        QNTRACE("note_editor", "Checking word \"" << originalWord << "\"");

        QString word = originalWord;

        bool conversionResult = false;
        const qint32 integerNumber = word.toInt(&conversionResult);
        if (conversionResult) {
            QNTRACE("note_editor", "Skipping the integer number " << word);
            continue;
        }

        const qint64 longIntegerNumber = word.toLongLong(&conversionResult);
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
                "Skipping the word which becomes empty after stripping off "
                    << "the punctuation: " << originalWord);
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
        "NoteEditorPrivate::applySpellCheck: apply to selection = "
            << (applyToSelection ? "true" : "false"));

    if (m_currentNoteMisSpelledWords.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "The list of current note misspelled words is empty, nothing to "
                << "apply");
        return;
    }

    QString javascript = QStringLiteral(
        "if (window.hasOwnProperty('spellChecker')) "
        "{ spellChecker.apply");

    if (applyToSelection) {
        javascript += QStringLiteral("ToSelection");
    }

    javascript += QStringLiteral("('");
    for (const auto & word: std::as_const(m_currentNoteMisSpelledWords)) {
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
    page->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));
}

void NoteEditorPrivate::updateBodyStyle()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::updateBodyStyle");

    QString css = bodyStyleCss();
    escapeStringForJavaScript(css);

    const QString javascript =
        QString::fromUtf8("replaceStyle('%1');").arg(css);

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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of body "
                       "style replacement from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of font family "
                       "update from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));

    if (Q_UNLIKELY(extraData.empty())) {
        QNWARNING(
            "note_editor",
            "No font family in extra data in JavaScript "
                << "callback after setting font family");
        setModified();
        pushNoteContentEditUndoCommand();
        return;
    }

    const QString fontFamily = extraData[0].second;
    Q_EMIT textFontFamilyChanged(fontFamily);

    const auto appliedToIt = resultMap.find(QStringLiteral("appliedTo"));
    if (appliedToIt == resultMap.end()) {
        QNWARNING(
            "note_editor",
            "Can't figure out whether font family was applied to body style or "
                << "to selection, assuming the latter option");

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

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of font height "
                       "update from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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

    page()->toHtml(NoteEditorCallbackFunctor<QString>(
        this, &NoteEditorPrivate::onPageHtmlReceived));

    if (Q_UNLIKELY(extraData.empty())) {
        QNWARNING(
            "note_editor",
            "No font height in extra data in JavaScript "
                << "callback after setting font height");
        setModified();
        pushInsertHtmlUndoCommand();
        return;
    }

    const int height = extraData[0].second.toInt();
    Q_EMIT textFontSizeChanged(height);

    const auto appliedToIt = resultMap.find(QStringLiteral("appliedTo"));
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

    if (m_pNote->restrictions()) {
        const auto & noteRestrictions = *m_pNote->restrictions();
        if (noteRestrictions.noUpdateContent() &&
            *noteRestrictions.noUpdateContent())
        {
            QNTRACE(
                "note_editor",
                "Note has noUpdateContent restriction set to true");
            return true;
        }
    }

    if (!m_pNotebook) {
        QNTRACE("note_editor", "No notebook is set to the editor");
        return true;
    }

    if (!m_pNotebook->restrictions()) {
        QNTRACE("note_editor", "Notebook has no restrictions");
        return false;
    }

    const auto & restrictions = *m_pNotebook->restrictions();
    if (restrictions.noUpdateNotes() && *restrictions.noUpdateNotes()) {
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
    const QString javascript =                                                 \
        QString::fromUtf8("managedPageAction(\"%1\", null)")                   \
            .arg(escapedCommand);                                              \
    QNDEBUG("note_editor", "JS command: " << javascript)

#define COMMAND_WITH_ARGS_TO_JS(command, args)                                 \
    QString escapedCommand = command;                                          \
    escapeStringForJavaScript(escapedCommand);                                 \
    QString escapedArgs = args;                                                \
    escapeStringForJavaScript(escapedArgs);                                    \
    const QString javascript =                                                 \
        QString::fromUtf8("managedPageAction('%1', '%2')")                     \
            .arg(escapedCommand, escapedArgs);                                 \
    QNDEBUG("note_editor", "JS command: " << javascript)

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
    local_storage::ILocalStoragePtr localStorage, SpellChecker & spellChecker,
    const Account & account, QThread * backgroundJobsThread,
    enml::IDecryptedTextCachePtr decryptedTextCache)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::initialize");

    auto & noteEditorLocalStorageBroker =
        NoteEditorLocalStorageBroker::instance();

    noteEditorLocalStorageBroker.setLocalStorage(std::move(localStorage));

    m_pSpellChecker = &spellChecker;

    if (backgroundJobsThread) {
        m_pFileIOProcessorAsync->moveToThread(backgroundJobsThread);
    }

    if (!decryptedTextCache) {
        decryptedTextCache = enml::createDecryptedTextCache(m_encryptor);
    }
    m_decryptedTextCache = std::move(decryptedTextCache);

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
        m_pAccount = std::make_unique<Account>(account);
    }
    else {
        *m_pAccount = account;
    }

    init();
}

void NoteEditorPrivate::setUndoStack(QUndoStack * pUndoStack)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::setUndoStack");
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

    m_htmlForPrinting.resize(0);

    auto * pConversionTimer = new QTimer(this);
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
    const auto status = eventLoop.exitStatus();

    pConversionTimer->deleteLater();
    pConversionTimer = nullptr;

    if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't print note: failed to get the note editor page's "
                       "HTML in time"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    const auto res = m_enmlConverter->convertHtmlToDoc(
        m_htmlForPrinting, doc, m_skipRulesForHtmlToEnmlConversion);
    if (!res.isValid()) {
        ErrorString errorDescription{QT_TR_NOOP("Can't print note")};
        const auto & error = res.error();
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

    const QFileInfo pdfFileInfo(filePath);
    if (pdfFileInfo.exists() && !pdfFileInfo.isWritable()) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note to pdf: the output pdf file already "
                       "exists and it is not writable"));
        errorDescription.details() = filePath;
        QNDEBUG("note_editor", errorDescription);
        return false;
    }

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
        auto * pSaveNoteTimer = new QTimer(this);
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
        const auto status = eventLoop.exitStatus();

        if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't export note to enex: failed to save "
                           "the edited note in time"));
            QNWARNING("note_editor", errorDescription);
            return false;
        }

        if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't export note to enex: failed to save "
                           "the edited note"));
            QNWARNING("note_editor", errorDescription);
            return false;
        }

        QNDEBUG("note_editor", "Successfully saved the edited note");
    }

    QList<qevercloud::Note> notes;
    notes << *m_pNote;

    QStringList tagLocalIds;
    QHash<QString, QString> tagNamesByTagLocalId;

    for (auto it = tagNames.constBegin(), end = tagNames.constEnd(); it != end;
         ++it)
    {
        const QString fakeTagLocalId = UidGenerator::Generate();
        tagLocalIds.push_back(fakeTagLocalId);
        tagNamesByTagLocalId[fakeTagLocalId] = *it;
    }

    notes[0].setTagLocalIds(tagLocalIds);

    const enml::IConverter::EnexExportTags exportTagsOption =
        (tagNames.isEmpty() ? enml::IConverter::EnexExportTags::No
                            : enml::IConverter::EnexExportTags::Yes);

    const auto res = m_enmlConverter->exportNotesToEnex(
        notes, tagNamesByTagLocalId, exportTagsOption);
    if (!res.isValid()) {
        errorDescription = res.error();
        return false;
    }

    enex = res.get();
    return true;
}

QString NoteEditorPrivate::currentNoteLocalId() const
{
    return m_noteLocalId;
}

void NoteEditorPrivate::setCurrentNoteLocalId(const QString & noteLocalId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setCurrentNoteLocalId: note local id = "
            << noteLocalId);

    if (m_noteLocalId == noteLocalId) {
        QNDEBUG("note_editor", "Already have this note local id set");
        return;
    }

    m_pNote.reset(nullptr);
    m_pNotebook.reset(nullptr);

    clearCurrentNoteInfo();

    m_noteLocalId = noteLocalId;
    clearEditorContent(
        m_noteLocalId.isEmpty() ? BlankPageKind::Initial
                                : BlankPageKind::NoteLoading);

    if (!m_noteLocalId.isEmpty()) {
        QNTRACE(
            "note_editor",
            "Emitting the request to find note and notebook "
                << "for note local id " << m_noteLocalId);
        Q_EMIT findNoteAndNotebook(m_noteLocalId);
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
            "Already pending the conversion of " << "note editor page to HTML");
        return;
    }

    m_pendingConversionToNote = true;

    ErrorString error;
    if (!htmlToNoteContent(error)) {
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
        Q_EMIT failedToSaveNoteToLocalStorage(errorDescription, m_noteLocalId);
        return;
    }

    if (Q_UNLIKELY(isInkNote(*m_pNote))) {
        QNDEBUG(
            "note_editor",
            "Ink notes are read-only so won't save it to "
                << "the local storage, will just pretend it was saved");
        Q_EMIT noteSavedToLocalStorage(m_noteLocalId);
        return;
    }

    if (m_pendingNoteSavingInLocalStorage) {
        QNDEBUG("note_editor", "Note is already being saved to local storage");
        m_shouldRepeatSavingNoteInLocalStorage = true;
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
        "Emitting the request to save the note in the local storage");

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

    if (!m_pNote->title() && noteTitle.isEmpty()) {
        QNDEBUG("note_editor", "Note title is still empty, nothing to do");
        return;
    }

    if (m_pNote->title() && (*m_pNote->title() == noteTitle)) {
        QNDEBUG("note_editor", "Note title hasn't changed, nothing to do");
        return;
    }

    m_pNote->setTitle(noteTitle);

    if (m_pNote->attributes()) {
        m_pNote->mutableAttributes()->setNoteTitleQuality(std::nullopt);
    }

    setModified();
}

void NoteEditorPrivate::setTagIds(
    const QStringList & tagLocalIds, const QStringList & tagGuids)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::setTagIds: tag local ids: "
            << tagLocalIds.join(QStringLiteral(", "))
            << "; tag guids: " << tagGuids.join(QStringLiteral(", ")));

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't set tags to the note: no note "
                       "is set to the editor"));
        QNWARNING(
            "note_editor",
            error << ", tag local ids: "
                  << tagLocalIds.join(QStringLiteral(", "))
                  << "; tag guids: " << tagGuids.join(QStringLiteral(", ")));
        Q_EMIT notifyError(error);
        return;
    }

    const QStringList previousTagLocalIds = m_pNote->tagLocalIds();

    const QStringList previousTagGuids =
        (m_pNote->tagGuids() ? *m_pNote->tagGuids() : QStringList());

    if (!tagLocalIds.isEmpty() && !tagGuids.isEmpty()) {
        if ((tagLocalIds == previousTagLocalIds) &&
            (tagGuids == previousTagGuids))
        {
            QNDEBUG(
                "note_editor",
                "The list of tag ids hasn't changed, nothing to do");
            return;
        }

        m_pNote->setTagLocalIds(tagLocalIds);
        m_pNote->setTagGuids(tagGuids);
        setModified();

        return;
    }

    if (!tagLocalIds.isEmpty()) {
        if (tagLocalIds == previousTagLocalIds) {
            QNDEBUG(
                "note_editor",
                "The list of tag local ids hasn't changed, nothing to do");
            return;
        }

        m_pNote->setTagLocalIds(tagLocalIds);
        m_pNote->setTagGuids(std::nullopt);
        setModified();

        return;
    }

    if (!tagGuids.isEmpty()) {
        if (tagGuids == previousTagGuids) {
            QNDEBUG(
                "note_editor",
                "The list of tag guids hasn't changed, nothing to do");
            return;
        }

        m_pNote->setTagGuids(tagGuids);
        m_pNote->setTagLocalIds(QStringList{});
        setModified();

        return;
    }

    if (previousTagLocalIds.isEmpty() && previousTagGuids.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "Tag local ids and/or guids were empty and are "
                << "still empty, nothing to do");
        return;
    }

    m_pNote->setTagLocalIds(QStringList{});
    m_pNote->setTagGuids(std::nullopt);
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

void NoteEditorPrivate::addResourceToNote(const qevercloud::Resource & resource)
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

    if (resource.data() && resource.data()->bodyHash() &&
        resource.recognition() && resource.recognition()->body())
    {
        const ResourceRecognitionIndices recoIndices(
            *resource.recognition()->body());

        if (!recoIndices.isNull() && recoIndices.isValid()) {
            m_recognitionIndicesByResourceHash[*resource.data()->bodyHash()] =
                recoIndices;

            QNDEBUG(
                "note_editor",
                "Set recognition indices for new resource: " << recoIndices);
        }
    }

    if (!m_pNote->resources()) {
        m_pNote->setResources(QList<qevercloud::Resource>() << resource);
    }
    else {
        m_pNote->mutableResources()->push_back(resource);
    }

    setModified();
}

void NoteEditorPrivate::removeResourceFromNote(
    const qevercloud::Resource & resource)
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

    if (m_pNote->resources()) {
        auto it = std::find_if(
            m_pNote->mutableResources()->begin(),
            m_pNote->mutableResources()->end(),
            [&resource](const qevercloud::Resource & r) {
                return resource.localId() == r.localId();
            });

        if (it != m_pNote->mutableResources()->end()) {
            m_pNote->mutableResources()->erase(it);
        }
    }

    setModified();

    if (resource.data() && resource.data()->bodyHash()) {
        const auto it = m_recognitionIndicesByResourceHash.find(
            *resource.data()->bodyHash());

        if (it != m_recognitionIndicesByResourceHash.end()) {
            Q_UNUSED(m_recognitionIndicesByResourceHash.erase(it));
            highlightRecognizedImageAreas(
                m_lastSearchHighlightedText,
                m_lastSearchHighlightedTextCaseSensitivity);
        }

        const auto imageIt = m_genericResourceImageFilePathsByResourceHash.find(
            *resource.data()->bodyHash());

        if (imageIt != m_genericResourceImageFilePathsByResourceHash.end()) {
            Q_UNUSED(
                m_genericResourceImageFilePathsByResourceHash.erase(imageIt))
        }
    }
}

void NoteEditorPrivate::replaceResourceInNote(
    const qevercloud::Resource & resource)
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

    if (Q_UNLIKELY(!m_pNote->resources() || m_pNote->resources()->isEmpty()))
    { // NOLINT
        ErrorString error(
            QT_TR_NOOP("Can't replace the resource within note: "
                       "note has no resources"));
        QNWARNING(
            "note_editor", error << ", replacement resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    const auto resources = *m_pNote->resources();
    const auto resourceIt = std::find_if(
        resources.constBegin(), resources.constEnd(),
        [resourceLocalId = resource.localId()](const qevercloud::Resource & r) {
            return r.localId() == resourceLocalId;
        });
    if (Q_UNLIKELY(resourceIt == resources.constEnd())) {
        ErrorString error(
            QT_TR_NOOP("Can't replace the resource within note: "
                       "can't find the resource to be replaced"));
        QNWARNING(
            "note_editor", error << ", replacement resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    const auto & targetResource = *resourceIt;
    QByteArray previousResourceHash;
    if (targetResource.data()->bodyHash()) {
        previousResourceHash = *targetResource.data()->bodyHash();
    }

    updateResource(targetResource.localId(), previousResourceHash, resource);
}

void NoteEditorPrivate::setNoteResources(
    const QList<qevercloud::Resource> & resources)
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
    setFocus();
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
        m_pNote->localId() + QStringLiteral(".html");
}

void NoteEditorPrivate::setRenameResourceDelegateSubscriptions( // NOLINT
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
    const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::removeSymlinksToImageResourceFile: "
            << "resource local id = " << resourceLocalId);

    if (Q_UNLIKELY(!m_pNote)) {
        QNDEBUG(
            "note_editor",
            "Can't remove symlinks to resource image file: "
                << "no note is set to the editor");
        return;
    }

    const QString fileStorageDirPath =
        ResourceDataInTemporaryFileStorageManager::
            imageResourceFileStorageFolderPath() +
        QStringLiteral("/") + m_pNote->localId();

    const QString fileStoragePathPrefix =
        fileStorageDirPath + QStringLiteral("/") + resourceLocalId;

    QDir dir(fileStorageDirPath);
    QNTRACE(
        "note_editor",
        "Resource file storage dir "
            << (dir.exists() ? "exists" : "doesn't exist"));

    const QFileInfoList entryList =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    QNTRACE(
        "note_editor",
        "Found " << entryList.size() << " files in the image resources folder: "
                 << QDir::toNativeSeparators(fileStorageDirPath));

    for (const auto & entry: std::as_const(entryList)) {
        if (!entry.isSymLink()) {
            continue;
        }

        const auto entryFilePath = entry.absoluteFilePath();
        QNTRACE(
            "note_editor",
            "See if we need to remove the symlink to " << "resource image file "
                                                       << entryFilePath);

        if (!entryFilePath.startsWith(fileStoragePathPrefix)) {
            continue;
        }

        Q_UNUSED(removeFile(entryFilePath))
    }
}

QString NoteEditorPrivate::createSymlinkToImageResourceFile(
    const QString & fileStoragePath, const QString & localId,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate"
            << "::createSymlinkToImageResourceFile: file storage path = "
            << fileStoragePath << ", local id = " << localId);

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

    removeSymlinksToImageResourceFile(localId);

    QFile imageResourceFile(fileStoragePath);
    if (Q_UNLIKELY(!imageResourceFile.link(linkFilePath))) {
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
            "Null pointer to mime data from drop event " << "was detected");
        return;
    }

    const auto urls = pMimeData->urls();
    for (const auto & url: std::as_const(urls)) {
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

qevercloud::Resource NoteEditorPrivate::attachResourceToNote(
    const QByteArray & data, const QByteArray & dataHash,
    const QMimeType & mimeType, const QString & filename,
    const QString & sourceUrl)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::attachResourceToNote: hash = "
            << dataHash.toHex() << ", mime type = " << mimeType.name()
            << ", filename = " << filename << ", source url = " << sourceUrl);

    qevercloud::Resource resource;
    QString resourceLocalId = resource.localId();

    // Force the resource to have empty local id for now
    resource.setLocalId(QString());

    if (Q_UNLIKELY(!m_pNote)) {
        QNINFO(
            "note_editor",
            "Can't attach resource to note editor: no note in the note editor");
        return resource;
    }

    // Now can return the local id back to the resource
    resource.setLocalId(resourceLocalId);

    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(data);

    if (!dataHash.isEmpty()) {
        resource.mutableData()->setBodyHash(dataHash);
    }

    resource.mutableData()->setSize(data.size());
    resource.setMime(mimeType.name());
    resource.setLocallyModified(true);

    if (!filename.isEmpty()) {
        resource.setAttributes(qevercloud::ResourceAttributes{});
        resource.mutableAttributes()->setFileName(filename);
    }

    if (!sourceUrl.isEmpty()) {
        if (!resource.attributes()) {
            resource.setAttributes(qevercloud::ResourceAttributes{});
        }
        resource.mutableAttributes()->setSourceURL(sourceUrl);
    }

    resource.setNoteLocalId(m_pNote->localId());
    if (m_pNote->guid()) {
        resource.setNoteGuid(*m_pNote->guid());
    }

    if (!m_pNote->resources()) {
        m_pNote->setResources(QList<qevercloud::Resource>() << resource);
    }
    else {
        m_pNote->mutableResources()->push_back(resource);
    }

    // NOTE: will not emit convertedToNote signal because the current state
    // of the note is likely not the one that listeners of this signal want to
    // see.
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

    const QString javascript = QString::fromUtf8("flipEnToDoCheckboxState(%1);")
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

    if (Q_UNLIKELY(!m_pNote->resources() || m_pNote->resources()->isEmpty()))
    { // NOLINT
        QNTRACE("note_editor", "Note has no resources - returning zero");
        return qint64(0);
    }

    qint64 size = 0;

    const auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    for (const auto & resource: std::as_const(resources)) {
        QNTRACE(
            "note_editor",
            "Computing size contributions for resource: " << resource);

        if (resource.data() && resource.data()->size()) {
            size += *resource.data()->size();
        }

        if (resource.alternateData() && resource.alternateData()->size()) {
            size += *resource.alternateData()->size();
        }

        if (resource.recognition() && resource.recognition()->size()) {
            size += *resource.recognition()->size();
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

    if (Q_UNLIKELY(!m_pNote->content())) {
        return qint64(0);
    }

    return static_cast<qint64>(m_pNote->content()->size());
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
        "NoteEditorPrivate::onSpellCheckAddWordToUserDictionaryAction");

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
        "NoteEditorPrivate::onSpellCheckCorrectionActionDone: " << data);

    Q_UNUSED(extraData)

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of spelling "
                       "correction from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate::onSpellCheckCorrectionUndoRedoFinished");

    Q_UNUSED(extraData)

    const auto resultMap = data.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of spelling "
                       "correction undo/redo from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
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
        "NoteEditorPrivate" << "::onSpellCheckerDynamicHelperUpdate: "
                            << words.join(QStringLiteral(";")));

    if (!m_spellCheckerEnabled) {
        QNTRACE("note_editor", "No spell checking is enabled, nothing to do");
        return;
    }

    if (Q_UNLIKELY(!m_pSpellChecker)) {
        QNDEBUG("note_editor", "Spell checker is null, won't do anything");
        return;
    }

    for (auto word: std::as_const(words)) {
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
    page->triggerAction(QWebEnginePage::Copy);
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
                html, *this, m_enmlTagsConverter,
                m_pResourceDataInTemporaryFileStorageManager,
                m_resourceFileStoragePathsByResourceLocalId, m_resourceInfo,
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
            "Unable to retrieve the mime data from " << "the clipboard");
    }

    QString textToPaste = pClipboard->text();
    QNTRACE("note_editor", "Text to paste: " << textToPaste);

    if (textToPaste.isEmpty()) {
        QNDEBUG("note_editor", "The text to paste is empty");
        return;
    }

    const bool shouldBeHyperlink =
        textToPaste.startsWith(QStringLiteral("http://")) ||
        textToPaste.startsWith(QStringLiteral("https://")) ||
        textToPaste.startsWith(QStringLiteral("mailto:")) ||
        textToPaste.startsWith(QStringLiteral("ftp://"));

    const bool shouldBeAttachment =
        textToPaste.startsWith(QStringLiteral("file://"));

    const bool shouldBeInAppLink =
        textToPaste.startsWith(QStringLiteral("evernote://"));

    if (!shouldBeHyperlink && !shouldBeAttachment && !shouldBeInAppLink) {
        QNTRACE(
            "note_editor",
            "The pasted text doesn't appear to be a url of hyperlink or "
                << "attachment");
        execJavascriptCommand(QStringLiteral("insertText"), textToPaste);
        return;
    }

    QUrl url(textToPaste);
    if (shouldBeAttachment) {
        if (!url.isValid()) {
            QNTRACE(
                "note_editor",
                "The pasted text seemed like file url but the url isn't valid "
                    << "after all, fallback to simple paste");
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
        "Was able to create the url from pasted text, inserting a hyperlink");

    if (shouldBeInAppLink) {
        QString userId, shardId, noteGuid;
        ErrorString errorDescription;
        if (!parseInAppLink(
                textToPaste, userId, shardId, noteGuid, errorDescription))
        {
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

    const quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
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

    const QString textToPaste = pClipboard->text();
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
    page->triggerAction(QWebEnginePage::SelectAll);
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
    const QFont chosenFont = QFontDialog::getFont(&fontWasChosen, m_font, this);
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
    page->triggerAction(QWebEnginePage::Copy);

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

    const QString javascript =
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

    const QString javascript =
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

    const QString javascript =
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

    const QString urlString = QStringLiteral("evernote:///view/") + userId +
        QStringLiteral("/") + shardId + QStringLiteral("/") + noteGuid +
        QStringLiteral("/") + noteGuid;

    const quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
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
    const QString fontFamily = font.family();

    const QString javascript =
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
        m_pPalette = std::make_unique<QPalette>(pal);
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
        m_pDefaultFont = std::make_unique<QFont>(font);
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
    changeFontSize(/* increase = */ false);
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

    const int pageWidth = geometry().width();
    if (widthInPixels > 2 * pageWidth) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, width is too large "
                       "(more than twice the page width)"));
        error.details() = QString::number(widthInPixels);
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (widthInPixels <= 0) {
        ErrorString error(QT_TR_NOOP("Can't insert table, bad width"));
        error.details() = QString::number(widthInPixels);
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    const int singleColumnWidth = widthInPixels / columns;
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

    const QString htmlTable = composeHtmlTable(
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

    if (relativeWidth > 100.0 + 1.0e-9) {
        ErrorString error(
            QT_TR_NOOP("Can't insert table, relative width is too large"));
        error.details() = QString::number(relativeWidth);
        error.details() += QStringLiteral("%");
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    const double singleColumnWidth = relativeWidth / columns;

    const QString htmlTable = composeHtmlTable(
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

    const QVariant lastAttachmentAddLocation =
        appSettings.value(NOTE_EDITOR_LAST_ATTACHMENT_ADD_LOCATION_KEY);

    if (!lastAttachmentAddLocation.isNull() &&
        lastAttachmentAddLocation.isValid())
    {
        QNTRACE(
            "note_editor",
            "Found last attachment add location: "
                << lastAttachmentAddLocation);

        const QFileInfo lastAttachmentAddDirInfo(
            lastAttachmentAddLocation.toString());

        if (!lastAttachmentAddDirInfo.exists()) {
            QNTRACE(
                "note_editor",
                "Cached last attachment add directory does " << "not exist");
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

    const QString absoluteFilePath = QFileDialog::getOpenFileName(
        this, tr("Add attachment") + QStringLiteral("..."),
        addAttachmentInitialFolderPath);

    if (absoluteFilePath.isEmpty()) {
        QNTRACE("note_editor", "User cancelled adding the attachment");
        return;
    }

    QNTRACE(
        "note_editor",
        "Absolute file path of chosen attachment: " << absoluteFilePath);

    const QFileInfo fileInfo(absoluteFilePath);
    const QString absoluteDirPath = fileInfo.absoluteDir().absolutePath();
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

    const auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const int resourceIndex = resourceIndexByHash(resources, resourceHash);
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

    const auto & resource = std::as_const(resources).at(resourceIndex);

    if (Q_UNLIKELY(
            !((resource.data() && resource.data()->body()) ||
              (resource.alternateData() && resource.alternateData()->body()))))
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

    if (Q_UNLIKELY(!resource.mime())) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment as it has no mime type"));
        QNWARNING(
            "note_editor",
            error << ", resource hash = " << resourceHash.toHex());
        Q_EMIT notifyError(error);
        return;
    }

    const QByteArray & data = (resource.data() && resource.data()->body())
        ? *resource.data()->body()
        : resource.alternateData().value().body().value();

    const QString & mimeType = *resource.mime();

    auto * pClipboard = QApplication::clipboard();
    if (Q_UNLIKELY(!pClipboard)) {
        ErrorString error(
            QT_TR_NOOP("Can't copy the attachment: can't get access "
                       "to clipboard"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto pMimeData = std::make_unique<QMimeData>();
    pMimeData->setData(mimeType, data);
    pClipboard->setMimeData(pMimeData.release());
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
    const auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    // FIXME: rewrite this with std::find_if
    for (const auto & resource: std::as_const(resources)) {
        if (resource.data() && resource.data()->bodyHash() &&
            (*resource.data()->bodyHash() == resourceHash))
        {
            Q_UNUSED(
                m_resourceInfo.removeResourceInfo(*resource.data()->bodyHash()))

            auto & noteEditorLocalStorageBroker =
                NoteEditorLocalStorageBroker::instance();

            auto localStorage = noteEditorLocalStorageBroker.localStorage();

            if (Q_UNLIKELY(!localStorage)) {
                ErrorString error(
                    QT_TR_NOOP("Can't remove the attachment: note "
                               "editor is not initialized properly"));
                QNWARNING("note_editor", error);
                Q_EMIT notifyError(error);
                return;
            }

            auto * delegate =
                new RemoveResourceDelegate(resource, *this, localStorage);

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
        "NoteEditorPrivate::renameAttachment: " << "resource hash = "
                                                << resourceHash.toHex());

    ErrorString errorPrefix(QT_TR_NOOP("Can't rename the attachment"));
    CHECK_NOTE_EDITABLE(errorPrefix)

    if (Q_UNLIKELY(!m_pNote)) {
        ErrorString error = errorPrefix;
        error.appendBase(QT_TR_NOOP("No note is set to the editor"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const auto resourceIt = std::find_if(
        resources.begin(), resources.end(),
        [&resourceHash](const qevercloud::Resource & resource) {
            return resource.data() && resource.data()->bodyHash() &&
                *resource.data()->bodyHash() == resourceHash;
        });
    if (Q_UNLIKELY(resourceIt == resources.end())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("Can't find the corresponding resource in the note"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto & resource = *resourceIt;
    if (Q_UNLIKELY(!(resource.data() && resource.data()->body()))) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The resource doesn't have the data body set"));
        QNWARNING("note_editor", error);
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

    auto resources =
        (m_pNote->resources() ? *m_pNote->resources()
                              : QList<qevercloud::Resource>());

    const auto resourceIt = std::find_if(
        resources.begin(), resources.end(),
        [&resourceHash](const qevercloud::Resource & resource) {
            return resource.data() && resource.data()->bodyHash() &&
                *resource.data()->bodyHash() == resourceHash;
        });

    if (Q_UNLIKELY(resourceIt == resources.end())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("Can't find the corresponding attachment "
                       "within the note"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto & resource = *resourceIt;

    if (Q_UNLIKELY(!resource.mime())) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The corresponding attachment's mime type is not set"));
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!resource.mime()->startsWith(QStringLiteral("image/")))) {
        ErrorString error = errorPrefix;
        error.appendBase(
            QT_TR_NOOP("The corresponding attachment's mime type "
                       "indicates it is not an image"));
        error.details() = *resource.mime();
        QNWARNING("note_editor", error << ", resource: " << resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!(resource.data() && resource.data()->body())) {
        QNDEBUG(
            "note_editor",
            "The resource to be rotated doesn't have data "
                << "body set, requesting it from NoteEditorLocalStorageBroker");

        const QString & resourceLocalId = resource.localId();

        m_rotationTypeByResourceLocalIdsPendingFindDataInLocalStorage
            [resourceLocalId] = rotationDirection;

        Q_EMIT findResourceData(resourceLocalId);
        return;
    }

    auto * delegate = new ImageResourceRotationDelegate(
        *resource.data()->bodyHash(), rotationDirection, *this, m_resourceInfo,
        *m_pResourceDataInTemporaryFileStorageManager,
        m_resourceFileStoragePathsByResourceLocalId);

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
        "NoteEditorPrivate::rotateImageAttachmentUnderCursor: rotation: "
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
        "NoteEditorPrivate::rotateImageAttachmentUnderCursorClockwise");

    rotateImageAttachmentUnderCursor(Rotation::Clockwise);
}

void NoteEditorPrivate::rotateImageAttachmentUnderCursorCounterclockwise()
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPrivate::rotateImageAttachmentUnderCursorCounterclockwise");

    rotateImageAttachmentUnderCursor(Rotation::Counterclockwise);
}

void NoteEditorPrivate::encryptSelectedText()
{
    QNDEBUG("note_editor", "NoteEditorPrivate::encryptSelectedText");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't encrypt the selected text"))
    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Can't encrypt the selected text"))

    auto * delegate = new EncryptSelectedTextDelegate(
        this, m_encryptor, m_decryptedTextCache, m_enmlTagsConverter);

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
        m_currentContextMenuExtraData.m_hint,
        m_currentContextMenuExtraData.m_id);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::decryptEncryptedText(
    QString encryptedText, QString cipherStr, QString hint,
    QString enCryptIndex)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::decryptEncryptedText");

    CHECK_NOTE_EDITABLE(QT_TR_NOOP("Can't decrypt the encrypted text"))
    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Can't decrypt the encrypted text"))

    const auto cipher = parseCipher(cipherStr);
    if (Q_UNLIKELY(!cipher)) {
        ErrorString error{
            QT_TR_NOOP("Cannot decrypt encrypted text: unknown cipher")};
        error.details() = cipherStr;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto * delegate = new DecryptEncryptedTextDelegate(
        enCryptIndex, encryptedText, *cipher, hint, this, m_encryptor,
        m_decryptedTextCache, m_enmlTagsConverter);

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
        m_currentContextMenuExtraData.m_hint,
        m_currentContextMenuExtraData.m_id);

    m_currentContextMenuExtraData.m_contentType.resize(0);
}

void NoteEditorPrivate::hideDecryptedText(
    QString encryptedText, QString decryptedText, QString cipherStr,
    QString hint, QString enDecryptedIndex)
{
    QNDEBUG("note_editor", "NoteEditorPrivate::hideDecryptedText");

    CHECK_DECRYPTED_TEXT_CACHE(QT_TR_NOOP("Can't hide the encrypted text"))

    const auto cipher = parseCipher(cipherStr);
    if (Q_UNLIKELY(!cipher)) {
        ErrorString error{
            QT_TR_NOOP("Cannot hide decrypted text: unknown cipher")};
        error.details() = cipherStr;
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    const auto originalDecryptedTextInfo =
        m_decryptedTextCache->findDecryptedTextInfo(encryptedText);
    if (originalDecryptedTextInfo &&
        (originalDecryptedTextInfo->first != decryptedText))
    {
        QNDEBUG(
            "note_editor",
            "The original decrypted text doesn't match the newer one, will "
                << "return-encrypt the decrypted text");

        const auto reEncryptedText =
            m_decryptedTextCache->updateDecryptedTextInfo(
                encryptedText, decryptedText);
        if (Q_UNLIKELY(!reEncryptedText)) {
            ErrorString error(
                QT_TR_NOOP("Can't hide the decrypted text: the decrypted text "
                           "was modified but it failed to get "
                           "return-encrypted"));
            QNWARNING("note_editor", error);
            Q_EMIT notifyError(error);
            return;
        }

        QNDEBUG(
            "note_editor",
            "Old encrypted text = " << encryptedText
                                    << ", new encrypted text = "
                                    << *reEncryptedText);
        encryptedText = *reEncryptedText;
    }

    const quint32 enCryptIndex = m_lastFreeEnCryptIdNumber++;

    QString html = m_enmlTagsConverter->convertEncryptedText(
        encryptedText, hint, *cipher, enCryptIndex);

    escapeStringForJavaScript(html);

    const QString javascript =
        QStringLiteral(
            "encryptDecryptManager.replaceDecryptedTextWithEncryptedText('") +
        enDecryptedIndex + QStringLiteral("', '") + html +
        QStringLiteral("');");

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

    const QString javascript =
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

    const auto resultMap = hyperlinkData.toMap();
    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of the attempt "
                       "to find the hyperlink data by id from JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        QNTRACE(
            "note_editor",
            "No hyperlink id under cursor was found, assuming we're adding "
                << "the new hyperlink to the selected text");

        GET_PAGE()

        const quint64 hyperlinkId = m_lastFreeHyperlinkIdNumber++;
        setupAddHyperlinkDelegate(hyperlinkId);
        return;
    }

    const auto dataIt = resultMap.constFind(QStringLiteral("data"));
    if (Q_UNLIKELY(dataIt == resultMap.constEnd())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the seemingly positive result of "
                       "the attempt to find the hyperlink data by id from "
                       "JavaScript"));
        QNWARNING("note_editor", error);
        Q_EMIT notifyError(error);
        return;
    }

    const QString hyperlinkDataStr = dataIt.value().toString();

    bool conversionResult = false;
    const quint64 hyperlinkId = hyperlinkDataStr.toULongLong(&conversionResult);
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

    const QStringList hyperlinkDataList = hyperlinkData.toStringList();
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
        filePath, *this, m_enmlTagsConverter,
        m_pResourceDataInTemporaryFileStorageManager, m_pFileIOProcessorAsync,
        m_pGenericResourceImageManager,
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

    const auto image = qvariant_cast<QImage>(mimeData.imageData());
    QByteArray data;
    QBuffer imageDataBuffer(&data);
    imageDataBuffer.open(QIODevice::WriteOnly);
    image.save(&imageDataBuffer, "PNG");

    const QString mimeType = QStringLiteral("image/png");

    auto * delegate = new AddResourceDelegate(
        data, mimeType, *this, m_enmlTagsConverter,
        m_pResourceDataInTemporaryFileStorageManager, m_pFileIOProcessorAsync,
        m_pGenericResourceImageManager,
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
    str = enml::utils::htmlEscapeString(str);
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
