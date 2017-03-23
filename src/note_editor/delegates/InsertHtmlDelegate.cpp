#include "InsertHtmlDelegate.h"
#include "../NoteEditor_p.h"
#include <quentier/enml/ENMLConverter.h>
#include <quentier/types/Account.h>
#include <quentier/note_editor/ResourceFileStorageManager.h>
#include <quentier/utility/FileIOThreadWorker.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

InsertHtmlDelegate::InsertHtmlDelegate(const QString & inputHtml, NoteEditorPrivate & noteEditor,
                                       ENMLConverter & enmlConverter,
                                       ResourceFileStorageManager * pResourceFileStorageManager,
                                       FileIOThreadWorker * pFileIOThreadWorker,
                                       QObject * parent) :
    QObject(parent),
    m_noteEditor(noteEditor),
    m_enmlConverter(enmlConverter),
    m_pResourceFileStorageManager(pResourceFileStorageManager),
    m_pFileIOThreadWorker(pFileIOThreadWorker),
    m_inputHtml(inputHtml)
{}

void InsertHtmlDelegate::start()
{
    QNDEBUG(QStringLiteral("InsertHtmlDelegate::start"));

    if (m_noteEditor.isModified()) {
        QObject::connect(&m_noteEditor, QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this, QNSLOT(InsertHtmlDelegate,onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void InsertHtmlDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(QStringLiteral("InsertHtmlDelegate::onOriginalPageConvertedToNote"));

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor, QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this, QNSLOT(InsertHtmlDelegate,onOriginalPageConvertedToNote,Note));

    doStart();
}

void InsertHtmlDelegate::doStart()
{
    QNDEBUG(QStringLiteral("InsertHtmlDelegate::doStart"));

    // TODO: implement
}

} // namespace quentier
