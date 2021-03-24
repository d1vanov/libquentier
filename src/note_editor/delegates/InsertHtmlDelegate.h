/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSet>
#include <QUuid>

namespace quentier {

class Account;
class ENMLConverter;
class NoteEditorPrivate;
class ResourceDataInTemporaryFileStorageManager;
class ResourceInfo;

class Q_DECL_HIDDEN InsertHtmlDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit InsertHtmlDelegate(
        QString inputHtml, NoteEditorPrivate & noteEditor,
        ENMLConverter & enmlConverter,
        ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
        ResourceInfo & resourceInfo, QObject * parent = nullptr);

    void start();

Q_SIGNALS:
    void finished(
        QList<qevercloud::Resource> addedResources,
        QStringList resourceFileStoragePaths);

    void notifyError(ErrorString error);

    // private signals:
    void saveResourceDataToTemporaryFile(
        QString noteLocalId, QString resourceLocalId, QByteArray data,
        QByteArray dataHash, QUuid requestId, bool isImage);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);
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

    QHash<QString, QString> & m_resourceFileStoragePathsByResourceLocalId;
    ResourceInfo & m_resourceInfo;

    QString m_inputHtml;
    QString m_cleanedUpHtml;

    QSet<QUrl> m_imageUrls;
    QSet<QUrl> m_pendingImageUrls;
    QSet<QUrl> m_failingImageUrls;

    QHash<QUuid, qevercloud::Resource> m_resourceBySaveDataToTemporaryFileRequestId;
    QHash<QString, QUrl> m_sourceUrlByResourceLocalId;
    QHash<QUrl, QUrl> m_urlToRedirectUrl;

    struct Q_DECL_HIDDEN ImgData
    {
        qevercloud::Resource m_resource;
        QString m_resourceFileStoragePath;
    };

    QHash<QUrl, ImgData> m_imgDataBySourceUrl;

    QNetworkAccessManager m_networkAccessManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
