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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_P_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_P_H

#include "NoteEditorPage.h"
#include "ResourceInfo.h"

#include <quentier/enml/DecryptedTextManager.h>
#include <quentier/enml/ENMLConverter.h>
#include <quentier/note_editor/INoteEditorBackend.h>
#include <quentier/note_editor/NoteEditor.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/ResourceRecognitionIndices.h>
#include <quentier/utility/EncryptionManager.h>
#include <quentier/utility/StringUtils.h>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QMimeData>
#include <QMimeType>
#include <QObject>
#include <QPointer>
#include <QProgressDialog>
#include <QUndoStack>

#ifdef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebEngineView>
typedef QWebEngineView WebView;
typedef QWebEnginePage WebPage;
#else
#include "NoteEditorPluginFactory.h"

#include <QWebView>
typedef QWebView WebView;
typedef QWebPage WebPage;
#endif

#include <memory>
#include <utility>
#include <vector>

QT_FORWARD_DECLARE_CLASS(QByteArray)
QT_FORWARD_DECLARE_CLASS(QImage)
QT_FORWARD_DECLARE_CLASS(QMimeType)
QT_FORWARD_DECLARE_CLASS(QThread)

#ifdef QUENTIER_USE_QT_WEB_ENGINE
QT_FORWARD_DECLARE_CLASS(QWebChannel)
QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(WebSocketClientWrapper)
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(FileIOProcessorAsync)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(ResourceDataInTemporaryFileStorageManager)
QT_FORWARD_DECLARE_CLASS(ResourceInfoJavaScriptHandler)

