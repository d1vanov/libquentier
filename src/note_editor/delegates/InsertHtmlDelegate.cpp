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
    m_cleanedUpHtml()
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

    QSet<QString> imgTagsSources;

    QXmlStreamReader reader(m_cleanedUpHtml);

    QString secondRoundCleanedUpHtml;
    QXmlStreamWriter writer(&secondRoundCleanedUpHtml);
    writer.setAutoFormatting(true);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();
    writer.writeDTD(QStringLiteral("<!DOCTYPE en-note SYSTEM \"http://xml.evernote.com/pub/enml2.dtd\">"));

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
            if (lastElementName == QStringLiteral("img"))
            {
                QXmlStreamAttributes attributes = reader.attributes();
                if (Q_UNLIKELY(!attributes.hasAttribute(QStringLiteral("src")))) {
                    QNDEBUG(QStringLiteral("Detected an img tag without src attribute, will skip this tag"));
                    continue;
                }

                QString urlString = attributes.value("src").toString();
                QUrl url(urlString);
                if (Q_UNLIKELY(!url.isValid())) {
                    QNDEBUG(QStringLiteral("Can't convert the img tag's src to a valid URL, will skip this tag"));
                    continue;
                }

                Q_UNUSED(imgTagsSources.insert(urlString))
            }
            // TODO: check "a" tag and its "href" attribute

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

    // TODO: implement further: see if imgTagSources is empty, if so, report success right away, otherwise continue
    // with downloading the images by these src's
}

} // namespace quentier
