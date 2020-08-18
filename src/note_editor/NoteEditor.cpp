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

#include <quentier/note_editor/NoteEditor.h>

#include "NoteEditor_p.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/note_editor/INoteEditorBackend.h>

#include <QColor>
#include <QCoreApplication>
#include <QDragMoveEvent>
#include <QFont>
#include <QUndoStack>
#include <QVBoxLayout>

namespace quentier {

NoteEditor::NoteEditor(QWidget * parent, Qt::WindowFlags flags) :
    QWidget(parent, flags), m_backend(new NoteEditorPrivate(*this))
{
    QVBoxLayout * pLayout = new QVBoxLayout;
    pLayout->addWidget(m_backend->widget());
    pLayout->setMargin(0);
    setLayout(pLayout);
    setAcceptDrops(true);
}

NoteEditor::~NoteEditor() {}

void NoteEditor::initialize(
    LocalStorageManagerAsync & localStorageManager, SpellChecker & spellChecker,
    const Account & account, QThread * pBackgroundJobsThread)
{
    m_backend->initialize(
        localStorageManager, spellChecker, account, pBackgroundJobsThread);
}

INoteEditorBackend * NoteEditor::backend()
{
    return m_backend;
}

void NoteEditor::setBackend(INoteEditorBackend * backend)
{
    QLayout * pLayout = layout();
    pLayout->removeWidget(m_backend->widget());

    backend->widget()->setParent(this, this->windowFlags());
    m_backend = backend;
    pLayout->addWidget(m_backend->widget());
}

void NoteEditor::setAccount(const Account & account)
{
    m_backend->setAccount(account);
}

void NoteEditor::setUndoStack(QUndoStack * pUndoStack)
{
    m_backend->setUndoStack(pUndoStack);
}

void NoteEditor::setInitialPageHtml(const QString & html)
{
    m_backend->setInitialPageHtml(html);
}

void NoteEditor::setNoteNotFoundPageHtml(const QString & html)
{
    m_backend->setNoteNotFoundPageHtml(html);
}

void NoteEditor::setNoteDeletedPageHtml(const QString & html)
{
    m_backend->setNoteDeletedPageHtml(html);
}

void NoteEditor::setNoteLoadingPageHtml(const QString & html)
{
    m_backend->setNoteLoadingPageHtml(html);
}

QString NoteEditor::currentNoteLocalUid() const
{
    return m_backend->currentNoteLocalUid();
}

void NoteEditor::setCurrentNoteLocalUid(const QString & noteLocalUid)
{
    m_backend->setCurrentNoteLocalUid(noteLocalUid);
}

void NoteEditor::clear()
{
    m_backend->clear();
}

bool NoteEditor::isModified() const
{
    return m_backend->isModified();
}

bool NoteEditor::isNoteLoaded() const
{
    return m_backend->isNoteLoaded();
}

qint64 NoteEditor::idleTime() const
{
    return m_backend->idleTime();
}

void NoteEditor::setFocus()
{
    m_backend->setFocusToEditor();
}

void NoteEditor::convertToNote()
{
    m_backend->convertToNote();
}

void NoteEditor::saveNoteToLocalStorage()
{
    m_backend->saveNoteToLocalStorage();
}

void NoteEditor::setNoteTitle(const QString & noteTitle)
{
    m_backend->setNoteTitle(noteTitle);
}

void NoteEditor::setTagIds(
    const QStringList & tagLocalUids, const QStringList & tagGuids)
{
    m_backend->setTagIds(tagLocalUids, tagGuids);
}

void NoteEditor::undo()
{
    m_backend->undo();
}

void NoteEditor::redo()
{
    m_backend->redo();
}

void NoteEditor::cut()
{
    m_backend->cut();
}

void NoteEditor::copy()
{
    m_backend->copy();
}

void NoteEditor::paste()
{
    m_backend->paste();
}

void NoteEditor::pasteUnformatted()
{
    m_backend->pasteUnformatted();
}

void NoteEditor::selectAll()
{
    m_backend->selectAll();
}

void NoteEditor::formatSelectionAsSourceCode()
{
    m_backend->formatSelectionAsSourceCode();
}

void NoteEditor::fontMenu()
{
    m_backend->fontMenu();
}

void NoteEditor::textBold()
{
    m_backend->textBold();
}

void NoteEditor::textItalic()
{
    m_backend->textItalic();
}

void NoteEditor::textUnderline()
{
    m_backend->textUnderline();
}

void NoteEditor::textStrikethrough()
{
    m_backend->textStrikethrough();
}

void NoteEditor::textHighlight()
{
    m_backend->textHighlight();
}

void NoteEditor::alignLeft()
{
    m_backend->alignLeft();
}

void NoteEditor::alignCenter()
{
    m_backend->alignCenter();
}

void NoteEditor::alignRight()
{
    m_backend->alignRight();
}

void NoteEditor::alignFull()
{
    m_backend->alignFull();
}

QString NoteEditor::selectedText() const
{
    return m_backend->selectedText();
}

bool NoteEditor::hasSelection() const
{
    return m_backend->hasSelection();
}

void NoteEditor::findNext(const QString & text, const bool matchCase) const
{
    m_backend->findNext(text, matchCase);
}

void NoteEditor::findPrevious(const QString & text, const bool matchCase) const
{
    m_backend->findPrevious(text, matchCase);
}

void NoteEditor::replace(
    const QString & textToReplace, const QString & replacementText,
    const bool matchCase)
{
    m_backend->replace(textToReplace, replacementText, matchCase);
}

void NoteEditor::replaceAll(
    const QString & textToReplace, const QString & replacementText,
    const bool matchCase)
{
    m_backend->replaceAll(textToReplace, replacementText, matchCase);
}

void NoteEditor::insertToDoCheckbox()
{
    m_backend->insertToDoCheckbox();
}

void NoteEditor::insertInAppNoteLink(
    const QString & userId, const QString & shardId, const QString & noteGuid,
    const QString & linkText)
{
    m_backend->insertInAppNoteLink(userId, shardId, noteGuid, linkText);
}

void NoteEditor::setSpellcheck(const bool enabled)
{
    m_backend->setSpellcheck(enabled);
}

bool NoteEditor::spellCheckEnabled() const
{
    return m_backend->spellCheckEnabled();
}

void NoteEditor::setFont(const QFont & font)
{
    m_backend->setFont(font);
}

void NoteEditor::setFontHeight(const int height)
{
    m_backend->setFontHeight(height);
}

void NoteEditor::setFontColor(const QColor & color)
{
    m_backend->setFontColor(color);
}

void NoteEditor::setBackgroundColor(const QColor & color)
{
    m_backend->setBackgroundColor(color);
}

QPalette NoteEditor::defaultPalette() const
{
    return m_backend->defaultPalette();
}

void NoteEditor::setDefaultPalette(const QPalette & pal)
{
    m_backend->setDefaultPalette(pal);
}

const QFont * NoteEditor::defaultFont() const
{
    return m_backend->defaultFont();
}

void NoteEditor::setDefaultFont(const QFont & font)
{
    m_backend->setDefaultFont(font);
}

void NoteEditor::insertHorizontalLine()
{
    m_backend->insertHorizontalLine();
}

void NoteEditor::increaseFontSize()
{
    m_backend->increaseFontSize();
}

void NoteEditor::decreaseFontSize()
{
    m_backend->decreaseFontSize();
}

void NoteEditor::increaseIndentation()
{
    m_backend->increaseIndentation();
}

void NoteEditor::decreaseIndentation()
{
    m_backend->decreaseIndentation();
}

void NoteEditor::insertBulletedList()
{
    m_backend->insertBulletedList();
}

void NoteEditor::insertNumberedList()
{
    m_backend->insertNumberedList();
}

void NoteEditor::insertTableDialog()
{
    m_backend->insertTableDialog();
}

void NoteEditor::insertFixedWidthTable(
    const int rows, const int columns, const int widthInPixels)
{
    m_backend->insertFixedWidthTable(rows, columns, widthInPixels);
}

void NoteEditor::insertRelativeWidthTable(
    const int rows, const int columns, const double relativeWidth)
{
    m_backend->insertRelativeWidthTable(rows, columns, relativeWidth);
}

void NoteEditor::insertTableRow()
{
    m_backend->insertTableRow();
}

void NoteEditor::insertTableColumn()
{
    m_backend->insertTableColumn();
}

void NoteEditor::removeTableRow()
{
    m_backend->removeTableRow();
}

void NoteEditor::removeTableColumn()
{
    m_backend->removeTableColumn();
}

void NoteEditor::addAttachmentDialog()
{
    m_backend->addAttachmentDialog();
}

void NoteEditor::saveAttachmentDialog(const QByteArray & resourceHash)
{
    m_backend->saveAttachmentDialog(resourceHash);
}

void NoteEditor::saveAttachmentUnderCursor()
{
    m_backend->saveAttachmentUnderCursor();
}

void NoteEditor::openAttachment(const QByteArray & resourceHash)
{
    m_backend->openAttachment(resourceHash);
}

void NoteEditor::openAttachmentUnderCursor()
{
    m_backend->openAttachmentUnderCursor();
}

void NoteEditor::copyAttachment(const QByteArray & resourceHash)
{
    m_backend->copyAttachment(resourceHash);
}

void NoteEditor::copyAttachmentUnderCursor()
{
    m_backend->copyAttachmentUnderCursor();
}

void NoteEditor::encryptSelectedText()
{
    m_backend->encryptSelectedText();
}

void NoteEditor::decryptEncryptedTextUnderCursor()
{
    m_backend->decryptEncryptedTextUnderCursor();
}

void NoteEditor::editHyperlinkDialog()
{
    m_backend->editHyperlinkDialog();
}

void NoteEditor::copyHyperlink()
{
    m_backend->copyHyperlink();
}

void NoteEditor::removeHyperlink()
{
    m_backend->removeHyperlink();
}

void NoteEditor::onNoteLoadCancelled()
{
    m_backend->onNoteLoadCancelled();
}

bool NoteEditor::print(QPrinter & printer, ErrorString & errorDescription)
{
    return m_backend->print(printer, errorDescription);
}

bool NoteEditor::exportToPdf(
    const QString & absoluteFilePath, ErrorString & errorDescription)
{
    return m_backend->exportToPdf(absoluteFilePath, errorDescription);
}

bool NoteEditor::exportToEnex(
    const QStringList & tagNames, QString & enex,
    ErrorString & errorDescription)
{
    return m_backend->exportToEnex(tagNames, enex, errorDescription);
}

void NoteEditor::dragMoveEvent(QDragMoveEvent * pEvent)
{
    if (Q_LIKELY(pEvent)) {
        pEvent->acceptProposedAction();
    }
}

void NoteEditor::dropEvent(QDropEvent * pEvent)
{
    if (Q_LIKELY(pEvent)) {
        pEvent->acceptProposedAction();
    }
}

} // namespace quentier
