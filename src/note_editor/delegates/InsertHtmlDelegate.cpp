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

#include "InsertHtmlDelegate.h"

#include "../NoteEditorPage.h"
#include "../NoteEditor_p.h"
#include "../ResourceDataInTemporaryFileStorageManager.h"

#include <quentier/enml/ENMLConverter.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Account.h>
#include <quentier/types/Note.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/Size.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSet>
#include <QTemporaryFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <limits>

namespace quentier {

InsertHtmlDelegate::InsertHtmlDelegate(
    const QString & inputHtml, NoteEditorPrivate & noteEditor,
    ENMLConverter & enmlConverter,
    ResourceDataInTemporaryFileStorageManager *
        pResourceDataInTemporaryFileStorageManager,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
    ResourceInfo & resourceInfo, QObject * parent) :
    QObject(parent),
    m_noteEditor(noteEditor), m_enmlConverter(enmlConverter),
    m_pResourceDataInTemporaryFileStorageManager(
        pResourceDataInTemporaryFileStorageManager),
    m_resourceFileStoragePathsByResourceLocalUid(
        resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo), m_inputHtml(inputHtml)
{}

void InsertHtmlDelegate::start()
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &InsertHtmlDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void InsertHtmlDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "InsertHtmlDelegate"
            << "::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &InsertHtmlDelegate::onOriginalPageConvertedToNote);

    doStart();
}

