#include "InsertHtmlDelegate.h"
#include "../NoteEditor_p.h"
#include <quentier/enml/ENMLConverter.h>
#include <quentier/types/Account.h>
#include <quentier/note_editor/ResourceFileStorageManager.h>
#include <quentier/utility/FileIOThreadWorker.h>
#include <quentier/logging/QuentierLogger.h>
#include <QXmlStreamReader>
#include <QSet>
#include <QUrl>

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
    m_inputHtml(inputHtml),
    m_cleanedUpHtml(),
    m_failingImageUrls(),
    m_addedResources(),
    m_networkAccessManager()
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

    if (Q_UNLIKELY(m_inputHtml.isEmpty())) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP("", "Can't insert HTML: the input html is empty"));
        QNWARNING(errorDescription);
        emit notifyError(errorDescription);
        return;
    }

    m_cleanedUpHtml.resize(0);
    ErrorString errorDescription;
    bool res = m_enmlConverter.cleanupExternalHtml(m_inputHtml, m_cleanedUpHtml, errorDescription);
    if (!res) {
        emit notifyError(errorDescription);
        return;
    }

    // NOTE: will exploit the fact that the cleaned up HTML is a valid XML

    m_imageUrls.clear();

    QXmlStreamReader reader(m_cleanedUpHtml);

    QString secondRoundCleanedUpHtml;
    QXmlStreamWriter writer(&secondRoundCleanedUpHtml);
    writer.setAutoFormatting(true);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();
    writer.writeDTD(QStringLiteral("<!DOCTYPE en-note SYSTEM \"http://xml.evernote.com/pub/enml2.dtd\">"));

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

            if (lastElementName == QStringLiteral("img"))
            {
                if (Q_UNLIKELY(!lastElementAttributes.hasAttribute(QStringLiteral("src")))) {
                    QNDEBUG(QStringLiteral("Detected an img tag without src attribute, will skip this tag"));
                    continue;
                }

                QString urlString = lastElementAttributes.value("src").toString();
                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG(QStringLiteral("Can't convert the img tag's src to a valid URL, will skip this tag; url = ")
                            << urlString);
                    continue;
                }

                Q_UNUSED(m_imageUrls.insert(url))
            }
            else if (lastElementName == QStringLiteral("a"))
            {
                if (Q_UNLIKELY(!lastElementAttributes.hasAttribute(QStringLiteral("href")))) {
                    QNDEBUG(QStringLiteral("Detected an a tag without href attribute, will skip the tag itself "
                                           "but preserving its internal content"));
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }

                QString urlString = lastElementAttributes.value("href").toString();
                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG(QStringLiteral("Can't convert the a tag's href to a valid URL, will skip this tag; url = ")
                            << urlString);
                    ++skippedElementWithPreservedContentsNestingCounter;
                    continue;
                }
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;
            QNTRACE(QStringLiteral("Wrote element: name = ") << lastElementName << QStringLiteral(" and its attributes"));
        }

        if ((writeElementCounter > 0) && reader.isCharacters())
        {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE(QStringLiteral("Wrote CDATA: ") << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE(QStringLiteral("Wrote characters: ") << text);
            }
        }

        if (reader.isEndElement())
        {
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
        ErrorString errorDescription(QT_TRANSLATE_NOOP("", "Can't insert HTML: unable to analyze the HTML"));
        errorDescription.details() = reader.errorString();
        QNWARNING(QStringLiteral("Error reading html: ") << errorDescription
                  << QStringLiteral(", HTML: ") << m_cleanedUpHtml);
        emit notifyError(errorDescription);
        return;
    }

    if (m_imageUrls.isEmpty()) {
        QNDEBUG(QStringLiteral("Found no images within the input HTML, thus don't need to download them"));
        // TODO: actually insert the cleaned up HTML into the note editor + set
        // up the callback to capture the result of this
        return;
    }

    // TODO: set up the proxy settings for the network access manager, get the
    // proxy settings from the application settings

    QObject::connect(&m_networkAccessManager, QNSIGNAL(QNetworkAccessManager,finished,QNetworkReply*),
                     this, QNSLOT(InsertHtmlDelegate,onImageDataDownloadFinished,QNetworkReply*));

    for(auto it = m_imageUrls.constBegin(), end = m_imageUrls.constEnd(); it != end; ++it) {
        const QUrl & url = *it;
        QNetworkRequest request(url);
        m_networkAccessManager.get(request);
        QNTRACE(QStringLiteral("Issued get request for url " ) << url);
    }
}

void InsertHtmlDelegate::onImageDataDownloadFinished(QNetworkReply * pReply)
{
    QNDEBUG(QStringLiteral("InsertHtmlDelegate::onImageDataDownloadFinished: url = ") << (pReply ? pReply->url().toString() : QStringLiteral("<null>")));

    if (Q_UNLIKELY(!pReply)) {
        QNWARNING(QStringLiteral("Received null QNetworkReply while trying to download the image from the pasted HTML"));
        return;
    }

    // TODO: implement further
}

} // namespace quentier
