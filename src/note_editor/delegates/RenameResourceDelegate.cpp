/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include "RenameResourceDelegate.h"
#include "../NoteEditor_p.h"
#include "../GenericResourceImageManager.h"
#include "../dialogs/RenameResourceDialog.h"

#include <quentier/logging/QuentierLogger.h>

#include <QScopedPointer>
#include <QBuffer>
#include <QImage>

namespace quentier {

#define GET_PAGE()                                                             \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditor.page());\
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP("RenameResourceDelegate",          \
                                            "Can't rename the attachment: "    \
                                            "no note editor page"));           \
        QNWARNING(error);                                                      \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }                                                                          \
// GET_PAGE

RenameResourceDelegate::RenameResourceDelegate(
        const Resource & resource, NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash,
        const bool performingUndo) :
    m_noteEditor(noteEditor),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_genericResourceImageFilePathsByResourceHash(genericResourceImageFilePathsByResourceHash),
    m_resource(resource),
    m_oldResourceName(resource.displayName()),
    m_newResourceName(),
    m_shouldGetResourceNameFromDialog(true),
    m_performingUndo(performingUndo),
    m_pNote(noteEditor.notePtr())
#ifdef QUENTIER_USE_QT_WEB_ENGINE
    ,
    m_genericResourceImageWriterRequestId()
#endif
{}