void InsertHtmlDelegate::onResourceDataSavedToTemporaryFile(
    QUuid requestId, QByteArray dataHash, ErrorString errorDescription)
{
    auto it = m_resourceBySaveDataToTemporaryFileRequestId.find(requestId);
    if (it == m_resourceBySaveDataToTemporaryFileRequestId.end()) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "InsertHtmlDelegate"
            << "::onResourceDataSavedToTemporaryFile: request id = "
            << requestId << ", data hash = " << dataHash.toHex()
            << ", error description: " << errorDescription);

    Resource resource = it.value();
    Q_UNUSED(m_resourceBySaveDataToTemporaryFileRequestId.erase(it))

    if (Q_UNLIKELY(!errorDescription.isEmpty())) {
        QNWARNING(
            "note_editor:delegate",
            "Failed to save the resource to "
                << "a temporary file: " << errorDescription);

        auto urlIt = m_sourceUrlByResourceLocalUid.find(resource.localUid());
        if (urlIt != m_sourceUrlByResourceLocalUid.end()) {
            const QUrl & url = urlIt.value();
            Q_UNUSED(m_failingImageUrls.insert(url))
            Q_UNUSED(m_sourceUrlByResourceLocalUid.erase(urlIt))
        }

        m_noteEditor.removeResourceFromNote(resource);
        checkImageResourcesReady();
        return;
    }

    if (!resource.hasDataHash()) {
        resource.setDataHash(dataHash);
        m_noteEditor.replaceResourceInNote(resource);
    }

    auto urlIt = m_sourceUrlByResourceLocalUid.find(resource.localUid());
    if (urlIt != m_sourceUrlByResourceLocalUid.end()) {
        const QUrl & url = urlIt.value();
        ImgData & imgData = m_imgDataBySourceUrl[url];
        imgData.m_resource = resource;

        Note * pNote = m_noteEditor.notePtr();
        if (Q_UNLIKELY(!pNote)) {
            errorDescription.setBase(
                QT_TR_NOOP("Internal error: can't insert HTML containing "
                           "images: no note is set to the editor"));
            QNWARNING("note_editor:delegate", errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QString fileStoragePath = ResourceDataInTemporaryFileStorageManager::
                                      imageResourceFileStorageFolderPath() +
            QStringLiteral("/") + pNote->localUid() + QStringLiteral("/") +
            resource.localUid() + QStringLiteral(".dat");

        imgData.m_resourceFileStoragePath = fileStoragePath;

        Q_UNUSED(m_sourceUrlByResourceLocalUid.erase(urlIt))
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: can't insert HTML containing "
                       "images: source URL was not found for "
                       "resource local uid"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    checkImageResourcesReady();
}

void InsertHtmlDelegate::onHtmlInserted(const QVariant & responseData)
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::onHtmlInserted");

    auto resultMap = responseData.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        removeAddedResourcesFromNote();

        ErrorString error(
            QT_TR_NOOP("Internal error: can't parse the result of "
                       "html insertion from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        removeAddedResourcesFromNote();

        ErrorString error;
        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Internal error: can't parse the error of "
                           "html insertion from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Internal error: can't insert html into "
                           "the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    int numResources = m_imgDataBySourceUrl.size();

    QList<Resource> resources;
    resources.reserve(numResources);

    QStringList resourceFileStoragePaths;
    resourceFileStoragePaths.reserve(numResources);

    for (auto it: qevercloud::toRange(m_imgDataBySourceUrl)) {
        ImgData & imgData = it.value();
        Resource & resource = imgData.m_resource;

        if (Q_UNLIKELY(!resource.hasDataHash())) {
            QNDEBUG(
                "note_editor:delegate",
                "One of added resources has no "
                    << "data hash");

            if (Q_UNLIKELY(!resource.hasDataBody())) {
                QNDEBUG(
                    "note_editor:delegate",
                    "This resource has no data "
                        << "body as well, will just skip it");
                continue;
            }

            QByteArray dataHash = QCryptographicHash::hash(
                resource.dataBody(), QCryptographicHash::Md5);

            resource.setDataHash(dataHash);
        }

        if (Q_UNLIKELY(!resource.hasDataSize())) {
            QNDEBUG(
                "note_editor:delegate",
                "One of added resources has no "
                    << "data size");

            if (Q_UNLIKELY(!resource.hasDataBody())) {
                QNDEBUG(
                    "note_editor:delegate",
                    "This resource has no data "
                        << "body as well, will just skip it");
                continue;
            }

            int dataSize = resource.dataBody().size();
            resource.setDataSize(dataSize);
        }

        m_resourceFileStoragePathsByResourceLocalUid[resource.localUid()] =
            imgData.m_resourceFileStoragePath;

        QSize resourceImageSize;
        if (resource.hasHeight() && resource.hasWidth()) {
            resourceImageSize.setHeight(resource.height());
            resourceImageSize.setWidth(resource.width());
        }

        m_resourceInfo.cacheResourceInfo(
            resource.dataHash(), resource.displayName(),
            humanReadableSize(static_cast<quint64>(resource.dataSize())),
            imgData.m_resourceFileStoragePath, resourceImageSize);

        resources << resource;
        resourceFileStoragePaths << imgData.m_resourceFileStoragePath;
    }

    QNDEBUG(
        "note_editor:delegate",
        "Finished the html insertion, number of "
            << "added image resources: " << resources.size());

    Q_EMIT finished(resources, resourceFileStoragePaths);
}

void InsertHtmlDelegate::doStart()
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::doStart");

    if (Q_UNLIKELY(m_inputHtml.isEmpty())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't insert HTML: the input html is empty"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_cleanedUpHtml.resize(0);
    ErrorString errorDescription;
    bool res = m_enmlConverter.cleanupExternalHtml(
        m_inputHtml, m_cleanedUpHtml, errorDescription);

    if (!res) {
        Q_EMIT notifyError(errorDescription);
        return;
    }

    // NOTE: will exploit the fact that the cleaned up HTML is a valid XML

    m_imageUrls.clear();

    QString supplementedHtml = QStringLiteral("<html><body>");
    supplementedHtml += m_cleanedUpHtml;
    supplementedHtml += QStringLiteral("</body></html>");

    QXmlStreamReader reader(supplementedHtml);

    QString secondRoundCleanedUpHtml;
    QXmlStreamWriter writer(&secondRoundCleanedUpHtml);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");

    int writeElementCounter = 0;
    size_t skippedElementWithPreservedContentsNestingCounter = 0;

    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            QNTRACE(
                "note_editor:delegate", "Start element: " << lastElementName);

            if ((lastElementName == QStringLiteral("title")) ||
                (lastElementName == QStringLiteral("head")))
            {
                lastElementName = QStringLiteral("div");
            }

            if ((lastElementName == QStringLiteral("html")) ||
                (lastElementName == QStringLiteral("body")))
            {
                continue;
            }
            else if (lastElementName == QStringLiteral("img")) {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("src")))
                {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Detected 'img' tag "
                            << "without src attribute, will skip this tag");
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("src"))
                        .toString();

                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Can't convert the 'img' tag's src to a valid URL, "
                            << "will skip this tag; url = " << urlString);
                    continue;
                }

                if (url.scheme().startsWith(QStringLiteral("http"))) {
                    Q_UNUSED(m_imageUrls.insert(url))
                }
            }
            else if (lastElementName == QStringLiteral("a")) {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("href")))
                {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Detected 'a' tag "
                            << "without href attribute, will skip the tag "
                               "itself "
                            << "but preserving its internal content");
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("href"))
                        .toString();

                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Can't convert the 'a'"
                            << "tag's href to a valid URL, will skip this tag; "
                            << "url = " << urlString);
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;

            QNTRACE(
                "note_editor:delegate",
                "Wrote element: name = " << lastElementName
                                         << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("note_editor:delegate", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("note_editor:delegate", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            QNTRACE("note_editor:delegate", "End element");

            if (writeElementCounter <= 0) {
                continue;
            }

            if (skippedElementWithPreservedContentsNestingCounter) {
                --skippedElementWithPreservedContentsNestingCounter;
                continue;
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't insert HTML: parsing failed"));
        errorDescription.details() = reader.errorString();

        QNWARNING(
            "note_editor:delegate",
            "Error reading html: " << errorDescription
                                   << ", HTML: " << m_cleanedUpHtml);

        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_cleanedUpHtml = secondRoundCleanedUpHtml;
    QNTRACE(
        "note_editor:delegate",
        "HTML after cleaning up bad img and a tags: " << m_cleanedUpHtml);

    if (m_imageUrls.isEmpty()) {
        QNDEBUG(
            "note_editor:delegate",
            "Found no images within the input "
                << "HTML, thus don't need to download them");
        insertHtmlIntoEditor();
        return;
    }

    // NOTE: will be using the application-wide proxy settings for image
    // downloading

    QObject::connect(
        &m_networkAccessManager, &QNetworkAccessManager::finished, this,
        &InsertHtmlDelegate::onImageDataDownloadFinished);

    m_pendingImageUrls = m_imageUrls;

    for (const auto & url: qAsConst(m_imageUrls)) {
        QNetworkRequest request(url);
        Q_UNUSED(m_networkAccessManager.get(request))
        QNTRACE("note_editor:delegate", "Issued get request for url " << url);
    }
}

void InsertHtmlDelegate::onImageDataDownloadFinished(QNetworkReply * pReply)
{
    QNDEBUG(
        "note_editor:delegate",
        "InsertHtmlDelegate"
            << "::onImageDataDownloadFinished: url = "
            << (pReply ? pReply->url().toString() : QStringLiteral("<null>")));

    if (Q_UNLIKELY(!pReply)) {
        QNWARNING(
            "note_editor:delegate",
            "Received null QNetworkReply while "
                << "trying to download the image from the pasted HTML");
        checkImageResourcesReady();
        return;
    }

    // Check for redirection
    QVariant redirectionTarget =
        pReply->attribute(QNetworkRequest::RedirectionTargetAttribute);

    if (!redirectionTarget.isNull()) {
        auto it = m_pendingImageUrls.find(pReply->url());
        if (it != m_pendingImageUrls.end()) {
            Q_UNUSED(m_pendingImageUrls.erase(it))
        }

        QUrl redirectUrl = pReply->url().resolved(redirectionTarget.toUrl());
        Q_UNUSED(m_pendingImageUrls.insert(redirectUrl))
        m_urlToRedirectUrl[pReply->url()] = redirectUrl;

        QNetworkRequest request(redirectUrl);
        Q_UNUSED(m_networkAccessManager.get(request))
        QNTRACE(
            "note_editor:delegate",
            "Issued get request for redirect url: " << redirectUrl);

        pReply->deleteLater();
        return;
    }

    QUrl url = pReply->url();
    Q_UNUSED(m_pendingImageUrls.remove(url))

    QNetworkReply::NetworkError error = pReply->error();
    if (error != QNetworkReply::NoError) {
        QNWARNING(
            "note_editor:delegate",
            "Detected error when attempting to "
                << "download the image from pasted HTML: "
                << pReply->errorString() << ", error code = " << error);

        checkImageResourcesReady();
        pReply->deleteLater();
        return;
    }

    QByteArray downloadedData = pReply->readAll();
    pReply->deleteLater();

    QImage image;
    bool res = image.loadFromData(downloadedData);
    if (Q_UNLIKELY(!res)) {
        QNDEBUG(
            "note_editor:delegate",
            "Wasn't able to load the image from "
                << "the downloaded data without format specification");

        QString format;
        QString urlString = url.toString();

        int dotIndex =
            urlString.lastIndexOf(QStringLiteral("."), -1, Qt::CaseInsensitive);

        if (dotIndex >= 0) {
            format =
                urlString.mid(dotIndex + 1, urlString.size() - dotIndex - 1);
            QNTRACE(
                "note_editor:delegate",
                "Trying to load the image with "
                    << "format " << format);

            res = image.loadFromData(
                downloadedData, format.toUpper().toLocal8Bit().constData());
        }
        else {
            QNDEBUG(
                "note_editor:delegate",
                "Can't find the last dot within "
                    << "the url, can't deduce the image format; url = "
                    << urlString);
        }

        if (!res) {
            QNTRACE(
                "note_editor:delegate",
                "Still can't load the image from "
                    << "the downloaded data, trying to write it to the "
                       "temporary "
                    << "file first and load from there");

            QTemporaryFile file;
            if (file.open()) {
                Q_UNUSED(file.write(downloadedData));
                file.flush();

                QNTRACE(
                    "note_editor:delegate",
                    "Wrote the downloaded data "
                        << "into the temporary file: " << file.fileName());

                res = image.load(file.fileName());
                if (!res) {
                    QNTRACE(
                        "note_editor:delegate",
                        "Could not load the image "
                            << "from temporary file without format "
                               "specification");
                }

                if (!res && !format.isEmpty()) {
                    res = image.load(
                        file.fileName(),
                        format.toUpper().toLocal8Bit().constData());

                    if (!res) {
                        QNTRACE(
                            "note_editor:delegate",
                            "Could not load "
                                << "the image from temporary file with the "
                                   "format "
                                << "specification too: " << format);
                    }
                }
            }
        }

        if (!res) {
            Q_UNUSED(m_failingImageUrls.insert(url))
            checkImageResourcesReady();
            return;
        }
    }

    QNDEBUG(
        "note_editor:delegate",
        "Successfully loaded the image from "
            << "the downloaded data");

    QByteArray pngImageData;
    QBuffer buffer(&pngImageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));

    res = image.save(&buffer, "PNG");
    if (Q_UNLIKELY(!res)) {
        QNDEBUG(
            "note_editor:delegate",
            "Wasn't able to save the downloaded "
                << "image to PNG format byte array");

        Q_UNUSED(m_failingImageUrls.insert(url))
        checkImageResourcesReady();
        return;
    }

    buffer.close();

    res = addResource(pngImageData, url);
    if (Q_UNLIKELY(!res)) {
        QNDEBUG(
            "note_editor:delegate",
            "Wasn't able to add the image to note "
                << "as a resource");
        Q_UNUSED(m_failingImageUrls.insert(url))
        checkImageResourcesReady();
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "Successfully added the image to note as "
            << "a resource");

    checkImageResourcesReady();
}

void InsertHtmlDelegate::checkImageResourcesReady()
{
    QNDEBUG(
        "note_editor:delegate",
        "InsertHtmlDelegate"
            << "::checkImageResourcesReady");

    if (!m_pendingImageUrls.isEmpty()) {
        QNDEBUG(
            "note_editor:delegate",
            "Still pending the download of "
                << QString::number(m_pendingImageUrls.size()) << " images");
        return;
    }

    if (!m_resourceBySaveDataToTemporaryFileRequestId.isEmpty()) {
        QNDEBUG(
            "note_editor:delegate",
            "Still pending saving of "
                << QString::number(
                       m_resourceBySaveDataToTemporaryFileRequestId.size())
                << " images");
        return;
    }

    bool res = adjustImgTagsInHtml();
    if (!res) {
        return;
    }

    insertHtmlIntoEditor();
}

bool InsertHtmlDelegate::adjustImgTagsInHtml()
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::adjustImgTagsInHtml");

    QString supplementedHtml = QStringLiteral("<html><body>");
    supplementedHtml += m_cleanedUpHtml;
    supplementedHtml += QStringLiteral("</body></html>");

    QXmlStreamReader reader(supplementedHtml);

    QString htmlWithAlteredImgTags;
    QXmlStreamWriter writer(&htmlWithAlteredImgTags);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");

    int writeElementCounter = 0;
    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            QNTRACE(
                "note_editor:delegate", "Start element: " << lastElementName);

            if ((lastElementName == QStringLiteral("title")) ||
                (lastElementName == QStringLiteral("head")))
            {
                lastElementName = QStringLiteral("div");
            }

            if ((lastElementName == QStringLiteral("html")) ||
                (lastElementName == QStringLiteral("body")))
            {
                continue;
            }
            else if (lastElementName == QStringLiteral("img")) {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("src")))
                {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Detected 'img' tag "
                            << "without src attribute, will skip this img tag");
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("src"))
                        .toString();

                QUrl url(urlString);
                if (m_failingImageUrls.contains(url)) {
                    QNDEBUG(
                        "note_editor:delegate",
                        "The image url " << url
                                         << " was marked as a failing one, "
                                         << ", will skip this img tag");
                    continue;
                }

                auto it = m_imgDataBySourceUrl.find(url);
                if (Q_UNLIKELY(it == m_imgDataBySourceUrl.end())) {
                    QNDEBUG(
                        "note_editor:delegate",
                        "Can't find "
                            << "the replacement data for the image url " << url
                            << ", see if it's due to redirect url usage");

                    auto rit = m_urlToRedirectUrl.find(url);
                    if (rit == m_urlToRedirectUrl.end()) {
                        QNDEBUG(
                            "note_editor:delegate",
                            "Couldn't find "
                                << "the redirect url for url " << url
                                << ", will just skip this img tag");
                        continue;
                    }

                    const QUrl & redirectUrl = rit.value();
                    QNDEBUG(
                        "note_editor:delegate",
                        "Found redirect url for " << url << ": "
                                                  << redirectUrl);

                    it = m_imgDataBySourceUrl.find(redirectUrl);
                    if (it == m_imgDataBySourceUrl.end()) {
                        QNDEBUG(
                            "note_editor:delegate",
                            "Couldn't find "
                                << "the replacement data for the image's "
                                   "redirect "
                                << "url " << redirectUrl
                                << ", will just skip this img tag");
                        continue;
                    }
                }

                QNDEBUG(
                    "note_editor:delegate",
                    "Successfully found "
                        << "the replacement data for image url");

                const ImgData & imgData = it.value();
                ErrorString resourceHtmlComposingError;

                QString resourceHtml = ENMLConverter::resourceHtml(
                    imgData.m_resource, resourceHtmlComposingError);

                if (Q_UNLIKELY(resourceHtml.isEmpty())) {
                    removeAddedResourcesFromNote();
                    ErrorString errorDescription(
                        QT_TR_NOOP("Can't insert HTML: can't compose the HTML "
                                   "representation of a resource that replaced "
                                   "the external image link"));
                    QNWARNING(
                        "note_editor:delegate",
                        errorDescription << "; resource: "
                                         << imgData.m_resource);
                    Q_EMIT notifyError(errorDescription);
                    return false;
                }

                QString supplementedResourceHtml =
                    QStringLiteral("<html><body>");
                supplementedResourceHtml += resourceHtml;
                supplementedResourceHtml += QStringLiteral("</body></html>");

                QXmlStreamReader resourceHtmlReader(supplementedResourceHtml);
                QXmlStreamAttributes resourceAttributes;
                while (!resourceHtmlReader.atEnd()) {
                    Q_UNUSED(resourceHtmlReader.readNext())

                    if (resourceHtmlReader.isStartDocument()) {
                        continue;
                    }

                    if (resourceHtmlReader.isDTD()) {
                        continue;
                    }

                    if (resourceHtmlReader.isEndDocument()) {
                        break;
                    }

                    if (resourceHtmlReader.isStartElement() &&
                        (resourceHtmlReader.name().toString() ==
                         QStringLiteral("img")))
                    {
                        resourceAttributes = resourceHtmlReader.attributes();
                    }
                }

                if (resourceHtmlReader.hasError()) {
                    ErrorString errorDescription(
                        QT_TR_NOOP("Can't insert HTML: failed to read "
                                   "the composed resource HTML"));
                    errorDescription.details() =
                        resourceHtmlReader.errorString();
                    QNWARNING(
                        "note_editor:delegate",
                        "Error reading html: " << errorDescription << ", HTML: "
                                               << m_cleanedUpHtml);
                    Q_EMIT notifyError(errorDescription);
                    return false;
                }

                resourceAttributes.append(
                    QStringLiteral("src"), imgData.m_resourceFileStoragePath);

                lastElementAttributes = resourceAttributes;
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;

            QNTRACE(
                "note_editor:delegate",
                "Wrote element: name = " << lastElementName
                                         << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("note_editor:delegate", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("note_editor:delegate", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            QNTRACE("note_editor:delegate", "End element");

            if (writeElementCounter <= 0) {
                continue;
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't insert HTML: failed to read "
                       "and recompose the cleaned up HTML"));
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "note_editor:delegate",
            "Error reading html: " << errorDescription
                                   << ", HTML: " << m_cleanedUpHtml);
        Q_EMIT notifyError(errorDescription);
        return false;
    }

    m_cleanedUpHtml = htmlWithAlteredImgTags;
    QNTRACE(
        "note_editor:delegate",
        "HTML after altering the img tags: " << m_cleanedUpHtml);
    return true;
}

