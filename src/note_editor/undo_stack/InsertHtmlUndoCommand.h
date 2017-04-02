#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"
#include "../NoteEditorPage.h"
#include <quentier/utility/Macros.h>
#include <quentier/types/Resource.h>
#include <QStringList>
#include <QHash>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceInfo)

class InsertHtmlUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
    typedef NoteEditorPage::Callback Callback;
public:
    InsertHtmlUndoCommand(QList<Resource> addedResources, QStringList resourceFileStoragePaths,
                          const Callback & callback, NoteEditorPrivate & noteEditor,
                          QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
                          ResourceInfo & resourceInfo, QUndoCommand * parent = Q_NULLPTR);
    InsertHtmlUndoCommand(QList<Resource> addedResources, QStringList resourceFileStoragePaths,
                          const Callback & callback, NoteEditorPrivate & noteEditor,
                          QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
                          ResourceInfo & resourceInfo, const QString & text, QUndoCommand * parent = Q_NULLPTR);
    virtual ~InsertHtmlUndoCommand();

    virtual void undoImpl() Q_DECL_OVERRIDE;
    virtual void redoImpl() Q_DECL_OVERRIDE;

private:
    QList<Resource>     m_addedResources;
    QStringList         m_resourceFileStoragePaths;
    Callback            m_callback;

    QHash<QString, QString> &       m_resourceFileStoragePathsByResourceLocalUid;
    ResourceInfo &                  m_resourceInfo;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H