void RenameResourceDelegate::start()
{
    QNDEBUG("RenameResourceDelegate::start");

    if (m_noteEditor.isEditorPageModified())
    {
        QObject::connect(&m_noteEditor,
                         QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this,
                         QNSLOT(RenameResourceDelegate,
                                onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else
    {
        doStart();
    }
}

void RenameResourceDelegate::startWithPresetNames(
    const QString & oldResourceName, const QString & newResourceName)
{
    QNDEBUG("RenameResourceDelegate::startWithPresetNames: "
            << "old resource name = " << oldResourceName
            << ", new resource name = " << newResourceName);

    m_oldResourceName = oldResourceName;
    m_newResourceName = newResourceName;
    m_shouldGetResourceNameFromDialog = false;

    start();
}

void RenameResourceDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG("RenameResourceDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor,
                        QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this,
                        QNSLOT(RenameResourceDelegate,
                               onOriginalPageConvertedToNote,Note));

    doStart();
}

#define CHECK_NOTE_ACTUALITY()                                                 \
    if (m_noteEditor.notePtr() != m_pNote) {                                   \
        ErrorString error(                                                     \
            QT_TRANSLATE_NOOP("RenameResourceDelegate",                        \
                              "The note set to the note editor was changed "   \
                              "during the attachment renaming, the action was "\
                              "not completed"));                               \
        QNDEBUG(error);                                                        \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }                                                                          \
// CHECK_NOTE_ACTUALITY

void RenameResourceDelegate::doStart()
{
    QNDEBUG("RenameResourceDelegate::doStart");

    CHECK_NOTE_ACTUALITY()

    if (Q_UNLIKELY(!m_resource.hasDataHash()))
    {
        ErrorString error(QT_TR_NOOP("Can't rename the attachment: the data "
                                     "hash is missing"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    if (m_shouldGetResourceNameFromDialog)
    {
        raiseRenameResourceDialog();
    }
    else
    {
        m_resource.setDisplayName(m_newResourceName);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
        buildAndSaveGenericResourceImage();
#else
        Q_EMIT finished(m_oldResourceName, m_newResourceName,
                        m_resource, m_performingUndo);
#endif
    }
}

void RenameResourceDelegate::raiseRenameResourceDialog()
{
    QNDEBUG("RenameResourceDelegate::raiseRenameResourceDialog");

    QScopedPointer<RenameResourceDialog> pRenameResourceDialog(
        new RenameResourceDialog(m_oldResourceName, &m_noteEditor));
    pRenameResourceDialog->setWindowModality(Qt::WindowModal);
    QObject::connect(pRenameResourceDialog.data(),
                     QNSIGNAL(RenameResourceDialog,accepted,QString),
                     this,
                     QNSLOT(RenameResourceDelegate,
                            onRenameResourceDialogFinished,QString));
    QNTRACE("Will exec rename resource dialog now");
    int res = pRenameResourceDialog->exec();
    if (res == QDialog::Rejected) {
        QNTRACE("Cancelled renaming the resource");
        Q_EMIT cancelled();
    }
}

void RenameResourceDelegate::onRenameResourceDialogFinished(
    QString newResourceName)
{
    QNDEBUG("RenameResourceDelegate::onRenameResourceDialogFinished: "
            << "new resource name = " << newResourceName);

    if (newResourceName.isEmpty())
    {
        QNTRACE("New resource name is empty, treating it "
                "as cancellation");
        Q_EMIT cancelled();
        return;
    }

    if (newResourceName == m_oldResourceName)
    {
        QNTRACE("The new resource name is equal to the old one, "
                "treating it as cancellation");
        Q_EMIT cancelled();
        return;
    }

    m_newResourceName = newResourceName;
    m_resource.setDisplayName(m_newResourceName);
    m_noteEditor.replaceResourceInNote(m_resource);

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    buildAndSaveGenericResourceImage();
#else
    Q_EMIT finished(m_oldResourceName, m_newResourceName,
                    m_resource, m_performingUndo);
#endif
}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void RenameResourceDelegate::buildAndSaveGenericResourceImage()
{
    QNDEBUG("RenameResourceDelegate::buildAndSaveGenericResourceImage");

    CHECK_NOTE_ACTUALITY()

    QImage resourceImage = m_noteEditor.buildGenericResourceImage(m_resource);

    QByteArray imageData;
    QBuffer buffer(&imageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));
    resourceImage.save(&buffer, "PNG");

    m_genericResourceImageWriterRequestId = QUuid::createUuid();

    QNDEBUG("Emitting request to write generic resource image "
            << "for resource with local uid "
            << m_resource.localUid() << ", request id "
            << m_genericResourceImageWriterRequestId
            << ", note local uid = " << m_pNote->localUid());

    QObject::connect(this,
                     QNSIGNAL(RenameResourceDelegate,saveGenericResourceImageToFile,
                              QString,QString,QByteArray,QString,QByteArray,
                              QString,QUuid),
                     m_pGenericResourceImageManager,
                     QNSLOT(GenericResourceImageManager,
                            onGenericResourceImageWriteRequest,
                            QString,QString,QByteArray,QString,QByteArray,
                            QString,QUuid));
    QObject::connect(m_pGenericResourceImageManager,
                     QNSIGNAL(GenericResourceImageManager,
                              genericResourceImageWriteReply,
                              bool,QByteArray,QString,ErrorString,QUuid),
                     this,
                     QNSLOT(RenameResourceDelegate,
                            onGenericResourceImageWriterFinished,
                            bool,QByteArray,QString,ErrorString,QUuid));

    Q_EMIT saveGenericResourceImageToFile(m_pNote->localUid(), m_resource.localUid(),
                                          imageData, QStringLiteral("png"),
                                          m_resource.dataHash(), m_resource.displayName(),
                                          m_genericResourceImageWriterRequestId);
}

void RenameResourceDelegate::onGenericResourceImageWriterFinished(
    bool success, QByteArray resourceHash, QString filePath,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_genericResourceImageWriterRequestId) {
        return;
    }

    QNDEBUG("RenameResourceDelegate::"
            << "onGenericResourceImageWriterFinished: success = "
            << (success ? "true" : "false")
            << ", resource hash = " << resourceHash.toHex()
            << ", file path = " << filePath
            << ", error description = " << errorDescription
            << ", request id = " << requestId);

    QObject::disconnect(this,
                        QNSIGNAL(RenameResourceDelegate,
                                 saveGenericResourceImageToFile,
                                 QString,QString,QByteArray,QString,QByteArray,
                                 QString,QUuid),
                        m_pGenericResourceImageManager,
                        QNSLOT(GenericResourceImageManager,
                               onGenericResourceImageWriteRequest,
                               QString,QString,QByteArray,QString,QByteArray,
                               QString,QUuid));
    QObject::disconnect(m_pGenericResourceImageManager,
                        QNSIGNAL(GenericResourceImageManager,
                                 genericResourceImageWriteReply,
                                 bool,QByteArray,QString,QString,QUuid),
                        this,
                        QNSLOT(RenameResourceDelegate,
                               onGenericResourceImageWriterFinished,
                               bool,QByteArray,QString,QString,QUuid));

    if (Q_UNLIKELY(!success))
    {
        ErrorString error(QT_TR_NOOP("Can't rename generic resource: can't write "
                                     "generic resource image to file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    m_genericResourceImageFilePathsByResourceHash[resourceHash] = filePath;

    QString javascript = QStringLiteral("updateImageResourceSrc('") +
                         QString::fromLocal8Bit(resourceHash.toHex()) +
                         QStringLiteral("', '") + filePath +
                         QStringLiteral("', 0, 0);");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &RenameResourceDelegate::onGenericResourceImageUpdated));
}

void RenameResourceDelegate::onGenericResourceImageUpdated(const QVariant & data)
{
    QNDEBUG("RenameResourceDelegate::onGenericResourceImageUpdated");

    Q_UNUSED(data)

    Q_EMIT finished(m_oldResourceName, m_newResourceName,
                    m_resource, m_performingUndo);
}

#endif // QUENTIER_USE_QT_WEB_ENGINE

} // namespace quentier