QT_FORWARD_DECLARE_CLASS(ActionsWatcher)
QT_FORWARD_DECLARE_CLASS(ContextMenuEventJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(GenericResourceImageManager)
QT_FORWARD_DECLARE_CLASS(PageMutationHandler)
QT_FORWARD_DECLARE_CLASS(RenameResourceDelegate)
QT_FORWARD_DECLARE_CLASS(ResizableImageJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(SpellChecker)
QT_FORWARD_DECLARE_CLASS(SpellCheckerDynamicHelper)
QT_FORWARD_DECLARE_CLASS(TableResizeJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(TextCursorPositionJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(ToDoCheckboxOnClickHandler)
QT_FORWARD_DECLARE_CLASS(ToDoCheckboxAutomaticInsertionHandler)

#ifdef QUENTIER_USE_QT_WEB_ENGINE
QT_FORWARD_DECLARE_CLASS(EnCryptElementOnClickHandler)
QT_FORWARD_DECLARE_CLASS(GenericResourceOpenAndSaveButtonsOnClickHandler)
QT_FORWARD_DECLARE_CLASS(GenericResourceImageJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(HyperlinkClickJavaScriptHandler)
QT_FORWARD_DECLARE_CLASS(WebSocketWaiter)
#endif

class Q_DECL_HIDDEN NoteEditorPrivate final :
    public WebView,
    public INoteEditorBackend
{
    Q_OBJECT
public:
    explicit NoteEditorPrivate(NoteEditor & noteEditor);
    virtual ~NoteEditorPrivate();

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QVariant execJavascriptCommandWithResult(const QString & command);

    QVariant execJavascriptCommandWithResult(
        const QString & command, const QString & args);
#endif

    void execJavascriptCommand(const QString & command);
    void execJavascriptCommand(const QString & command, const QString & args);

Q_SIGNALS:
    void contentChanged();
    void noteAndNotebookFoundInLocalStorage(Note note, Notebook notebook);
    void noteNotFound(QString noteLocalUid);
    void noteDeleted(QString noteLocalUid);

    void noteModified();
    void notifyError(ErrorString error);

    void inAppNoteLinkClicked(
        QString userId, QString shardId, QString noteGuid);

    void inAppNoteLinkPasteRequested(
        QString url, QString userId, QString shardId, QString noteGuid);

    void convertedToNote(Note note);
    void cantConvertToNote(ErrorString error);

    void noteEditorHtmlUpdated(QString html);

    void currentNoteChanged(Note note);

    void spellCheckerNotReady();
    void spellCheckerReady();

    void noteLoaded();

    // Signals to notify anyone interested of the formatting at the current
    // cursor position
    void textBoldState(bool state);
    void textItalicState(bool state);
    void textUnderlineState(bool state);
    void textStrikethroughState(bool state);
    void textAlignLeftState(bool state);
    void textAlignCenterState(bool state);
    void textAlignRightState(bool state);
    void textAlignFullState(bool state);
    void textInsideOrderedListState(bool state);
    void textInsideUnorderedListState(bool state);
    void textInsideTableState(bool state);

    void textFontFamilyChanged(QString fontFamily);
    void textFontSizeChanged(int fontSize);

    void insertTableDialogRequested();

public:
    // Force the conversion from ENML to HTML
    void updateFromNote();

    // Resets the note's HTML to the given one
    void setNoteHtml(const QString & html);

    const Account * accountPtr() const;

    const Resource attachResourceToNote(
        const QByteArray & data, const QByteArray & dataHash,
        const QMimeType & mimeType, const QString & filename,
        const QString & sourceUrl = {});

    void addResourceToNote(const Resource & resource);
    void removeResourceFromNote(const Resource & resource);
    void replaceResourceInNote(const Resource & resource);
    void setNoteResources(const QList<Resource> & resources);

    QImage buildGenericResourceImage(const Resource & resource);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    void saveGenericResourceImage(
        const Resource & resource, const QImage & image);
    void provideSrcForGenericResourceImages();
    void setupGenericResourceOnClickHandler();
#endif

    void updateResource(
        const QString & resourceLocalUid,
        const QByteArray & previousResourceHash, Resource updatedResource);

    Note * notePtr()
    {
        return m_pNote.get();
    }
    void setModified();

    bool isPageEditable() const
    {
        return m_isPageEditable;
    }

    QString noteEditorPagePath() const;

    const QString & genericResourceImageFileStoragePath() const
    {
        return m_genericResourceImageFileStoragePath;
    }

    void setRenameResourceDelegateSubscriptions(
        RenameResourceDelegate & delegate);

    void removeSymlinksToImageResourceFile(const QString & resourceLocalUid);

    QString createSymlinkToImageResourceFile(
        const QString & fileStoragePath, const QString & localUid,
        ErrorString & errorDescription);

    void onDropEvent(QDropEvent * pEvent);
    void dropFile(const QString & filepath);

    quint64 GetFreeEncryptedTextId()
    {
        return m_lastFreeEnCryptIdNumber++;
    }
    quint64 GetFreeDecryptedTextId()
    {
        return m_lastFreeEnDecryptedIdNumber++;
    }

    void refreshMisSpelledWordsList();
    void applySpellCheck(const bool applyToSelection = false);
    void removeSpellCheck();
    void enableDynamicSpellCheck();
    void disableDynamicSpellCheck();

    virtual bool isNoteLoaded() const override;
    virtual qint64 idleTime() const override;

    virtual QString selectedText() const override;
    virtual bool hasSelection() const override;

    bool searchHighlightEnabled() const;

    void setSearchHighlight(
        const QString & textToFind, const bool matchCase,
        const bool force = false) const;

    void highlightRecognizedImageAreas(
        const QString & textToFind, const bool matchCase) const;

    virtual bool spellCheckEnabled() const override;

    virtual bool isModified() const override;
    virtual bool isEditorPageModified() const override;

    qint64 noteResourcesSize() const;
    qint64 noteContentSize() const;
    qint64 noteSize() const;

    virtual QPalette defaultPalette() const override;

    virtual const QFont * defaultFont() const override;

public Q_SLOTS:
    // INoteEditorBackend interface
    virtual void initialize(
        LocalStorageManagerAsync & localStorageManager,
        SpellChecker & spellChecker, const Account & account,
        QThread * pBackgroundJobsThread) override;

    virtual QObject * object() override
    {
        return this;
    }
    virtual QWidget * widget() override
    {
        return this;
    }
    virtual void setAccount(const Account & account) override;
    virtual void setUndoStack(QUndoStack * pUndoStack) override;

    virtual void setInitialPageHtml(const QString & html) override;
    virtual void setNoteNotFoundPageHtml(const QString & html) override;
    virtual void setNoteDeletedPageHtml(const QString & html) override;
    virtual void setNoteLoadingPageHtml(const QString & html) override;

    virtual void undo() override;
    virtual void redo() override;
    virtual void cut() override;
    virtual void copy() override;
    virtual void paste() override;
    virtual void pasteUnformatted() override;
    virtual void selectAll() override;
    virtual void formatSelectionAsSourceCode() override;
    virtual void fontMenu() override;
    virtual void textBold() override;
    virtual void textItalic() override;
    virtual void textUnderline() override;
    virtual void textStrikethrough() override;
    virtual void textHighlight() override;
    virtual void alignLeft() override;
    virtual void alignCenter() override;
    virtual void alignRight() override;
    virtual void alignFull() override;

    virtual void findNext(
        const QString & text, const bool matchCase) const override;

    virtual void findPrevious(
        const QString & text, const bool matchCase) const override;

    virtual void replace(
        const QString & textToReplace, const QString & replacementText,
        const bool matchCase) override;

    virtual void replaceAll(
        const QString & textToReplace, const QString & replacementText,
        const bool matchCase) override;

    void onReplaceJavaScriptDone(const QVariant & data);

    virtual void insertToDoCheckbox() override;

    virtual void insertInAppNoteLink(
        const QString & userId, const QString & shardId,
        const QString & noteGuid, const QString & linkText) override;

    virtual void setSpellcheck(const bool enabled) override;
    virtual void setFont(const QFont & font) override;
    virtual void setFontHeight(const int height) override;
    virtual void setFontColor(const QColor & color) override;
    virtual void setBackgroundColor(const QColor & color) override;
    virtual void setDefaultPalette(const QPalette & pal) override;
    virtual void setDefaultFont(const QFont & font) override;
    virtual void insertHorizontalLine() override;
    virtual void increaseFontSize() override;
    virtual void decreaseFontSize() override;
    virtual void increaseIndentation() override;
    virtual void decreaseIndentation() override;
    virtual void insertBulletedList() override;
    virtual void insertNumberedList() override;
    virtual void insertTableDialog() override;

    virtual void insertFixedWidthTable(
        const int rows, const int columns, const int widthInPixels) override;

    virtual void insertRelativeWidthTable(
        const int rows, const int columns, const double relativeWidth) override;

    virtual void insertTableRow() override;
    virtual void insertTableColumn() override;
    virtual void removeTableRow() override;
    virtual void removeTableColumn() override;
    virtual void addAttachmentDialog() override;

    virtual void saveAttachmentDialog(const QByteArray & resourceHash) override;

    virtual void saveAttachmentUnderCursor() override;
    virtual void openAttachment(const QByteArray & resourceHash) override;
    virtual void openAttachmentUnderCursor() override;
    virtual void copyAttachment(const QByteArray & resourceHash) override;
    virtual void copyAttachmentUnderCursor() override;
    virtual void removeAttachment(const QByteArray & resourceHash) override;
    virtual void removeAttachmentUnderCursor() override;
    virtual void renameAttachment(const QByteArray & resourceHash) override;
    virtual void renameAttachmentUnderCursor() override;

    virtual void rotateImageAttachment(
        const QByteArray & resourceHash,
        const Rotation rotationDirection) override;

    virtual void rotateImageAttachmentUnderCursor(
        const Rotation rotationDirection) override;

    void rotateImageAttachmentUnderCursorClockwise();
    void rotateImageAttachmentUnderCursorCounterclockwise();

    virtual void encryptSelectedText() override;

    virtual void decryptEncryptedTextUnderCursor() override;

    virtual void decryptEncryptedText(
        QString encryptedText, QString cipher, QString keyLength, QString hint,
        QString enCryptIndex) override;

    virtual void hideDecryptedTextUnderCursor() override;

    virtual void hideDecryptedText(
        QString encryptedText, QString decryptedText, QString cipher,
        QString keyLength, QString hint, QString enDecryptedIndex) override;

    virtual void editHyperlinkDialog() override;
    virtual void copyHyperlink() override;
    virtual void removeHyperlink() override;

    virtual void onNoteLoadCancelled() override;

    virtual bool print(
        QPrinter & printer, ErrorString & errorDescription) override;

    virtual bool exportToPdf(
        const QString & absoluteFilePath,
        ErrorString & errorDescription) override;

    virtual bool exportToEnex(
        const QStringList & tagNames, QString & enex,
        ErrorString & errorDescription) override;

    virtual void setCurrentNoteLocalUid(const QString & noteLocalUid) override;

    virtual void clear() override;
    virtual void setFocusToEditor() override;
    virtual void convertToNote() override;
    virtual void saveNoteToLocalStorage() override;
    virtual void setNoteTitle(const QString & noteTitle) override;

    virtual void setTagIds(
        const QStringList & tagLocalUids,
        const QStringList & tagGuids) override;

    void undoPageAction();
    void redoPageAction();

    void flipEnToDoCheckboxState(const quint64 enToDoIdNumber);

    void updateLastInteractionTimestamp();

public:
    virtual QString currentNoteLocalUid() const override;

    // private signals:
Q_SIGNALS:
    // Signals for communicating with ResourceDataInTemporaryFileStorageManager

    /**
     * The signal delegating the sequence of actions required for opening the
     * resource data within the external editor to
     * ResourceDataInTemporaryFileStorageManager
     */
    void openResourceFile(QString resourceLocalUid);

    // Signals for communicating with FileIOProcessorAsync

    /**
     * The signal used for writing of note editor page's HTML to a file so that
     * it can be loaded as a URL within the note editor page
     */
    void writeNoteHtmlToFile(
        QString absoluteFilePath, QByteArray html, QUuid requestId,
        bool append);

    /**
     * The signal used to save the resource binary data to some file selected by
     * the user (i.e. this signal is used in the course of actions processing
     * the user initiated request to save some of note's resources to a file)
     */
    void saveResourceToFile(
        QString absoluteFilePath, QByteArray resourceData, QUuid requestId,
        bool append);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    /**
     * The signal used during the preparation for loading the note into the note
     * editor page: this signal initiates writing the specifically constructed
     * image - "generic resource image" - to a file so that it can be loaded
     * as an img tag's URL into the note editor page
     */
    void saveGenericResourceImageToFile(
        QString noteLocalUid, QString resourceLocalUid,
        QByteArray resourceImageData, QString resourceFileSuffix,
        QByteArray resourceActualHash, QString resourceDisplayName,
        QUuid requestId);
#endif // QUENTIER_USE_QT_WEB_ENGINE

    // Signals for communicating with NoteEditorLocalStorageBroker
    void findNoteAndNotebook(const QString & noteLocalUid);
    void saveNoteToLocalStorageRequest(const Note & note);
    void findResourceData(const QString & resourceLocalUid);
    void noteSavedToLocalStorage(QString noteLocalUid);

    void failedToSaveNoteToLocalStorage(
        ErrorString errorDescription, QString noteLocalUid);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    /**
     * The signal used during the asynchronous sequence of actions required for
     * printing the note to pdf
     */
    void htmlReadyForPrinting();
#endif

private Q_SLOTS:
    void onTableResized();

    void onFoundSelectedHyperlinkId(
        const QVariant & hyperlinkData,
        const QVector<std::pair<QString, QString>> & extraData);
    void onFoundHyperlinkToCopy(
        const QVariant & hyperlinkData,
        const QVector<std::pair<QString, QString>> & extraData);

    void onNoteLoadFinished(bool ok);
    void onContentChanged();

    void onResourceFileChanged(
        QString resourceLocalUid, QString fileStoragePath,
        QByteArray resourceData, QByteArray resourceDataHash);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    void onGenericResourceImageSaved(
        bool success, QByteArray resourceActualHash, QString filePath,
        ErrorString errorDescription, QUuid requestId);

    void onHyperlinkClicked(QString url);

    void onWebSocketReady();
#else
    void onHyperlinkClicked(QUrl url);
#endif

    void onToDoCheckboxClicked(quint64 enToDoCheckboxId);
    void onToDoCheckboxClickHandlerError(ErrorString error);

    void onToDoCheckboxInserted(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onToDoCheckboxAutomaticInsertion();

    void onToDoCheckboxAutomaticInsertionUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onJavaScriptLoaded();

    void onOpenResourceRequest(const QByteArray & resourceHash);
    void onSaveResourceRequest(const QByteArray & resourceHash);

    void contextMenuEvent(QContextMenuEvent * pEvent) override;

    void onContextMenuEventReply(
        QString contentType, QString selectedHtml,
        bool insideDecryptedTextFragment, QStringList extraData,
        quint64 sequenceNumber);

    void onTextCursorPositionChange();

    void onTextCursorBoldStateChanged(bool state);
    void onTextCursorItalicStateChanged(bool state);
    void onTextCursorUnderlineStateChanged(bool state);
    void onTextCursorStrikethgouthStateChanged(bool state);
    void onTextCursorAlignLeftStateChanged(bool state);
    void onTextCursorAlignCenterStateChanged(bool state);
    void onTextCursorAlignRightStateChanged(bool state);
    void onTextCursorAlignFullStateChanged(bool state);
    void onTextCursorInsideOrderedListStateChanged(bool state);
    void onTextCursorInsideUnorderedListStateChanged(bool state);
    void onTextCursorInsideTableStateChanged(bool state);

    void onTextCursorOnImageResourceStateChanged(
        bool state, QByteArray resourceHash);

    void onTextCursorOnNonImageResourceStateChanged(
        bool state, QByteArray resourceHash);

    void onTextCursorOnEnCryptTagStateChanged(
        bool state, QString encryptedText, QString cipher, QString length);

    void onTextCursorFontNameChanged(QString fontName);
    void onTextCursorFontSizeChanged(int fontSize);

    void onWriteFileRequestProcessed(
        bool success, ErrorString errorDescription, QUuid requestId);

    void onSpellCheckCorrectionAction();
    void onSpellCheckIgnoreWordAction();
    void onSpellCheckAddWordToUserDictionaryAction();

    void onSpellCheckCorrectionActionDone(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onSpellCheckCorrectionUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onSpellCheckerDynamicHelperUpdate(QStringList words);

    void onSpellCheckerReady();

    void onImageResourceResized(bool pushUndoCommand);

    void onSelectionFormattedAsSourceCode(
        const QVariant & response,
        const QVector<std::pair<QString, QString>> & extraData);

    // Slots for delegates
    void onAddResourceDelegateFinished(
        Resource addedResource, QString resourceFileStoragePath);

    void onAddResourceDelegateError(ErrorString error);

    void onAddResourceUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onRemoveResourceDelegateFinished(
        Resource removedResource, bool reversible);

    void onRemoveResourceDelegateCancelled(QString resourceLocalUid);
    void onRemoveResourceDelegateError(ErrorString error);

    void onRemoveResourceUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onRenameResourceDelegateFinished(
        QString oldResourceName, QString newResourceName, Resource resource,
        bool performingUndo);

    void onRenameResourceDelegateCancelled();
    void onRenameResourceDelegateError(ErrorString error);

    void onImageResourceRotationDelegateFinished(
        QByteArray resourceDataBefore, QByteArray resourceHashBefore,
        QByteArray resourceRecognitionDataBefore,
        QByteArray resourceRecognitionDataHashBefore,
        QSize resourceImageSizeBefore, Resource resourceAfter,
        INoteEditorBackend::Rotation rotationDirection);

    void onImageResourceRotationDelegateError(ErrorString error);

    void onHideDecryptedTextFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onHideDecryptedTextUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onEncryptSelectedTextDelegateFinished();
    void onEncryptSelectedTextDelegateCancelled();
    void onEncryptSelectedTextDelegateError(ErrorString error);

    void onEncryptSelectedTextUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onDecryptEncryptedTextDelegateFinished(
        QString encryptedText, QString cipher, size_t length, QString hint,
        QString decryptedText, QString passphrase, bool rememberForSession,
        bool decryptPermanently);

    void onDecryptEncryptedTextDelegateCancelled();
    void onDecryptEncryptedTextDelegateError(ErrorString error);

    void onDecryptEncryptedTextUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onAddHyperlinkToSelectedTextDelegateFinished();
    void onAddHyperlinkToSelectedTextDelegateCancelled();
    void onAddHyperlinkToSelectedTextDelegateError(ErrorString error);

    void onAddHyperlinkToSelectedTextUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onEditHyperlinkDelegateFinished();
    void onEditHyperlinkDelegateCancelled();
    void onEditHyperlinkDelegateError(ErrorString error);

    void onEditHyperlinkUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onRemoveHyperlinkDelegateFinished();
    void onRemoveHyperlinkDelegateError(ErrorString error);

    void onRemoveHyperlinkUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onInsertHtmlDelegateFinished(
        QList<Resource> addedResources, QStringList resourceFileStoragePaths);

    void onInsertHtmlDelegateError(ErrorString error);

    void onInsertHtmlUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onSourceCodeFormatUndoRedoFinished(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    // Slots for undo command signals
    void onUndoCommandError(ErrorString error);

    void onSpellCheckerDictionaryEnabledOrDisabled(bool checked);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    void getHtmlForPrinting();
#endif

    // Slots for signals from NoteEditorLocalStorageBroker
    void onNoteSavedToLocalStorage(QString noteLocalUid);

    void onFailedToSaveNoteToLocalStorage(
        QString noteLocalUid, ErrorString errorDescription);

    void onFoundNoteAndNotebook(Note note, Notebook notebook);

    void onFailedToFindNoteOrNotebook(
        QString noteLocalUid, ErrorString errorDescription);

    void onNoteUpdated(Note note);
    void onNotebookUpdated(Notebook notebook);
    void onNoteDeleted(QString noteLocalUid);
    void onNotebookDeleted(QString notebookLocalUid);
    void onFoundResourceData(Resource resource);

    void onFailedToFindResourceData(
        QString resourceLocalUid, ErrorString errorDescription);

    // Slots for signals from ResourceDataInTemporaryFileStorageManager

    void onFailedToPutResourceDataInTemporaryFile(
        QString resourceLocalUid, QString noteLocalUid,
        ErrorString errorDescription);

    void onNoteResourceTemporaryFilesPreparationProgress(
        double progress, QString noteLocalUid);

    void onNoteResourceTemporaryFilesPreparationError(
        QString noteLocalUid, ErrorString errorDescription);

    void onNoteResourceTemporaryFilesReady(QString noteLocalUid);

    void onOpenResourceInExternalEditorPreparationProgress(
        double progress, QString resourceLocalUid, QString noteLocalUid);

    void onFailedToOpenResourceInExternalEditor(
        QString resourceLocalUid, QString noteLocalUid,
        ErrorString errorDescription);

    void onOpenedResourceInExternalEditor(
        QString resourceLocalUid, QString noteLocalUid);

private:
    void init();

    void handleHyperlinkClicked(const QUrl & url);
    void handleInAppLinkClicked(const QString & urlString);

    bool parseInAppLink(
        const QString & urlString, QString & userId, QString & shardId,
        QString & noteGuid, ErrorString & errorDescription) const;

    bool checkNoteSize(
        const QString & newNoteContent, ErrorString & errorDescription) const;

    void pushNoteContentEditUndoCommand();

    void pushTableActionUndoCommand(
        const QString & name, NoteEditorPage::Callback callback);

    void pushInsertHtmlUndoCommand(
        const QList<Resource> & addedResources = {},
        const QStringList & resourceFileStoragePaths = {});

    template <typename T>
    QString composeHtmlTable(
        const T width, const T singleColumnWidth, const int rows,
        const int columns, const bool relative);

    void onManagedPageActionFinished(
        const QVariant & result,
        const QVector<std::pair<QString, QString>> & extraData);

    void updateJavaScriptBindings();

    void changeFontSize(const bool increase);
    void changeIndentation(const bool increase);

    void findText(
        const QString & textToFind, const bool matchCase,
        const bool searchBackward = false, NoteEditorPage::Callback = 0) const;

    /**
     * When no note is set to the editor,
     * it displays some "replacement" or "blank" page.
     * This page can be different depending on the state of the editor
     */
    enum class BlankPageKind
    {
        /**
         * Blank page of "Initial" kind is displayed before the note is set
         * to the editor
         */
        Initial = 0,
        /**
         * Blank page of "NoteNotFound" kind is displayed if no note
         * corresponding to the local uid passed to setCurrentNoteLocalUid
         * slot was found * within the local storage
         */
        NoteNotFound,
        /**
         * Blank page of "NoteDeleted" kind is displayed if the note which
         * was displayed by the editor was deleted (either marked as "deleted"
         * or deleted permanently (expunged) from the local storage
         */
        NoteDeleted,
        /**
         * Blank page of "NoteLoading" kind is displayed after the note
         * local uid is set to the editor but before the editor is ready
         * to display the note
         */
        NoteLoading,
        /**
         * Blank page of "InternalError" kind is displayed if the note editor
         * cannot display the note for some reason
         */
        InternalError
    };

    friend QDebug & operator<<(QDebug & dbg, const BlankPageKind kind);

    /**
     * Reset the page displayed by the note editor to one of "blank" ones
     * not corresponding to any note
     *
     * @param kind                  The kind of replacement page for the note
     *                              editor
     * @param errorDescription      The description of error used if kind is
     *                              "InternalError"
     */
    void clearEditorContent(
        const BlankPageKind kind = BlankPageKind::Initial,
        const ErrorString & errorDescription = ErrorString());

    void noteToEditorContent();
    void updateColResizableTableBindings();
    void inkNoteToEditorContent();

    bool htmlToNoteContent(ErrorString & errorDescription);

    void updateHashForResourceTag(
        const QByteArray & oldResourceHash, const QByteArray & newResourceHash);

    void provideSrcForResourceImgTags();

    void manualSaveResourceToFile(const Resource & resource);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    void provideSrcAndOnClickScriptForImgEnCryptTags();

    void setupGenericResourceImages();

    // Returns true if the resource image gets built and is being saved
    // to a file asynchronously
    bool findOrBuildGenericResourceImage(const Resource & resource);

    void setupWebSocketServer();
    void setupJavaScriptObjects();
    void setupTextCursorPositionTracking();
#endif

    void setupGenericTextContextMenu(
        const QStringList & extraData, const QString & selectedHtml,
        bool insideDecryptedTextFragment);

    void setupImageResourceContextMenu(const QByteArray & resourceHash);
    void setupNonImageResourceContextMenu(const QByteArray & resourceHash);

    void setupEncryptedTextContextMenu(
        const QString & cipher, const QString & keyLength,
        const QString & encryptedText, const QString & hint,
        const QString & id);

    void setupActionShortcut(
        const int key, const QString & context, QAction & action);

    void setupFileIO();
    void setupSpellChecker();
    void setupScripts();
    void setupGeneralSignalSlotConnections();
    void setupNoteEditorPage();
    void setupNoteEditorPageConnections(NoteEditorPage * page);
    void setupTextCursorPositionJavaScriptHandlerConnections();
    void setupSkipRulesForHtmlToEnmlConversion();

    QString noteNotFoundPageHtml() const;
    QString noteDeletedPageHtml() const;
    QString noteLoadingPageHtml() const;
    QString noteEditorPagePrefix() const;

    QString bodyStyleCss() const;
    void appendDefaultFontInfoToCss(QTextStream & strm) const;

    QString initialPageHtml() const;
    QString composeBlankPageHtml(const QString & rawText) const;

    void determineStatesForCurrentTextCursorPosition();
    void determineContextMenuEventTarget();

    void setPageEditable(const bool editable);

    bool checkContextMenuSequenceNumber(const quint64 sequenceNumber) const;

    void onPageHtmlReceived(
        const QString & html,
        const QVector<std::pair<QString, QString>> & extraData = {});

    void onSelectedTextEncryptionDone(
        const QVariant & dummy,
        const QVector<std::pair<QString, QString>> & extraData);

    void onTableActionDone(
        const QVariant & dummy,
        const QVector<std::pair<QString, QString>> & extraData);

    int resourceIndexByHash(
        const QList<Resource> & resources,
        const QByteArray & resourceHash) const;

    void writeNotePageFile(const QString & html);

    bool parseEncryptedTextContextMenuExtraData(
        const QStringList & extraData, QString & encryptedText,
        QString & decryptedText, QString & cipher, QString & keyLength,
        QString & hint, QString & id, ErrorString & errorDescription) const;

    void setupPasteGenericTextMenuActions();
    void setupParagraphSubMenuForGenericTextMenu(const QString & selectedHtml);
    void setupStyleSubMenuForGenericTextMenu();
    void setupSpellCheckerDictionariesSubMenuForGenericTextMenu();

    void rebuildRecognitionIndicesCache();

    void enableSpellCheck();
    void disableSpellCheck();

    void onSpellCheckSetOrCleared(
        const QVariant & dummy,
        const QVector<std::pair<QString, QString>> & extraData);

    void updateBodyStyle();
    void onBodyStyleUpdated(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onFontFamilyUpdated(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    void onFontHeightUpdated(
        const QVariant & data,
        const QVector<std::pair<QString, QString>> & extraData);

    bool isNoteReadOnly() const;

    void setupAddHyperlinkDelegate(
        const quint64 hyperlinkId, const QString & presetHyperlink = {},
        const QString & replacementLinkText = {});

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    void onPageHtmlReceivedForPrinting(
        const QString & html,
        const QVector<std::pair<QString, QString>> & extraData = {});
#endif

    void clearCurrentNoteInfo();
    void reloadCurrentNote();

    void clearPrepareResourceForOpeningProgressDialog(
        const QString & resourceLocalUid);

private:
    // Overrides for some Qt's virtual methods
    virtual void timerEvent(QTimerEvent * pEvent) override;
    virtual void dragMoveEvent(QDragMoveEvent * pEvent) override;
    virtual void dropEvent(QDropEvent * pEvent) override;

    void pasteImageData(const QMimeData & mimeData);

    void escapeStringForJavaScript(QString & str) const;

private:
    template <class T>
    class Q_DECL_HIDDEN NoteEditorCallbackFunctor
    {
    public:
        NoteEditorCallbackFunctor(
            NoteEditorPrivate * pNoteEditor,
            void (NoteEditorPrivate::*method)(
                const T & result,
                const QVector<std::pair<QString, QString>> & extraData),
            const QVector<std::pair<QString, QString>> & extraData = {}) :
            m_pNoteEditor(pNoteEditor),
            m_method(method), m_extraData(extraData)
        {}

        NoteEditorCallbackFunctor(const NoteEditorCallbackFunctor<T> & other) :
            m_pNoteEditor(other.m_pNoteEditor), m_method(other.m_method),
            m_extraData(other.m_extraData)
        {}

        NoteEditorCallbackFunctor & operator=(
            const NoteEditorCallbackFunctor<T> & other)
        {
            if (this != &other) {
                m_pNoteEditor = other.m_pNoteEditor;
                m_method = other.m_method;
                m_extraData = other.m_extraData;
            }

            return *this;
        }

        void operator()(const T & result)
        {
            if (Q_LIKELY(!m_pNoteEditor.isNull())) {
                (m_pNoteEditor->*m_method)(result, m_extraData);
            }
        }

    private:
        QPointer<NoteEditorPrivate> m_pNoteEditor;

        void (NoteEditorPrivate::*m_method)(
            const T & result,
            const QVector<std::pair<QString, QString>> & extraData);

        QVector<std::pair<QString, QString>> m_extraData;
    };

    friend class NoteEditorCallbackFunctor<QString>;
    friend class NoteEditorCallbackFunctor<QVariant>;

    class Q_DECL_HIDDEN ReplaceCallback
    {
    public:
        ReplaceCallback(NoteEditorPrivate * pNoteEditor) :
            m_pNoteEditor(pNoteEditor)
        {}

        void operator()(const QVariant & data)
        {
            if (Q_UNLIKELY(m_pNoteEditor.isNull())) {
                return;
            }

            m_pNoteEditor->onReplaceJavaScriptDone(data);
        }

    private:
        QPointer<NoteEditorPrivate> m_pNoteEditor;
    };

    enum class Alignment
    {
        Left = 0,
        Center,
        Right,
        Full
    };

    struct Q_DECL_HIDDEN TextFormattingState
    {
        bool m_bold = false;
        bool m_italic = false;
        bool m_underline = false;
        bool m_strikethrough = false;

        Alignment m_alignment = Alignment::Left;

        bool m_insideOrderedList = false;
        bool m_insideUnorderedList = false;
        bool m_insideTable = false;

        bool m_onImageResource = false;
        bool m_onNonImageResource = false;
        QString m_resourceHash;

        bool m_onEnCryptTag = false;
        QString m_encryptedText;
        QString m_cipher;
        QString m_length;
    };

    // Holds some data required for certain context menu actions, like
    // the encrypted text data for its decryption, the hash of the resource
    // under cursor for which the action is toggled etc.
    struct Q_DECL_HIDDEN CurrentContextMenuExtraData
    {
        QString m_contentType;

        // Encrypted text extra data
        QString m_encryptedText;
        QString m_decryptedText;
        QString m_keyLength;
        QString m_cipher;
        QString m_hint;
        bool m_insideDecryptedText = false;
        QString m_id;

        // Resource extra data
        QByteArray m_resourceHash;
    };

private:
    QString m_noteEditorPageFolderPath;
    QString m_genericResourceImageFileStoragePath;

    QFont m_font;

    // JavaScript scripts
    QString m_jQueryJs;
    QString m_jQueryUiJs;
    QString m_resizableTableColumnsJs;
    QString m_resizableImageManagerJs;
    QString m_debounceJs;
    QString m_rangyCoreJs;
    QString m_rangySelectionSaveRestoreJs;
    QString m_onTableResizeJs;
    QString m_nodeUndoRedoManagerJs;
    QString m_selectionManagerJs;
    QString m_textEditingUndoRedoManagerJs;
    QString m_getSelectionHtmlJs;
    QString m_snapSelectionToWordJs;
    QString m_replaceSelectionWithHtmlJs;
    QString m_updateResourceHashJs;
    QString m_updateImageResourceSrcJs;
    QString m_provideSrcForResourceImgTagsJs;
    QString m_setupEnToDoTagsJs;
    QString m_flipEnToDoCheckboxStateJs;
    QString m_onResourceInfoReceivedJs;
    QString m_findInnermostElementJs;
    QString m_determineStatesForCurrentTextCursorPositionJs;
    QString m_determineContextMenuEventTargetJs;
    QString m_pageMutationObserverJs;
    QString m_tableManagerJs;
    QString m_resourceManagerJs;
    QString m_htmlInsertionManagerJs;
    QString m_sourceCodeFormatterJs;
    QString m_hyperlinkManagerJs;
    QString m_encryptDecryptManagerJs;
    QString m_hilitorJs;
    QString m_imageAreasHilitorJs;
    QString m_findReplaceManagerJs;
    QString m_spellCheckerJs;
    QString m_managedPageActionJs;
    QString m_setInitialCaretPositionJs;
    QString m_toDoCheckboxAutomaticInsertionJs;
    QString m_disablePasteJs;
    QString m_findAndReplaceDOMTextJs;
    QString m_tabAndShiftTabIndentAndUnindentReplacerJs;
    QString m_replaceStyleJs;
    QString m_setFontFamilyJs;
    QString m_setFontSizeJs;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QString m_qWebKitSetupJs;
#else
    QString m_provideSrcForGenericResourceImagesJs;
    QString m_onGenericResourceImageReceivedJs;
    QString m_provideSrcAndOnClickScriptForEnCryptImgTagsJs;
    QString m_qWebChannelJs;
    QString m_qWebChannelSetupJs;
    QString m_notifyTextCursorPositionChangedJs;
    QString m_setupTextCursorPositionTrackingJs;
    QString m_genericResourceOnClickHandlerJs;
    QString m_setupGenericResourceOnClickHandlerJs;
    QString m_clickInterceptorJs;

    QWebSocketServer * m_pWebSocketServer;
    WebSocketClientWrapper * m_pWebSocketClientWrapper;
    QWebChannel * m_pWebChannel;
    EnCryptElementOnClickHandler * m_pEnCryptElementClickHandler;
    GenericResourceOpenAndSaveButtonsOnClickHandler *
        m_pGenericResourceOpenAndSaveButtonsOnClickHandler;
    HyperlinkClickJavaScriptHandler * m_pHyperlinkClickJavaScriptHandler;
    WebSocketWaiter * m_pWebSocketWaiter;

    bool m_setUpJavaScriptObjects = false;

    bool m_webSocketReady = false;
    quint16 m_webSocketServerPort = 0;
#endif

    GenericResourceImageManager * m_pGenericResourceImageManager = nullptr;

    SpellCheckerDynamicHelper * m_pSpellCheckerDynamicHandler;
    TableResizeJavaScriptHandler * m_pTableResizeJavaScriptHandler;
    ResizableImageJavaScriptHandler * m_pResizableImageJavaScriptHandler;
    ToDoCheckboxOnClickHandler * m_pToDoCheckboxClickHandler;
    ToDoCheckboxAutomaticInsertionHandler *
        m_pToDoCheckboxAutomaticInsertionHandler;
    PageMutationHandler * m_pPageMutationHandler;

    ActionsWatcher * m_pActionsWatcher;

    QUndoStack * m_pUndoStack = nullptr;

    std::unique_ptr<Account> m_pAccount;

    QString m_htmlForPrinting;

    QString m_initialPageHtml;
    QString m_noteNotFoundPageHtml;
    QString m_noteDeletedPageHtml;
    QString m_noteLoadingPageHtml;

    bool m_noteWasNotFound = false;
    bool m_noteWasDeleted = false;

    quint64 m_contextMenuSequenceNumber =
        1; // NOTE: must start from 1 as JavaScript treats 0 as null!
    QPoint m_lastContextMenuEventGlobalPos;
    QPoint m_lastContextMenuEventPagePos;
    ContextMenuEventJavaScriptHandler * m_pContextMenuEventJavaScriptHandler;

    TextCursorPositionJavaScriptHandler *
        m_pTextCursorPositionJavaScriptHandler;

    TextFormattingState m_currentTextFormattingState;

    QUuid m_writeNoteHtmlToFileRequestId;

    bool m_isPageEditable = false;
    bool m_pendingConversionToNote = false;
    bool m_pendingConversionToNoteForSavingInLocalStorage = false;
    bool m_pendingNoteSavingInLocalStorage = false;
    bool m_shouldRepeatSavingNoteInLocalStorage = false;
    bool m_pendingNotePageLoad = false;
    bool m_pendingNoteImageResourceTemporaryFiles = false;

    /**
     * Two following variables deserve special explanation. Since Qt 5.9
     * QWebEnginePage::load method started to behave really weirdly: it seems
     * when it's called for the first time, the method blocks the event loop
     * until the page is actually loaded. I.e. when the page got loaded,
     * the execution of code after the call to QWebEnginePage::load
     * (or QWebEnginePage::setUrl since it calls QWebEnginePage::load
     * internally) continues.
     *
     * Why to give a damn, you ask? Well, things become more interesting if you
     * attempt to call QWebEnginePage::load (or QWebEnginePage::setUrl) while
     * there's still an event loop blocked inside QWebEnginePage::load.
     * In particular, what seems to happen is that the second call to
     * QWebEnginePage::load does not block; the page seems to be loaded
     * successfully but then the original blocked call to QWebEnginePage::load
     * returns. The net effect is the appearance of the first loaded URL within
     * the page, not the second one.
     *
     * This behaviour has only been observed with Qt 5.9, not with any prior
     * version. It is (of course) not documented or mentioned anywhere, you have
     * to learn this on your own, the hard way. Thank you, Qt devs, you are
     * the best... not.
     *
     * Working around this issue using a special boolean flag indicating whether
     * the method is currently blocked in at least one event loop. If yes, won't
     * attempt to call QWebEnginePage::load (or QWebEnginePage::setUrl) until
     * the blocked method returns, instead will just save the next URL to load
     * and will load it later
     */
    bool m_pendingNotePageLoadMethodExit = false;
    QUrl m_pendingNextPageUrl;

    bool m_pendingIndexHtmlWritingToFile = false;
    bool m_pendingJavaScriptExecution = false;

    bool m_pendingBodyStyleUpdate = false;

    bool m_skipPushingUndoCommandOnNextContentChange = false;

    QString m_noteLocalUid;

    std::unique_ptr<QFont> m_pDefaultFont;
    std::unique_ptr<QPalette> m_pPalette;

    std::unique_ptr<Note> m_pNote;
    std::unique_ptr<Notebook> m_pNotebook;

    /**
     * This flag is set to true when the note editor page's content gets changed
     * and thus needs to be converted to HTML and then ENML and then put into
     * m_pNote object; when m_pNote's ENML becomes actual with the note editor
     * page's content, this flag is dropped back to false
     */
    bool m_needConversionToNote = false;

    /**
     * This flag is set to true when the note editor page's content gets
     * changed and thus needs to be converted to HTML and then ENML and then put
     * into m_pNote object which then needs to be saved in the local storage. Or
     * when m_pNote object changes via some other way and needs to be saved in
     * the local storage. This flag is dropped back to false after the note has
     * been saved to the local storage.
     */
    bool m_needSavingNoteInLocalStorage = false;

    /**
     * These two bools implement a cheap scheme of watching
     * for changes in note editor since some particular moment in time.
     * For example, the conversion of note from HTML into ENML happens
     * in the background mode, when the editor is idle for at least N seconds.
     * How can such idle state be determined? Create a timer for N seconds,
     * as it begins, set m_watchingForContentChange to true and
     * m_contentChangedSinceWatchingStart to false. On every next content change
     * m_contentChangedSinceWatchingStart would be set to true. When the timer
     * ends, it can check the state of m_contentChangedSinceWatchingStart. If
     * it's true, it means the editing is still in progress and it's not nice to
     * block the GUI thread by HTML to ENML conversion. So drop this variable
     * into false again and wait for another N seconds. And only if there were
     * no further edits during N seconds, convert note editor's page to ENML
     */
    bool m_watchingForContentChange = false;
    bool m_contentChangedSinceWatchingStart = false;

    int m_secondsToWaitBeforeConversionStart = 30;

    int m_pageToNoteContentPostponeTimerId = 0;

    /**
     * Timestamp corresponding to the last user's interaction with the note
     * editor
     */
    qint64 m_lastInteractionTimestamp = -1;

    std::shared_ptr<EncryptionManager> m_encryptionManager;
    std::shared_ptr<DecryptedTextManager> m_decryptedTextManager;

    ENMLConverter m_enmlConverter;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    NoteEditorPluginFactory * m_pPluginFactory = nullptr;
#endif

    // Progress dialogs for note resources requested to be opened
    std::vector<std::pair<QString, QProgressDialog *>>
        m_prepareResourceForOpeningProgressDialogs;

    QMenu * m_pGenericTextContextMenu = nullptr;
    QMenu * m_pImageResourceContextMenu = nullptr;
    QMenu * m_pNonImageResourceContextMenu = nullptr;
    QMenu * m_pEncryptedTextContextMenu = nullptr;

    SpellChecker * m_pSpellChecker = nullptr;
    bool m_spellCheckerEnabled = false;
    QSet<QString> m_currentNoteMisSpelledWords;
    StringUtils m_stringUtils;

    QString m_lastSelectedHtml;
    QString m_lastSelectedHtmlForEncryption;
    QString m_lastSelectedHtmlForHyperlink;

    QString m_lastMisSpelledWord;

    mutable QString m_lastSearchHighlightedText;
    mutable bool m_lastSearchHighlightedTextCaseSensitivity = false;

    QString m_enmlCachedMemory;  // Cached memory for HTML to ENML conversions
    QString m_htmlCachedMemory;  // Cached memory for ENML from Note -> HTML
                                 // conversions
    QString m_errorCachedMemory; // Cached memory for various errors

    QVector<ENMLConverter::SkipHtmlElementRule>
        m_skipRulesForHtmlToEnmlConversion;

    ResourceDataInTemporaryFileStorageManager *
        m_pResourceDataInTemporaryFileStorageManager = nullptr;
    FileIOProcessorAsync * m_pFileIOProcessorAsync;

    ResourceInfo m_resourceInfo;
    ResourceInfoJavaScriptHandler * m_pResourceInfoJavaScriptHandler;

    QHash<QString, QString> m_resourceFileStoragePathsByResourceLocalUid;
    QSet<QUuid> m_manualSaveResourceToFileRequestIds;

    QHash<QString, QStringList> m_fileSuffixesForMimeType;
    QHash<QString, QString> m_fileFilterStringForMimeType;

    QHash<QByteArray, QString> m_genericResourceImageFilePathsByResourceHash;

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    GenericResourceImageJavaScriptHandler *
        m_pGenericResoureImageJavaScriptHandler;
#endif

    QSet<QUuid> m_saveGenericResourceImageToFileRequestIds;

    QHash<QByteArray, ResourceRecognitionIndices>
        m_recognitionIndicesByResourceHash;

    CurrentContextMenuExtraData m_currentContextMenuExtraData;

    QSet<QUuid> m_resourceLocalUidsPendingFindDataInLocalStorageForSavingToFile;
    QHash<QUuid, Rotation>
        m_rotationTypeByResourceLocalUidsPendingFindDataInLocalStorage;

    quint64 m_lastFreeEnToDoIdNumber = 1;
    quint64 m_lastFreeHyperlinkIdNumber = 1;
    quint64 m_lastFreeEnCryptIdNumber = 1;
    quint64 m_lastFreeEnDecryptedIdNumber = 1;

    NoteEditor * const q_ptr;
    Q_DECLARE_PUBLIC(NoteEditor)
};

} // namespace quentier

void initNoteEditorResources();

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_P_H
