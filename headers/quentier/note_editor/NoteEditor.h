/*
 * Copyright 2016-2023 Dmitry Ivanov
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

#pragma once

#include <quentier/local_storage/Fwd.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>

#include <QPrinter>
#include <QStringList>
#include <QThread>
#include <QWidget>

class QUndoStack;

namespace quentier {

class Account;
class INoteEditorBackend;
class SpellChecker;

/**
 * @brief The NoteEditor class is a widget encapsulating all the functionality
 * necessary for showing and editing notes
 */
class QUENTIER_EXPORT NoteEditor : public QWidget
{
    Q_OBJECT
public:
    explicit NoteEditor(
        QWidget * parent = nullptr,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        Qt::WindowFlags flags = {});
#else
        Qt::WindowFlags flags = 0); // NOLINT
#endif

    ~NoteEditor() noexcept override;

    /**
     * NoteEditor requires LocalStorageManagerAsync, SpellChecker and
     * Account for its work but due to the particularities of Qt's .ui files
     * processing these can't be passed right inside the constructor,
     * hence here's a special initialization method
     *
     * @param localStorage                  Local storage
     * @param spellChecker                  Spell checker to be used by note
     *                                      editor
     * @param account                       Current account
     * @param pBackgroundJobsThread         Pointer to the thread to be used for
     *                                      scheduling of background jobs
     *                                      of NoteEditor; if null, NoteEditor's
     *                                      background jobs would take place in
     *                                      GUI thread
     */
    void initialize(
        local_storage::ILocalStoragePtr localStorage,
        SpellChecker & spellChecker, const Account & account,
        QThread * pBackgroundJobsThread = nullptr);

    /**
     * @return the pointer to the note editor's backend
     */
    [[nodiscard]] INoteEditorBackend * backend() noexcept;

    /**
     * This method can be used to set the backend to the note editor;
     * the note editor has the default backend so this method is not obligatory
     * to be called
     */
    void setBackend(INoteEditorBackend * backend);

    /**
     * Set the current account to the note editor
     */
    void setAccount(const Account & account);

    /**
     * Get the undo stack serving to the note editor
     */
    [[nodiscard]] const QUndoStack * undoStack() const noexcept;

    /**
     * Set the undo stack for the note editor to use
     */
    void setUndoStack(QUndoStack * pUndoStack);

    /**
     * Set the html to be displayed when the note is not set to the editor
     */
    void setInitialPageHtml(const QString & html);

    /**
     * Set the html to be displayed when the note attempted to be set to
     * the editor was not found within the local storage
     */
    void setNoteNotFoundPageHtml(const QString & html);

    /**
     * Set the html to be displayed when the note set to the editor was deleted
     * from the local storage (either marked as deleted or deleted permanently
     * i.e. expunged)
     */
    void setNoteDeletedPageHtml(const QString & html);

    /**
     * Set the html to be displayed when the note set to the editor is being
     * loaded into it
     */
    void setNoteLoadingPageHtml(const QString & html);

    /**
     * Get the local id of the note currently set to the note editor
     */
    [[nodiscard]] QString currentNoteLocalId() const;

    /**
     * Set note local id to the note editor. The note is being searched for
     * within the local storage, in case of no note being found noteNotFound
     * signal is emitted. Otherwise note editor page starts loading.
     *
     * @param noteLocalId               The local id of note
     */
    void setCurrentNoteLocalId(const QString & noteLocalId);

    /**
     * Clear the contents of the note editor
     */
    void clear();

    /**
     * @return true if there's content within the editor not yet converted to
     * note or not saved to local storage, false otherwise
     */
    [[nodiscard]] bool isModified() const noexcept;

    /**
     * @return true if there's content within the editor not yet converted to
     * note, false otherwise
     */
    [[nodiscard]] bool isEditorPageModified() const noexcept;

    /**
     * @return true if the note last set to the editor has been fully
     * loaded already, false otherwise
     */
    [[nodiscard]] bool isNoteLoaded() const noexcept;

    /**
     * @return the number of milliseconds since the last user's interaction
     * with the note editor or -1 if there was no interaction or if no note
     * is loaded at the moment
     */
    [[nodiscard]] qint64 idleTime() const noexcept;

    /**
     * Sets the focus to the backend note editor widget
     */
    void setFocus();

    [[nodiscard]] QString selectedText() const noexcept;
    [[nodiscard]] bool hasSelection() const noexcept;

    [[nodiscard]] bool spellCheckEnabled() const noexcept;

    [[nodiscard]] bool print(
        QPrinter & printer, ErrorString & errorDescription);

    [[nodiscard]] bool exportToPdf(
        const QString & absoluteFilePath, ErrorString & errorDescription);

    [[nodiscard]] bool exportToEnex(
        const QStringList & tagNames, QString & enex,
        ErrorString & errorDescription);

    /**
     * @return palette containing default colors used by the editor; the palette
     * is composed of colors from note editor widget's native palette but some
     * of them might be overridden by colors from the palette specified
     * previously via setDefaultPalette method: those colors from the specified
     * palette which were valid
     */
    [[nodiscard]] QPalette defaultPalette() const;

    /**
     * @return pointer to the default font used by the note editor; if no such
     *         font was set to the editor previously, returns null pointer
     */
    [[nodiscard]] const QFont * defaultFont() const;

