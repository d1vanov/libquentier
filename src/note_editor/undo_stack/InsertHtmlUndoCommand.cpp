#include "InsertHtmlUndoCommand.h"
#include "../NoteEditor_p.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Utility.h>
#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QMimeType>

namespace quentier {

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditorPrivate.page()); \
    if (Q_UNLIKELY(!page)) { \
        ErrorString error(QT_TRANSLATE_NOOP("", "can't undo/redo the html insertion: no note editor page")); \
        QNWARNING(error); \
        emit notifyError(error); \
        return; \
    }

InsertHtmlUndoCommand::InsertHtmlUndoCommand(QList<Resource> addedResources, QStringList resourceFileStoragePaths,
                                             const Callback & callback, NoteEditorPrivate & noteEditor,
                                             QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
                                             ResourceInfo & resourceInfo, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths),
    m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalUid(resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo)
{
    setText(tr("Insert HTML"));
}

InsertHtmlUndoCommand::InsertHtmlUndoCommand(QList<Resource> addedResources, QStringList resourceFileStoragePaths,
                                             const Callback & callback, NoteEditorPrivate & noteEditor,
                                             QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
                                             ResourceInfo & resourceInfo, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths),
    m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalUid(resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo)
{}

InsertHtmlUndoCommand::~InsertHtmlUndoCommand()
{}

void InsertHtmlUndoCommand::undoImpl()
{
    QNDEBUG(QStringLiteral("InsertHtmlUndoCommand::undoImpl"));

    const QList<Resource> & addedResources = m_addedResources;
    int numResources = addedResources.size();

    for(int i = 0; i < numResources; ++i)
    {
        const Resource * pResource = &(addedResources.at(i));

        if (Q_UNLIKELY(!pResource->hasDataHash()))
        {
            QNDEBUG(QStringLiteral("One of added resources has no data hash: ") << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(QStringLiteral("This resource has no data body as well, just skipping it"));
                continue;
            }

            QByteArray hash = QCryptographicHash::hash(pResource->dataBody(), QCryptographicHash::Md5);
            m_addedResources[i].setDataHash(hash);
            // This might have caused detach, need to update the pointer to the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.removeResourceFromNote(*pResource);

        auto rit = m_resourceFileStoragePathsByResourceLocalUid.find(pResource->localUid());
        if (Q_LIKELY(rit != m_resourceFileStoragePathsByResourceLocalUid.end())) {
            Q_UNUSED(m_resourceFileStoragePathsByResourceLocalUid.erase(rit))
        }

        m_resourceInfo.removeResourceInfo(pResource->dataHash());
    }

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("htmlInsertionManager.undo();"), m_callback);
}

void InsertHtmlUndoCommand::redoImpl()
{
    QNDEBUG(QStringLiteral("InsertHtmlUndoCommand::redoImpl"));

    const QList<Resource> & addedResources = m_addedResources;
    int numResources = addedResources.size();

    QMimeDatabase mimeDatabase;

    for(int i = 0; i < numResources; ++i)
    {
        const Resource * pResource = &(addedResources.at(i));

        QMimeType mimeType;
        if (pResource->hasMime()) {
            mimeType = mimeDatabase.mimeTypeForName(pResource->mime());
        }

        if (Q_UNLIKELY(!mimeType.isValid()))
        {
            QNDEBUG(QStringLiteral("Could not deduce the resource data's mime type from the mime type name "
                                   "or resource has no declared mime type"));
            if (pResource->hasDataBody()) {
                QNDEBUG(QStringLiteral("Trying to deduce the mime type from the resource's data"));
                mimeType = mimeDatabase.mimeTypeForData(pResource->dataBody());
            }
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(QStringLiteral("All attempts to deduce the correct mime type have failed, fallback to mime type of image/png"));
            mimeType = mimeDatabase.mimeTypeForName(QStringLiteral("image/png"));
        }

        if (Q_UNLIKELY(!pResource->hasMime()))
        {
            QNDEBUG(QStringLiteral("One of added resources has no mime type: ") << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(QStringLiteral("This resource has no data body as well, just skipping it"));
                continue;
            }


            m_addedResources[i].setMime(mimeType.name());
            // This might have caused resize, need to update the pointer to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->hasDataHash()))
        {
            QNDEBUG(QStringLiteral("One of added resources has no data hash: ") << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(QStringLiteral("This resource has no data body as well, just skipping it"));
                continue;
            }

            QByteArray hash = QCryptographicHash::hash(pResource->dataBody(), QCryptographicHash::Md5);
            m_addedResources[i].setDataHash(hash);
            // This might have caused resize, need to update the pointer to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->hasDataSize()))
        {
            QNDEBUG(QStringLiteral("One of added resources has no data size: ") << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(QStringLiteral("This resource has no data body as well, just skipping it"));
                continue;
            }

            m_addedResources[i].setDataSize(m_addedResources[i].dataBody().size());
            // This might have caused resize, need to update the pointer to the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.addResourceToNote(*pResource);

        if (Q_LIKELY(m_resourceFileStoragePaths.size() > i))
        {
            m_resourceFileStoragePathsByResourceLocalUid[pResource->localUid()] = m_resourceFileStoragePaths[i];
            m_resourceInfo.cacheResourceInfo(pResource->dataHash(), pResource->displayName(),
                                             humanReadableSize(static_cast<quint64>(pResource->dataSize())),
                                             m_resourceFileStoragePaths[i]);
        }
        else
        {
            QNWARNING(QStringLiteral("Can't restore the resource file storage path for one of resources: the number of "
                                     "resource file storage path is less than or equal to the index: paths = ")
                      << m_resourceFileStoragePaths.join(QStringLiteral(", ")) << QStringLiteral("; resource: ") << pResource);
        }

    }

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("htmlInsertionManager.redo();"), m_callback);
}

} // namespace quentier
