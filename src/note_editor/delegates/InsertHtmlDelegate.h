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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"

#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>
#include <quentier/types/Resource.h>

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSet>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Account)
QT_FORWARD_DECLARE_CLASS(ENMLConverter)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)
QT_FORWARD_DECLARE_CLASS(ResourceDataInTemporaryFileStorageManager)
QT_FORWARD_DECLARE_CLASS(ResourceInfo)

class Q_DECL_HIDDEN InsertHtmlDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit InsertHtmlDelegate(
        const QString & inputHtml, NoteEditorPrivate & noteEditor,
        ENMLConverter & enmlConverter,
        ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
        ResourceInfo & resourceInfo, QObject * parent = nullptr);

    void start();

Q_SIGNALS:
    void finished(
        QList<Resource> addedResources, QStringList resourceFileStoragePaths);

    void notifyError(ErrorString error);

    // private signals:
    void saveResourceDataToTemporaryFile(
        QString noteLocalUid, QString resourceLocalUid, QByteArray data,
        QByteArray dataHash, QUuid requestId, bool isImage);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);
    void onImageDataDownloadFinished(QNetworkReply * pReply);

    void onResourceDataSavedToTemporaryFile(
        QUuid requestId, QByteArray dataHash, ErrorString errorDescription);

    void onHtmlInserted(const QVariant & responseData);

private:
    void doStart();
    void checkImageResourcesReady();
    bool addResource(const QByteArray & resourceData, const QUrl & url);

    bool adjustImgTagsInHtml();
    void insertHtmlIntoEditor();

    void removeAddedResourcesFromNote();

private:
    using JsCallback = JsResultCallbackFunctor<InsertHtmlDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    ENMLConverter & m_enmlConverter;

    ResourceDataInTemporaryFileStorageManager *
        m_pResourceDataInTemporaryFileStorageManager;
    QHash<QString, QString> & m_resourceFileStoragePathsByResourceLocalUid;
    ResourceInfo & m_resourceInfo;

    QString m_inputHtml;
    QString m_cleanedUpHtml;

    QSet<QUrl> m_imageUrls;
    QSet<QUrl> m_pendingImageUrls;
    QSet<QUrl> m_failingImageUrls;

    QHash<QUuid, Resource> m_resourceBySaveDataToTemporaryFileRequestId;
    QHash<QString, QUrl> m_sourceUrlByResourceLocalUid;
    QHash<QUrl, QUrl> m_urlToRedirectUrl;

    struct Q_DECL_HIDDEN ImgData
    {
        Resource m_resource;
        QString m_resourceFileStoragePath;
    };

    QHash<QUrl, ImgData> m_imgDataBySourceUrl;

    QNetworkAccessManager m_networkAccessManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