Q_SIGNALS:
    /**
     * @brief contentChanged signal is emitted when the note's content (text)
     * gets modified via manual editing (i.e. not any action like paste or cut)
     */
    void contentChanged();

    /**
     * @brief noteAndNotebookFoundInLocalStorage signal is emitted when note and
     * its corresponding notebook were found within the local storage right
     * before the note editor starts to load the note into the editor
     */
    void noteAndNotebookFoundInLocalStorage(
        qevercloud::Note note, qevercloud::Notebook notebook);

    /**
     * @brief noteNotFound signal is emitted when the note could not be found
     * within the local storage by the provided local id
     */
    void noteNotFound(QString noteLocalId);

    /**
     * @brief noteDeleted signal is emitted when the note displayed within the
     * note editor is deleted. The note editor stops displaying the note in this
     * case shortly after emitting this signal
     */
    void noteDeleted(QString noteLocalId);

    /**
     * @brief noteModified signal is emitted when the note's content within
     * the editor gets modified via some way - either via manual editing or
     * via some action (like paste or cut)
     */
    void noteModified();

    /**
     * @brief notifyError signal is emitted when NoteEditor encounters some
     * problem worth letting the user to know about
     */
    void notifyError(ErrorString error);

    /**
     * @brief inAppNoteLinkClicked signal is emitted when the in-app note link
     * is clicked within the note editor
     */
    void inAppNoteLinkClicked(
        QString userId, QString shardId, QString noteGuid);

    /**
     * inAppNoteLinkPasteRequested signal is emitted when the note editor
     * detects the attempt to paste the in-app note link into the note editor;
     * the link would not be inserted right away, instead this signal would be
     * emitted. Whatever party managing the note editor is expected to connect
     * some slot to this signal and provide the optionally amended link
     * information to the note editor by sending the signal connected to its
     * insertInAppNoteLink slot - this slot accepts both the URL of the link and
     * the link text and performs the actual link insertion into the note.
     * If the link text is empty, the URL itself is used as the link text.
     */
    void inAppNoteLinkPasteRequested(
        QString url, QString userId, QString shardId, QString noteGuid);

    void convertedToNote(qevercloud::Note note);
    void cantConvertToNote(ErrorString error);

    void noteEditorHtmlUpdated(QString html);

    void currentNoteChanged(qevercloud::Note note);

    void spellCheckerNotReady();
    void spellCheckerReady();

    void noteLoaded();

    /**
     * @brief noteSavedToLocalStorage signal is emitted when the note has been
     * saved within the local storage. NoteEditor doesn't do this on its own
     * unless it's explicitly asked to do this via invoking its
     * saveNoteToLocalStorage slot
     */
    void noteSavedToLocalStorage(QString noteLocalId);

    /**
     * @brief failedToSaveNoteToLocalStorage signal is emitted in case of
     * failure to save the note to local storage
     */
    void failedToSaveNoteToLocalStorage(
        ErrorString errorDescription, QString noteLocalId);

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

