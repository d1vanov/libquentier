#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H

#include <quentier/utility/Macros.h>
#include <quentier/types/Note.h>
#include <QObject>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Account)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)
QT_FORWARD_DECLARE_CLASS(ENMLConverter)
QT_FORWARD_DECLARE_CLASS(ResourceFileStorageManager)
QT_FORWARD_DECLARE_CLASS(FileIOThreadWorker)

class InsertHtmlDelegate: public QObject
{
    Q_OBJECT
public:
    explicit InsertHtmlDelegate(const QString & inputHtml, NoteEditorPrivate & noteEditor,
                                ENMLConverter & enmlConverter,
                                ResourceFileStorageManager * pResourceFileStorageManager,
                                FileIOThreadWorker * pFileIOThreadWorker,
                                QObject * parent = Q_NULLPTR);

    void start();

Q_SIGNALS:
    void finished();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);

private:
    void doStart();

private:
    NoteEditorPrivate &             m_noteEditor;
    ENMLConverter &                 m_enmlConverter;

    ResourceFileStorageManager *    m_pResourceFileStorageManager;
    FileIOThreadWorker *            m_pFileIOThreadWorker;

    QString                         m_inputHtml;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