void InsertHtmlDelegate::insertHtmlIntoEditor()
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::insertHtmlIntoEditor");

    auto * pPage = qobject_cast<NoteEditorPage *>(m_noteEditor.page());
    if (Q_UNLIKELY(!pPage)) {
        ErrorString error(QT_TR_NOOP("Can't insert HTML: no note editor page"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    ENMLConverter::escapeString(m_cleanedUpHtml, /* simplify = */ false);
    m_cleanedUpHtml = m_cleanedUpHtml.trimmed();
    QNTRACE("note_editor:delegate", "Trimmed HTML: " << m_cleanedUpHtml);
    m_cleanedUpHtml.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    QNTRACE(
        "note_editor:delegate",
        "Trimmed HTML with escaped newlines: " << m_cleanedUpHtml);

    pPage->executeJavaScript(
        QStringLiteral("htmlInsertionManager.insertHtml('") + m_cleanedUpHtml +
            QStringLiteral("');"),
        JsCallback(*this, &InsertHtmlDelegate::onHtmlInserted));
}

bool InsertHtmlDelegate::addResource(
    const QByteArray & resourceData, const QUrl & url)
{
    QNDEBUG("note_editor:delegate", "InsertHtmlDelegate::addResource");

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNWARNING(
            "note_editor:delegate",
            "Can't add image from inserted HTML: "
                << "no note is set to the editor");
        return false;
    }

    const Account * pAccount = m_noteEditor.accountPtr();

    bool noteHasLimits = pNote->hasNoteLimits();
    if (noteHasLimits) {
        QNTRACE(
            "note_editor:delegate",
            "Note has its own limits, will use "
                << "them to check the number of note resources");

        const qevercloud::NoteLimits & limits = pNote->noteLimits();
        if (limits.noteResourceCountMax.isSet() &&
            (limits.noteResourceCountMax.ref() == pNote->numResources()))
        {
            QNINFO(
                "note_editor:delegate",
                "Can't add image from inserted "
                    << "HTML: the note is already at max allowed number of "
                    << "attachments (judging by note limits)");
            return false;
        }
    }
    else if (pAccount) {
        QNTRACE(
            "note_editor:delegate",
            "Note has no limits of its own, will "
                << "use the account-wise limits to check the number "
                << "of note resources");

        int numNoteResources = pNote->numResources();
        ++numNoteResources;
        if (numNoteResources > pAccount->noteResourceCountMax()) {
            QNINFO(
                "note_editor:delegate",
                "Can't add image from inserted "
                    << "HTML: the note is already at max allowed number of "
                    << "attachments (judging by account limits)");
            return false;
        }
    }
    else {
        QNINFO(
            "note_editor:delegate",
            "No account when adding image from "
                << "inserted HTML to note, can't check the account-wise note "
                << "limits");
    }

    QMimeDatabase mimeDatabase;
    QMimeType mimeType = mimeDatabase.mimeTypeForData(resourceData);
    if (Q_UNLIKELY(!mimeType.isValid())) {
        QNDEBUG(
            "note_editor:delegate",
            "Could not deduce the resource data's "
                << "mime type from the data, fallback to image/png");
        mimeType = mimeDatabase.mimeTypeForName(QStringLiteral("image/png"));
    }

    QByteArray dataHash =
        QCryptographicHash::hash(resourceData, QCryptographicHash::Md5);

    Resource resource = m_noteEditor.attachResourceToNote(
        resourceData, dataHash, mimeType, QString(), url.toString());

    m_sourceUrlByResourceLocalUid[resource.localUid()] = url;

    QObject::connect(
        this, &InsertHtmlDelegate::saveResourceDataToTemporaryFile,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            onSaveResourceDataToTemporaryFileRequest);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            saveResourceDataToTemporaryFileCompleted,
        this, &InsertHtmlDelegate::onResourceDataSavedToTemporaryFile);

    QUuid requestId = QUuid::createUuid();
    m_resourceBySaveDataToTemporaryFileRequestId[requestId] = resource;

    QNTRACE(
        "note_editor:delegate",
        "Emitting the request to save the image "
            << "resource to a temporary file: request id = " << requestId
            << ", resource local uid = " << resource.localUid()
            << ", data hash = " << dataHash.toHex()
            << ", mime type name = " << mimeType.name());

    Q_EMIT saveResourceDataToTemporaryFile(
        pNote->localUid(), resource.localUid(), resourceData, dataHash,
        requestId,
        /* is image = */ true);

    return true;
}

void InsertHtmlDelegate::removeAddedResourcesFromNote()
{
    QNDEBUG(
        "note_editor:delegate",
        "InsertHtmlDelegate"
            << "::removeAddedResourcesFromNote");

    for (auto it: qevercloud::toRange(qAsConst(m_imgDataBySourceUrl))) {
        m_noteEditor.removeResourceFromNote(it.value().m_resource);
    }
}

} // namespace quentier