public Q_SLOTS:
    /**
     * Invoke this slot to launch the asynchronous procedure of converting
     * the current contents of the note editor to note; the convertedToNote
     * signal would be emitted in response when the conversion is done
     */
    void convertToNote();

    /**
     * Invoke this slot to launch the asynchronous procedure of saving the
     * modified current note back to the local storage. If no note is set to the
     * editor or if the note is not modified, no action would be performed.
     * Otherwise noteSavedToLocalStorage signal would be emitted in case of
     * successful saving or failedToSaveNoteToLocalStorage would be emitted
     * otherwise
     */
    void saveNoteToLocalStorage();

    /**
     * Invoke this slot to set the title to the note displayed via the note
     * editor. The note editor itself doesn't manage the note title in any way
     * so any external code using the note editor can set the title to the note
     * editor's note which would be considered modified if the title is new and
     * then eventually the note would be saved to local storage
     *
     * @param noteTitle         The title of the note
     */
    void setNoteTitle(const QString & noteTitle);

    /**
     * Invoke this slot to set tag local ids and/or tag guids to the note
     * displayed via the note editor. The note editor itself doesn't manage the
     * note tags in any way so any external code using the note editor can set
     * the tag ids to the note editor's internal note which would be considered
     * modified if the tag ids are new and then eventually the note would be
     * saved to local storage
     *
     * @param tagLocalIds       The list of tag local ids for the note
     * @param tagGuids          The list of tag guids for the note
     */
    void setTagIds(
        const QStringList & tagLocalIds, const QStringList & tagGuids);

    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void pasteUnformatted();
    void selectAll();

    void formatSelectionAsSourceCode();

    void fontMenu();
    void textBold();
    void textItalic();
    void textUnderline();
    void textStrikethrough();
    void textHighlight();

    void alignLeft();
    void alignCenter();
    void alignRight();
    void alignFull();

    void findNext(const QString & text, bool matchCase) const;
    void findPrevious(const QString & text, bool matchCase) const;

    void replace(
        const QString & textToReplace, const QString & replacementText,
        bool matchCase);

    void replaceAll(
        const QString & textToReplace, const QString & replacementText,
        bool matchCase);

    void insertToDoCheckbox();

    void insertInAppNoteLink(
        const QString & userId, const QString & shardId,
        const QString & noteGuid, const QString & linkText);

    void setSpellcheck(bool enabled);

    void setFont(const QFont & font);
    void setFontHeight(int height);
    void setFontColor(const QColor & color);
    void setBackgroundColor(const QColor & color);

    /**
     * Sets the palette with colors to be used by the editor. New colors are
     * applied after the note is fully loaded. If no note is set to the editor,
     * the palette is simply remembered for the next note to be loaded into it.
     *
     * Colors within the palette and their usage:
     * 1. WindowText - used as default font color
     * 2. Base - used as default background color
     * 3. HighlightedText - used as font color for selected text
     * 4. Highlight - used as background color for selected text
     *
     * @param pal           The palette to be set. Invalid colors from it are
     *                      substituted by colors from widget's palette by
     *                      the editor
     */
    void setDefaultPalette(const QPalette & pal);

    /**
     * Sets the font which would be used by the editor by default
     *
     * @param font          The font to be used by the editor by default
     */
    void setDefaultFont(const QFont & font);

    void insertHorizontalLine();

    void increaseFontSize();
    void decreaseFontSize();

    void increaseIndentation();
    void decreaseIndentation();

    void insertBulletedList();
    void insertNumberedList();

    void insertTableDialog();

    void insertFixedWidthTable(
        int rows, int columns, int widthInPixels);

    void insertRelativeWidthTable(
        int rows, int columns, double relativeWidth);

    void insertTableRow();
    void insertTableColumn();
    void removeTableRow();
    void removeTableColumn();

    void addAttachmentDialog();
    void saveAttachmentDialog(const QByteArray & resourceHash);
    void saveAttachmentUnderCursor();
    void openAttachment(const QByteArray & resourceHash);
    void openAttachmentUnderCursor();
    void copyAttachment(const QByteArray & resourceHash);
    void copyAttachmentUnderCursor();

    void encryptSelectedText();
    void decryptEncryptedTextUnderCursor();

    void editHyperlinkDialog();
    void copyHyperlink();
    void removeHyperlink();

    void onNoteLoadCancelled();

protected:
    void dragMoveEvent(QDragMoveEvent * pEvent) override;
    void dropEvent(QDropEvent * pEvent) override;

private:
    INoteEditorBackend * m_backend;
};

} // namespace quentier
