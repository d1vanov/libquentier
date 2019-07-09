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

#include "InsertHtmlDelegate.h"
#include "../NoteEditor_p.h"
#include "../NoteEditorPage.h"
#include "../ResourceDataInTemporaryFileStorageManager.h"

#include <quentier/enml/ENMLConverter.h>
#include <quentier/types/Account.h>
#include <quentier/types/Note.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/enml/ENMLConverter.h>

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSet>
#include <QUrl>
#include <QImage>
#include <QBuffer>
#include <QCryptographicHash>
#include <QMimeType>
#include <QMimeDatabase>
#include <QTemporaryFile>

#include <limits>

namespace quentier {

InsertHtmlDelegate::InsertHtmlDelegate(
        const QString & inputHtml,
        NoteEditorPrivate & noteEditor,
        ENMLConverter & enmlConverter,
        ResourceDataInTemporaryFileStorageManager * pResourceDataInTemporaryFileStorageManager,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
        ResourceInfo & resourceInfo,
        QObject * parent) :
    QObject(parent),
    m_noteEditor(noteEditor),
    m_enmlConverter(enmlConverter),
    m_pResourceDataInTemporaryFileStorageManager(pResourceDataInTemporaryFileStorageManager),
    m_resourceFileStoragePathsByResourceLocalUid(resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo),
    m_inputHtml(inputHtml),
    m_cleanedUpHtml(),
    m_imageUrls(),
    m_pendingImageUrls(),
    m_failingImageUrls(),
    m_resourceBySaveDataToTemporaryFileRequestId(),
    m_sourceUrlByResourceLocalUid(),
    m_urlToRedirectUrl(),
    m_imgDataBySourceUrl(),
    m_networkAccessManager()
{}

void InsertHtmlDelegate::start()
{
    QNDEBUG("InsertHtmlDelegate::start");

    if (m_noteEditor.isEditorPageModified())
    {
        QObject::connect(&m_noteEditor,
                         QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this,
                         QNSLOT(InsertHtmlDelegate,
                                onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else
    {
        doStart();
    }
}

void InsertHtmlDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG("InsertHtmlDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor,
                        QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this,
                        QNSLOT(InsertHtmlDelegate,
                               onOriginalPageConvertedToNote,Note));

    doStart();
}

void InsertHtmlDelegate::onResourceDataSavedToTemporaryFile(
    QUuid requestId, QByteArray dataHash, ErrorString errorDescription)
{
    auto it = m_resourceBySaveDataToTemporaryFileRequestId.find(requestId);
    if (it == m_resourceBySaveDataToTemporaryFileRequestId.end()) {
        return;
    }

    QNDEBUG("InsertHtmlDelegate::onResourceDataSavedToTemporaryFile: "
            << "request id = " << requestId
            << ", data hash = " << dataHash.toHex()
            << ", error description: " << errorDescription);

    Resource resource = it.value();
    Q_UNUSED(m_resourceBySaveDataToTemporaryFileRequestId.erase(it))

    if (Q_UNLIKELY(!errorDescription.isEmpty()))
    {
        QNWARNING("Failed to save the resource to a temporary file: "
                  << errorDescription);

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
    if (urlIt != m_sourceUrlByResourceLocalUid.end())
    {
        const QUrl & url = urlIt.value();
        ImgData & imgData = m_imgDataBySourceUrl[url];
        imgData.m_resource = resource;

        Note * pNote = m_noteEditor.notePtr();
        if (Q_UNLIKELY(!pNote))
        {
            errorDescription.setBase(QT_TR_NOOP("Internal error: can't insert HTML "
                                                "containing images: no note is "
                                                "set to the editor"));
            QNWARNING(errorDescription);
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QString fileStoragePath =
            ResourceDataInTemporaryFileStorageManager::imageResourceFileStorageFolderPath() +
            QStringLiteral("/") + pNote->localUid() + QStringLiteral("/") +
            resource.localUid() + QStringLiteral(".dat");
        imgData.m_resourceFileStoragePath = fileStoragePath;

        Q_UNUSED(m_sourceUrlByResourceLocalUid.erase(urlIt))
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: can't insert HTML containing "
                                     "images: source URL was not found for "
                                     "resource local uid"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    checkImageResourcesReady();
}

void InsertHtmlDelegate::onHtmlInserted(const QVariant & responseData)
{
    QNDEBUG("InsertHtmlDelegate::onHtmlInserted");

    QMap<QString,QVariant> resultMap = responseData.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end()))
    {
        removeAddedResourcesFromNote();

        ErrorString error(QT_TR_NOOP("Internal error: can't parse the result of "
                                     "html insertion from JavaScript"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res)
    {
        removeAddedResourcesFromNote();

        ErrorString error;
        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end()))
        {
            error.setBase(QT_TR_NOOP("Internal error: can't parse the error of "
                                     "html insertion from JavaScript"));
        }
        else
        {
            error.setBase(QT_TR_NOOP("Internal error: can't insert html into "
                                     "the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    int numResources = m_imgDataBySourceUrl.size();

    QList<Resource> resources;
    resources.reserve(numResources);

    QStringList resourceFileStoragePaths;
    resourceFileStoragePaths.reserve(numResources);

    for(auto it = m_imgDataBySourceUrl.begin(),
        end = m_imgDataBySourceUrl.end(); it != end; ++it)
    {
        ImgData & imgData = it.value();
        Resource & resource = imgData.m_resource;

        if (Q_UNLIKELY(!resource.hasDataHash()))
        {
            QNDEBUG("One of added resources has no data hash");

            if (Q_UNLIKELY(!resource.hasDataBody()))
            {
                QNDEBUG("This resource has no data body as well, "
                        "will just skip it");
                continue;
            }

            QByteArray dataHash = QCryptographicHash::hash(resource.dataBody(),
                                                           QCryptographicHash::Md5);
            resource.setDataHash(dataHash);
        }

        if (Q_UNLIKELY(!resource.hasDataSize()))
        {
            QNDEBUG("One of added resources has no data size");

            if (Q_UNLIKELY(!resource.hasDataBody()))
            {
                QNDEBUG("This resource has no data body as well, "
                        "will just skip it");
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

    QNDEBUG("Finished the html insertion, number of added image "
            << "resources: " << resources.size());
    Q_EMIT finished(resources, resourceFileStoragePaths);
}

void InsertHtmlDelegate::doStart()
{
    QNDEBUG("InsertHtmlDelegate::doStart");

    if (Q_UNLIKELY(m_inputHtml.isEmpty()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Can't insert HTML: the input "
                                                "html is empty"));
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_cleanedUpHtml.resize(0);
    ErrorString errorDescription;
    bool res = m_enmlConverter.cleanupExternalHtml(m_inputHtml, m_cleanedUpHtml,
                                                   errorDescription);
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

    while(!reader.atEnd())
    {
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

        if (reader.isStartElement())
        {
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            QNTRACE("Start element: " << lastElementName);

            if ( (lastElementName == QStringLiteral("title")) ||
                 (lastElementName == QStringLiteral("head")) )
            {
                lastElementName = QStringLiteral("div");
            }

            if ( (lastElementName == QStringLiteral("html")) ||
                 (lastElementName == QStringLiteral("body")) )
            {
                continue;
            }
            else if (lastElementName == QStringLiteral("img"))
            {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("src"))) {
                    QNDEBUG("Detected an img tag without src "
                            "attribute, will skip this tag");
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("src")).toString();
                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG("Can't convert the img tag's src to a "
                            << "valid URL, will skip this tag; url = "
                            << urlString);
                    continue;
                }

                Q_UNUSED(m_imageUrls.insert(url))
            }
            else if (lastElementName == QStringLiteral("a"))
            {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("href"))) {
                    QNDEBUG("Detected an a tag without href attribute, "
                            "will skip the tag itself but preserving "
                            "its internal content");
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("href")).toString();
                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG("Can't convert the a tag's href to "
                            << "a valid URL, will skip this tag; url = "
                            << urlString);
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;
            QNTRACE("Wrote element: name = " << lastElementName
                    << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters())
        {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("Wrote characters: " << text);
            }
        }

        if (reader.isEndElement())
        {
            QNTRACE("End element");

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

    if (reader.hasError())
    {
        ErrorString errorDescription(QT_TR_NOOP("Can't insert HTML: unable to "
                                                "analyze the HTML"));
        errorDescription.details() = reader.errorString();
        QNWARNING("Error reading html: " << errorDescription
                  << ", HTML: " << m_cleanedUpHtml);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_cleanedUpHtml = secondRoundCleanedUpHtml;
    QNTRACE("HTML after cleaning up bad img and a tags: "
            << m_cleanedUpHtml);

    if (m_imageUrls.isEmpty())
    {
        QNDEBUG("Found no images within the input HTML, thus "
                "don't need to download them");
        insertHtmlIntoEditor();
        return;
    }

    QNetworkAccessManager::NetworkAccessibility networkAccessibility =
        m_networkAccessManager.networkAccessible();
    if (networkAccessibility != QNetworkAccessManager::Accessible) {
        QNDEBUG("The network is not accessible, can't load any image");
        m_failingImageUrls = m_imageUrls;
        checkImageResourcesReady();
        return;
    }

    // NOTE: will be using the application-wide proxy settings for image downloading

    QObject::connect(&m_networkAccessManager,
                     QNSIGNAL(QNetworkAccessManager,finished,QNetworkReply*),
                     this,
                     QNSLOT(InsertHtmlDelegate,onImageDataDownloadFinished,
                            QNetworkReply*));

    m_pendingImageUrls = m_imageUrls;

    for(auto it = m_imageUrls.constBegin(),
        end = m_imageUrls.constEnd(); it != end; ++it)
    {
        const QUrl & url = *it;
        QNetworkRequest request(url);
        Q_UNUSED(m_networkAccessManager.get(request))
        QNTRACE("Issued get request for url " << url);
    }
}

void InsertHtmlDelegate::onImageDataDownloadFinished(QNetworkReply * pReply)
{
    QNDEBUG("InsertHtmlDelegate::onImageDataDownloadFinished: url = "
            << (pReply ? pReply->url().toString() : QStringLiteral("<null>")));

    if (Q_UNLIKELY(!pReply))
    {
        QNWARNING("Received null QNetworkReply while trying to "
                  "download the image from the pasted HTML");
        checkImageResourcesReady();
        return;
    }

    // Check for redirection
    QVariant redirectionTarget =
        pReply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redirectionTarget.isNull())
    {
        auto it = m_pendingImageUrls.find(pReply->url());
        if (it != m_pendingImageUrls.end()) {
            Q_UNUSED(m_pendingImageUrls.erase(it))
        }

        QUrl redirectUrl = pReply->url().resolved(redirectionTarget.toUrl());
        Q_UNUSED(m_pendingImageUrls.insert(redirectUrl))
        m_urlToRedirectUrl[pReply->url()] = redirectUrl;

        QNetworkRequest request(redirectUrl);
        Q_UNUSED(m_networkAccessManager.get(request))
        QNTRACE("Issued get request for redirect url: " << redirectUrl);

        pReply->deleteLater();
        return;
    }

    QUrl url = pReply->url();
    Q_UNUSED(m_pendingImageUrls.remove(url))

    QNetworkReply::NetworkError error = pReply->error();
    if (error != QNetworkReply::NoError)
    {
        QNWARNING("Detected error when attempting to download "
                  << "the image from pasted HTML: "
                  << pReply->errorString() << ", error code = " << error);
        checkImageResourcesReady();
        pReply->deleteLater();
        return;
    }

    QByteArray downloadedData = pReply->readAll();
    pReply->deleteLater();

    QImage image;
    bool res = image.loadFromData(downloadedData);
    if (Q_UNLIKELY(!res))
    {
        QNDEBUG("Wasn't able to load the image from the downloaded "
                "data without format specification");

        QString format;
        QString urlString = url.toString();
        int dotIndex = urlString.lastIndexOf(QStringLiteral("."), -1,
                                             Qt::CaseInsensitive);
        if (dotIndex >= 0)
        {
            format = urlString.mid(dotIndex + 1, urlString.size() - dotIndex - 1);
            QNTRACE("Trying to load the image with format " << format);
            res = image.loadFromData(downloadedData,
                                     format.toUpper().toLocal8Bit().constData());
        }
        else
        {
            QNDEBUG("Can't find the last dot within the url, "
                    << "can't deduce the image format; url = " << urlString);
        }

        if (!res)
        {
            QNTRACE("Still can't load the image from the downloaded "
                    "data, trying to write it to the temporary file "
                    "first and load from there");

            QTemporaryFile file;
            if (file.open())
            {
                Q_UNUSED(file.write(downloadedData));
                file.flush();
                QNTRACE("Wrote the downloaded data into the temporary file: "
                        << file.fileName());

                res = image.load(file.fileName());
                if (!res) {
                    QNTRACE("Could not load the image from temporary "
                            "file without format specification");
                }

                if (!res && !format.isEmpty())
                {
                    res = image.load(file.fileName(),
                                     format.toUpper().toLocal8Bit().constData());
                    if (!res) {
                        QNTRACE("Could not load the image from "
                                << "temporary file with the format "
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

    QNDEBUG("Successfully loaded the image from the downloaded data");

    QByteArray pngImageData;
    QBuffer buffer(&pngImageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));

    res = image.save(&buffer, "PNG");
    if (Q_UNLIKELY(!res))
    {
        QNDEBUG("Wasn't able to save the downloaded image to PNG "
                "format byte array");
        Q_UNUSED(m_failingImageUrls.insert(url))
        checkImageResourcesReady();
        return;
    }

    buffer.close();

    res = addResource(pngImageData, url);
    if (Q_UNLIKELY(!res)) {
        QNDEBUG("Wasn't able to add the image to note as a resource");
        Q_UNUSED(m_failingImageUrls.insert(url))
        checkImageResourcesReady();
        return;
    }

    QNDEBUG("Successfully added the image to note as a resource");
    checkImageResourcesReady();
}

void InsertHtmlDelegate::checkImageResourcesReady()
{
    QNDEBUG("InsertHtmlDelegate::checkImageResourcesReady");

    if (!m_pendingImageUrls.isEmpty()) {
        QNDEBUG("Still pending the download of "
                << QString::number(m_pendingImageUrls.size()) << " images");
        return;
    }

    if (!m_resourceBySaveDataToTemporaryFileRequestId.isEmpty()) {
        QNDEBUG("Still pending saving of "
                << QString::number(m_resourceBySaveDataToTemporaryFileRequestId.size())
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
    QNDEBUG("InsertHtmlDelegate::adjustImgTagsInHtml");

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

    while(!reader.atEnd())
    {
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

        if (reader.isStartElement())
        {
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            QNTRACE("Start element: " << lastElementName);

            if ( (lastElementName == QStringLiteral("title")) ||
                 (lastElementName == QStringLiteral("head")) )
            {
                lastElementName = QStringLiteral("div");
            }

            if ( (lastElementName == QStringLiteral("html")) ||
                 (lastElementName == QStringLiteral("body")) )
            {
                continue;
            }
            else if (lastElementName == QStringLiteral("img"))
            {
                if (!lastElementAttributes.hasAttribute(QStringLiteral("src"))) {
                    QNDEBUG("Detected an img tag without src "
                            "attribute, will skip this img tag");
                    continue;
                }

                QString urlString =
                    lastElementAttributes.value(QStringLiteral("src")).toString();
                QUrl url(urlString);
                if (m_failingImageUrls.contains(url)) {
                    QNDEBUG("The image url " << url
                            << " was marked as a failing one, "
                            << ", will skip this img tag");
                    continue;
                }

                auto it = m_imgDataBySourceUrl.find(url);
                if (Q_UNLIKELY(it == m_imgDataBySourceUrl.end()))
                {
                    QNDEBUG("Can't find the replacement data for "
                            << "the image url " << url
                            << ", see if it's due to redirect url usage");

                    auto rit = m_urlToRedirectUrl.find(url);
                    if (rit == m_urlToRedirectUrl.end()) {
                        QNDEBUG("Couldn't find the redirect url for url "
                                << url << ", will just skip this img tag");
                        continue;
                    }

                    const QUrl & redirectUrl = rit.value();
                    QNDEBUG("Found redirect url for url " << url
                            << ": " << redirectUrl);

                    it = m_imgDataBySourceUrl.find(redirectUrl);
                    if (it == m_imgDataBySourceUrl.end()) {
                        QNDEBUG("Couldn't find the replacement data "
                                << "for the image's redirect url "
                                << redirectUrl
                                << ", will just skip this img tag");
                        continue;
                    }
                }

                QNDEBUG("Successfully found the replacement data "
                        "for the image url");
                const ImgData & imgData = it.value();
                ErrorString resourceHtmlComposingError;
                QString resourceHtml =
                    ENMLConverter::resourceHtml(imgData.m_resource,
                                                resourceHtmlComposingError);
                if (Q_UNLIKELY(resourceHtml.isEmpty()))
                {
                    removeAddedResourcesFromNote();
                    ErrorString errorDescription(
                        QT_TR_NOOP("Can't insert HTML: can't compose the HTML "
                                   "representation of a resource that replaced "
                                   "the external image link"));
                    QNWARNING(errorDescription << "; resource: "
                              << imgData.m_resource);
                    Q_EMIT notifyError(errorDescription);
                    return false;
                }

                QString supplementedResourceHtml = QStringLiteral("<html><body>");
                supplementedResourceHtml += resourceHtml;
                supplementedResourceHtml += QStringLiteral("</body></html>");

                QXmlStreamReader resourceHtmlReader(supplementedResourceHtml);
                QXmlStreamAttributes resourceAttributes;
                while(!resourceHtmlReader.atEnd())
                {
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
                        (resourceHtmlReader.name().toString() == QStringLiteral("img")))
                    {
                        resourceAttributes = resourceHtmlReader.attributes();
                    }
                }

                if (resourceHtmlReader.hasError())
                {
                    ErrorString errorDescription(
                        QT_TR_NOOP("Can't insert HTML: failed to read the composed "
                                   "resource HTML"));
                    errorDescription.details() = resourceHtmlReader.errorString();
                    QNWARNING("Error reading html: " << errorDescription
                              << ", HTML: " << m_cleanedUpHtml);
                    Q_EMIT notifyError(errorDescription);
                    return false;
                }

                resourceAttributes.append(QStringLiteral("src"),
                                          imgData.m_resourceFileStoragePath);
                lastElementAttributes = resourceAttributes;
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;
            QNTRACE("Wrote element: name = " << lastElementName
                    << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters())
        {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("Wrote characters: " << text);
            }
        }

        if (reader.isEndElement())
        {
            QNTRACE("End element");

            if (writeElementCounter <= 0) {
                continue;
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError())
    {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't insert HTML: failed to read "
                       "and recompose the cleaned up HTML"));
        errorDescription.details() = reader.errorString();
        QNWARNING("Error reading html: " << errorDescription
                  << ", HTML: " << m_cleanedUpHtml);
        Q_EMIT notifyError(errorDescription);
        return false;
    }

    m_cleanedUpHtml = htmlWithAlteredImgTags;
    QNTRACE("HTML after altering the img tags: " << m_cleanedUpHtml);
    return true;
}

void InsertHtmlDelegate::insertHtmlIntoEditor()
{
    QNDEBUG("InsertHtmlDelegate::insertHtmlIntoEditor");

    NoteEditorPage * pPage = qobject_cast<NoteEditorPage*>(m_noteEditor.page());
    if (Q_UNLIKELY(!pPage)) {
        ErrorString error(QT_TR_NOOP("Can't insert HTML: no note editor page"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    ENMLConverter::escapeString(m_cleanedUpHtml, /* simplify = */ false);
    m_cleanedUpHtml = m_cleanedUpHtml.trimmed();
    QNTRACE("Trimmed HTML: " << m_cleanedUpHtml);
    m_cleanedUpHtml.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    QNTRACE("Trimmed HTML with escaped newlines: " << m_cleanedUpHtml);

    pPage->executeJavaScript(
        QStringLiteral("htmlInsertionManager.insertHtml('") +
        m_cleanedUpHtml + QStringLiteral("');"),
        JsCallback(*this, &InsertHtmlDelegate::onHtmlInserted));
}

bool InsertHtmlDelegate::addResource(const QByteArray & resourceData,
                                     const QUrl & url)
{
    QNDEBUG("InsertHtmlDelegate::addResource");

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNWARNING("Can't add image from inserted HTML: no note "
                  "is set to the editor");
        return false;
    }

    const Account * pAccount = m_noteEditor.accountPtr();

    bool noteHasLimits = pNote->hasNoteLimits();
    if (noteHasLimits)
    {
        QNTRACE("Note has its own limits, will use them to check "
                "the number of note resources");

        const qevercloud::NoteLimits & limits = pNote->noteLimits();
        if (limits.noteResourceCountMax.isSet() &&
            (limits.noteResourceCountMax.ref() == pNote->numResources()))
        {
            QNINFO("Can't add image from inserted HTML: the note "
                   "is already at max allowed number of attachments "
                   "(judging by note limits)");
            return false;
        }
    }
    else if (pAccount)
    {
        QNTRACE("Note has no limits of its own, will use "
                "the account-wise limits to check the number "
                "of note resources");

        int numNoteResources = pNote->numResources();
        ++numNoteResources;
        if (numNoteResources > pAccount->noteResourceCountMax()) {
            QNINFO("Can't add image from inserted HTML: the note "
                   "is already at max allowed number of attachments "
                   "(judging by account limits)");
            return false;
        }
    }
    else
    {
        QNINFO("No account when adding image from inserted HTML "
               "to note, can't check the account-wise note limits");
    }

    QMimeDatabase mimeDatabase;
    QMimeType mimeType = mimeDatabase.mimeTypeForData(resourceData);
    if (Q_UNLIKELY(!mimeType.isValid())) {
        QNDEBUG("Could not deduce the resource data's mime type "
                "from the data, fallback to image/png");
        mimeType = mimeDatabase.mimeTypeForName(QStringLiteral("image/png"));
    }

    QByteArray dataHash = QCryptographicHash::hash(resourceData,
                                                   QCryptographicHash::Md5);
    Resource resource = m_noteEditor.attachResourceToNote(resourceData, dataHash,
                                                          mimeType, QString(),
                                                          url.toString());

    m_sourceUrlByResourceLocalUid[resource.localUid()] = url;

    QObject::connect(this,
                     QNSIGNAL(InsertHtmlDelegate,saveResourceDataToTemporaryFile,
                              QString,QString,QByteArray,QByteArray,QUuid,bool),
                     m_pResourceDataInTemporaryFileStorageManager,
                     QNSLOT(ResourceDataInTemporaryFileStorageManager,
                            onSaveResourceDataToTemporaryFileRequest,
                            QString,QString,QByteArray,QByteArray,QUuid,bool));
    QObject::connect(m_pResourceDataInTemporaryFileStorageManager,
                     QNSIGNAL(ResourceDataInTemporaryFileStorageManager,
                              saveResourceDataToTemporaryFileCompleted,
                              QUuid,QByteArray,ErrorString),
                     this,
                     QNSLOT(InsertHtmlDelegate,onResourceDataSavedToTemporaryFile,
                            QUuid,QByteArray,ErrorString));

    QUuid requestId = QUuid::createUuid();
    m_resourceBySaveDataToTemporaryFileRequestId[requestId] = resource;

    QNTRACE("Emitting the request to save the image resource to "
            << "a temporary file: request id = " << requestId
            << ", resource local uid = " << resource.localUid()
            << ", data hash = " << dataHash.toHex()
            << ", mime type name = " << mimeType.name());
    Q_EMIT saveResourceDataToTemporaryFile(pNote->localUid(), resource.localUid(),
                                           resourceData, dataHash,
                                           requestId, /* is image = */ true);
    return true;
}

void InsertHtmlDelegate::removeAddedResourcesFromNote()
{
    QNDEBUG("InsertHtmlDelegate::removeAddedResourcesFromNote");

    for(auto iit = m_imgDataBySourceUrl.constBegin(),
        iend = m_imgDataBySourceUrl.constEnd(); iit != iend; ++iit)
    {
        m_noteEditor.removeResourceFromNote(iit.value().m_resource);
    }
}

} // namespace quentier
