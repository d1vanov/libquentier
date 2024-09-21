/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/enml/Factory.h>
#include <quentier/enml/IConverter.h>
#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/enml/IHtmlData.h>
#include <quentier/logging/QuentierLogger.h>

#include <QFile>
#include <QXmlStreamReader>

#include <gtest/gtest.h>

#include <array>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::enml::tests {

namespace {

[[nodiscard]] Result<void, QString> compareEnml(
    const QString & original, const QString & processed)
{
    const QString originalSimplified = original.simplified();
    const QString processedSimplified = processed.simplified();

    QXmlStreamReader readerOriginal{originalSimplified};
    QXmlStreamReader readerProcessed{processedSimplified};

#define PRINT_WARNING(err)                                                     \
    QNWARNING(                                                                 \
        "tests:enml",                                                          \
        err << "\n\nContext in the original ENML: <" << readerOriginal.name()  \
            << ">: " << readerOriginal.readElementText()                       \
            << "\n\nContext in the processed ENML: <"                          \
            << readerProcessed.name()                                          \
            << ">: " << readerProcessed.readElementText()                      \
            << "\n\nFull simplified original ENML: " << originalSimplified     \
            << "\n\nFull simplified processed ENML: " << processedSimplified)

    while (!readerOriginal.atEnd() && !readerProcessed.atEnd()) {
        readerOriginal.readNext();
        readerProcessed.readNext();

        bool checkForEmptyCharacters = true;
        while (checkForEmptyCharacters) {
            if (readerOriginal.isCharacters()) {
                QString textOriginal = readerOriginal.readElementText();
                if (textOriginal.simplified().isEmpty()) {
                    Q_UNUSED(readerOriginal.readNext());
                    continue;
                }
            }

            checkForEmptyCharacters = false;
        }

        checkForEmptyCharacters = true;
        while (checkForEmptyCharacters) {
            if (readerProcessed.isCharacters()) {
                QString textProcessed = readerProcessed.readElementText();
                if (textProcessed.simplified().isEmpty()) {
                    Q_UNUSED(readerProcessed.readNext());
                    continue;
                }
            }

            checkForEmptyCharacters = false;
        }

        bool checkForEntityReference = true;
        while (checkForEntityReference) {
            if (readerOriginal.isEntityReference()) {
                Q_UNUSED(readerOriginal.readNext());
                continue;
            }

            checkForEntityReference = false;
        }

        if (readerOriginal.isStartDocument() &&
            !readerProcessed.isStartDocument())
        {
            QString error = QStringLiteral(
                "QXmlStreamReader of the original ENML is "
                "at the start of the document while the reader "
                "of the processed ENML is not");

            PRINT_WARNING(error);
            return Result<void, QString>{std::move(error)};
        }

        if (readerOriginal.isStartElement()) {
            if (!readerProcessed.isStartElement()) {
                QString error = QStringLiteral(
                    "QXmlStreamReader of the original ENML "
                    "is at the start of the element while "
                    "the reader of the processed ENML is not");

                PRINT_WARNING(
                    error
                    << "\n\nchecking the state of "
                       "processed ENML reader: isStartDocument: "
                    << (readerProcessed.isStartDocument() ? "true" : "false")
                    << ", isDTD: "
                    << (readerProcessed.isDTD() ? "true" : "false")
                    << ", isCDATA: "
                    << (readerProcessed.isCDATA() ? "true" : "false")
                    << ", isCharacters: "
                    << (readerProcessed.isCharacters() ? "true" : "false")
                    << ", isComment: "
                    << (readerProcessed.isComment() ? "true" : "false")
                    << ", isEndElement: "
                    << (readerProcessed.isEndElement() ? "true" : "false")
                    << ", isEndDocument: "
                    << (readerProcessed.isEndDocument() ? "true" : "false")
                    << ", isEntityReference: "
                    << (readerProcessed.isEntityReference() ? "true" : "false")
                    << ", isProcessingInstruction: "
                    << (readerProcessed.isProcessingInstruction() ? "true"
                                                                  : "false")
                    << ", isStandaloneDocument: "
                    << (readerProcessed.isStandaloneDocument() ? "true"
                                                               : "false")
                    << ", isStartDocument: "
                    << (readerProcessed.isStartDocument() ? "true" : "false")
                    << ", isWhitespace: "
                    << (readerProcessed.isWhitespace() ? "true" : "false"));

                return Result<void, QString>{std::move(error)};
            }

            const auto originalName = readerOriginal.name();
            const auto processedName = readerProcessed.name();
            if (originalName != processedName) {
                QString error = QStringLiteral(
                    "Found a tag in the original ENML which name doesn't match "
                    "the name of the corresponding element in the processed "
                    "ENML");

                PRINT_WARNING(error);
                return Result<void, QString>{std::move(error)};
            }

            const QXmlStreamAttributes originalAttributes =
                readerOriginal.attributes();

            const QXmlStreamAttributes processedAttributes =
                readerProcessed.attributes();

            if (originalName == QStringLiteral("en-todo")) {
                bool originalChecked = false;
                if (originalAttributes.hasAttribute(QStringLiteral("checked")))
                {
                    const auto originalCheckedStr =
                        originalAttributes.value(QStringLiteral("checked"));
                    if (originalCheckedStr == QStringLiteral("true")) {
                        originalChecked = true;
                    }
                }

                bool processedChecked = false;
                if (processedAttributes.hasAttribute(QStringLiteral("checked")))
                {
                    const auto processedCheckedStr =
                        processedAttributes.value(QStringLiteral("checked"));
                    if (processedCheckedStr == QStringLiteral("true")) {
                        processedChecked = true;
                    }
                }

                if (originalChecked != processedChecked) {
                    QString error = QStringLiteral(
                        "Checked state of ToDo item from the original ENML "
                        "doesn't match the state of the item from "
                        "the processed ENML");

                    PRINT_WARNING(error);
                    return Result<void, QString>{std::move(error)};
                }
            }
            else if (originalName == QStringLiteral("td")) {
                const auto numOriginalAttributes = originalAttributes.size();
                const auto numProcessedAttributes = processedAttributes.size();

                if (numOriginalAttributes != numProcessedAttributes) {
                    QString error = QStringLiteral(
                                        "The number of attributes in tag ") +
                        originalName.toString() +
                        QStringLiteral(" doesn't match in the original and "
                                       "the processed ENMLs");
                    PRINT_WARNING(error);
                    return Result<void, QString>{std::move(error)};
                }

                for (const auto & originalAttribute:
                     std::as_const(originalAttributes))
                {
                    if (originalAttribute.name() == QStringLiteral("style")) {
                        QNTRACE(
                            "tests::enml",
                            "Won't compare the style "
                                << "attribute for td tag as it's known to be "
                                << "slightly modified by the web engine so "
                                   "it's "
                                << "just not easy to compare it");

                        continue;
                    }

                    if (!processedAttributes.contains(originalAttribute)) {
                        QString error = QStringLiteral(
                            "The corresponding attributes within "
                            "the original and the processed "
                            "ENMLs do not match");

                        QNWARNING(
                            "tests::enml",
                            error
                                << ": the original attribute was not found "
                                   "within the processed attributes; "
                                   "original ENML: "
                                << originalSimplified
                                << "\nProcessed ENML: " << processedSimplified
                                << "\nOriginal attribute: name = "
                                << originalAttribute.name()
                                << ", namespace uri = "
                                << originalAttribute.namespaceUri()
                                << ", qualified name = "
                                << originalAttribute.qualifiedName()
                                << ", is default = "
                                << (originalAttribute.isDefault() ? "true"
                                                                  : "false")
                                << ", value = " << originalAttribute.value()
                                << ", prefix = " << originalAttribute.prefix());

                        return Result<void, QString>{std::move(error)};
                    }
                }
            }
            else {
                const auto numOriginalAttributes = originalAttributes.size();
                const auto numProcessedAttributes = processedAttributes.size();

                if (numOriginalAttributes != numProcessedAttributes) {
                    QString error = QStringLiteral(
                                        "The number of attributes in tag ") +
                        originalName.toString() +
                        QStringLiteral(" doesn't match in the original and "
                                       "the processed ENMLs");
                    PRINT_WARNING(error);
                    return Result<void, QString>{std::move(error)};
                }

                for (const auto & originalAttribute:
                     std::as_const(originalAttributes))
                {
                    if (!processedAttributes.contains(originalAttribute)) {
                        QString error = QStringLiteral(
                            "The corresponding attributes within "
                            "the original and the processed ENMLs do not "
                            "match");

                        QNWARNING(
                            "tests::enml",
                            error
                                << ": the original attribute "
                                   "was not found within "
                                   "the processed attributes; "
                                   "original ENML: "
                                << originalSimplified
                                << "\nProcessed ENML: " << processedSimplified
                                << "\nOriginal attribute: name = "
                                << originalAttribute.name()
                                << ", namespace uri = "
                                << originalAttribute.namespaceUri()
                                << ", qualified name = "
                                << originalAttribute.qualifiedName()
                                << ", is default = "
                                << (originalAttribute.isDefault() ? "true"
                                                                  : "false")
                                << ", value = " << originalAttribute.value()
                                << ", prefix = " << originalAttribute.prefix());

                        return Result<void, QString>{std::move(error)};
                    }
                }
            }
        }

        if (readerOriginal.isEndElement() && !readerProcessed.isEndElement()) {
            QString error = QStringLiteral(
                "QXmlStreamReader of the original ENML is "
                "at the end of the element while "
                "the reader of the processed ENML is not");

            PRINT_WARNING(error);
            return Result<void, QString>{std::move(error)};
        }

        if (readerOriginal.isCharacters()) {
            if (!readerProcessed.isCharacters()) {
                auto textOriginal = readerOriginal.text();
                if (textOriginal.toString().simplified().isEmpty()) {
                    continue;
                }

                QString error = QStringLiteral(
                    "QXmlStreamReader of the original ENML "
                    "points to characters while the reader "
                    "of the processed ENML does not");

                QNWARNING(
                    "tests::enml",
                    error << "; original ENML: " << originalSimplified
                          << "\nProcessed ENML: " << processedSimplified);

                return Result<void, QString>{std::move(error)};
            }

            if (readerOriginal.isCDATA()) {
                if (!readerProcessed.isCDATA()) {
                    QString error = QStringLiteral(
                        "QXmlStreamReader of the original "
                        "ENML points to CDATA while the reader "
                        "of the processed ENML does not");

                    QNWARNING(
                        "tests::enml",
                        error << "; original ENML: " << originalSimplified
                              << "\nProcessed ENML: " << processedSimplified);

                    return Result<void, QString>{std::move(error)};
                }
            }

            const QString textOriginal =
                readerOriginal.text().toString().simplified();

            const QString textProcessed =
                readerProcessed.text().toString().simplified();

            if (textOriginal != textProcessed) {
                QString error = QStringLiteral(
                    "The text extracted from the corresponding elements of "
                    "both the original ENML and the processed ENML does not "
                    "match");

                QNWARNING(
                    "tests:enml",
                    error << "; original ENML: " << originalSimplified
                          << "\nProcessed ENML: " << processedSimplified
                          << "\nOriginal element text: " << textOriginal
                          << "\nProcessed element text: " << textProcessed);

                return Result<void, QString>{std::move(error)};
            }
        }

        if (readerOriginal.isEndDocument() && !readerProcessed.isEndDocument())
        {
            QString error = QStringLiteral(
                "QXmlStreamReader of the original ENML is at "
                "the end of the document while the reader of "
                "the processed ENML is not");

            QNWARNING(
                "tests::enml",
                error << "; original ENML: " << originalSimplified
                      << "\nProcessed ENML: " << processedSimplified);

            return Result<void, QString>{std::move(error)};
        }
    }

    if (readerOriginal.atEnd() != readerProcessed.atEnd()) {
        QString error = QStringLiteral(
            "QXmlStreamReaders for the original ENML and "
            "the processed ENML have not both came to their "
            "ends after the checking loop");

        QNWARNING(
            "tests::enml",
            error << "; original ENML: " << originalSimplified
                  << "\nProcessed ENML: " << processedSimplified);

        return Result<void, QString>{std::move(error)};
    }

    return Result<void, QString>{};
}

[[nodiscard]] Result<void, QString> convertEnmlToHtmlAndBackImpl(
    const QString & enml, IDecryptedTextCache & decryptedTextCache)
{
    auto converter = createConverter();

    auto res = converter->convertEnmlToHtml(enml, decryptedTextCache);
    if (Q_UNLIKELY(!res.isValid())) {
        QString error = QStringLiteral("Unable to convert ENML to HTML: ");
        error += res.error().nonLocalizedString();
        QNWARNING("tests::enml", error);
        return Result<void, QString>{std::move(error)};
    }

    auto htmlData = res.get();
    Q_ASSERT(htmlData);

    QString html = htmlData->html();
    html.prepend(
        QStringLiteral("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
                       "\"http://www.w3.org/TR/html4/strict.dtd\">"
                       "<html><head>"
                       "<meta http-equiv=\"Content-Type\" "
                       "content=\"text/html\" charset=\"UTF-8\" />"
                       "<title></title></head>"));

    html.append(QStringLiteral("</html>"));

    auto res2 = converter->convertHtmlToEnml(html, decryptedTextCache);
    if (Q_UNLIKELY(!res2.isValid())) {
        QString error = QStringLiteral("Unable to convert HTML to ENML: ");
        error += res2.error().nonLocalizedString();
        QNWARNING("tests::enml", error);
        return Result<void, QString>{std::move(error)};
    }

    auto res3 = compareEnml(enml, res2.get());
    if (Q_UNLIKELY(!res3.isValid())) {
        QString error = QStringLiteral(
            "ENML -> HTML -> ENML conversion revealed "
            "inconsistencies: ");
        error += res3.error();
        QNWARNING("tests::enml", error << "\n\nHTML: " << html);
        return Result<void, QString>{std::move(error)};
    }

    return Result<void, QString>{};
}

} // namespace

TEST(ConverterTest, ConvertSimpleEnmlToHtmlAndBack)
{
    const QString enml = QStringLiteral(
        R"#(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE en-note SYSTEM 
"http://xml.evernote.com/pub/enml2.dtd">
<en-note>
<span style="font-weight:bold;color:red;">
Here's some bold red text!</span>
<div>Hickory, dickory, dock,</div>
<div>The mouse ran up the clock.</div>
<div>The clock struck one,</div>
<div>The mouse ran down,</div>
<div>Hickory, dickory, dock.</div>
<div><br/></div>
<div>-- Author unknown</div>
</en-note>)#");

    auto decryptedTextCache = createDecryptedTextCache();
    auto res = convertEnmlToHtmlAndBackImpl(enml, *decryptedTextCache);
    EXPECT_TRUE(res.isValid()) << res.error().toStdString();
}

TEST(ConverterTest, ConvertEnmlWithToDoTagsToHtmlAndBack)
{
    const QString enml = QStringLiteral(
        R"#(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE en-note SYSTEM 
"http://xml.evernote.com/pub/enml2.dtd">
<en-note>
<h1>Hello, world!</h1>
<div>Here's the note with some todo tags</div>
<en-todo/>An item that I haven't completed yet
<br/>
<en-todo checked="true"/>A completed item
<br/>
<en-todo checked="false"/>Another not yet completed item
</en-note>)#");

    auto decryptedTextCache = createDecryptedTextCache();
    auto res = convertEnmlToHtmlAndBackImpl(enml, *decryptedTextCache);
    EXPECT_TRUE(res.isValid()) << res.error().toStdString();
}

TEST(ConverterTest, ConvertEnmlWithEncryptedFragmentsToHtmlAndBack)
{
    const QString enml = QStringLiteral(
        R"#(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE en-note SYSTEM 
"http://xml.evernote.com/pub/enml2.dtd">
<en-note>
<h3>This note contains encrypted text</h3>
<br/>
<div>Here's the encrypted text containing only 
the hint attribute</div>
<en-crypt hint="this is my rifle, this is my gun">
RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf
7pu3/2VOBqZMvfkKGh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+Sp
JFHntkeLglxFWJt6oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx
6sW/KqIfdr+0rF4k+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6m
HHHPA03olCbi7ePVwO7e94mpuvcg2lGTJyDw/NoZmjFycjXESRJgLIr+
gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG+Vem1pTWgRfYXF70mMduEmAd
4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wIZM5CW2rbM2lwM/R0IEno
K7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<br/><div>Here's the encrypted text containing only 
the cipher attribute</div>
<en-crypt cipher="AES">RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw
3uuwMKs32Y+wJGLZa0N8PcTzf7pu3/2VOBqZMvfkKGh4mnJuGy45ZT2T
wOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeLglxFWJt6oIG14i7IpamIuYy
E5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k+rqP7tpI5ha/ALkh
aZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mpuvcg2lGTJ
yDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG
+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409
LK9wIZM5CW2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<br/><div>Here's the encrypted text containing only 
the length attribute</div>
<en-crypt length="128">RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw
3uuwMKs32Y+wJGLZa0N8PcTzf7pu3/2VOBqZMvfkKGh4mnJuGy45ZT2T
wOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeLglxFWJt6oIG14i7IpamIuYyE
5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k+rqP7tpI5ha/ALkhaZ
AuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mpuvcg2lGTJyDw
/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG+Vem
1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wI
ZM5CW2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<br/><div>Here's the encrypted text containing cipher 
and length attributes</div>
<en-crypt cipher="AES" length="128">RU5DMI1mnQ7fKjBk
9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf7pu3/2VOBqZMvfkK
Gh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeLglxFWJt6
oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k
+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVw
O7e94mpuvcg2lGTJyDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8
Rb0M9vGK1tG9haG+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW
9iPpffWTZgD409LK9wIZM5CW2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/H
F+8E=</en-crypt>
<br/><div>Here's the encrypted text containing cipher 
and hint attributes</div>
<en-crypt hint="this is my rifle, this is my gun" 
cipher="AES">
RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf7pu3
/2VOBqZMvfkKGh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeL
glxFWJt6oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k
+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mp
uvcg2lGTJyDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG
+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wIZM5C
W2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<br/><div>Here's the encrypted text containing length 
and hint attributes</div>
<en-crypt hint="this is my rifle, this is my gun" 
length="128">
RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf7pu3
/2VOBqZMvfkKGh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeL
glxFWJt6oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k
+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mp
uvcg2lGTJyDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG
+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wIZM5C
W2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<br/><div>Here's the encrypted text containing cipher, 
length and hint attributes</div>
<en-crypt hint="this is my rifle, this is my gun" 
cipher="AES" length="128">
RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf7pu3
/2VOBqZMvfkKGh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeL
glxFWJt6oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k
+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mp
uvcg2lGTJyDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG
+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wIZM5C
W2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=</en-crypt>
<div>Here's the text encrypted with RC2 which should 
reside in decrypted text cache</div>
<en-crypt hint="my_own_encryption_key_1988">
K+sUXSxI2Mt075+pSDxR/gnCNIEnk5XH1P/D0Eie17
JIWgGnNo5QeMo3L0OeBORARGvVtBlmJx6vJY2Ij/2En
MVy6/aifSdZXAxRlfnTLvI1IpVgHpTMzEfy6zBVMo+V
Bt2KglA+7L0iSjA0hs3GEHI6ZgzhGfGj</en-crypt>
<div>Here's the text encrypted with AES which should 
reside in decrypted text cache</div>
<en-crypt hint="MyEncryptionPassword">
RU5DMBwXjfKR+x9ksjSJhtiF+CxfwXn2Hf/WqdVwLwJDX9YX5R34Z5SBMSCIOFF
r1MUeNkzHGVP5fHEppUlIExDG/Vpjh9KK1uu0VqTFoUWA0IXAAMA5eHnbxhBrjvL
3CoTQV7prRqJVLpUX77Q0vbNims1quxVWaf7+uVeK60YoiJnSOHvEYptoOs1FVfZ
AwnDDBoCUOsAb2nCh2UZ6LSFneb58xQ/6WeoQ7QDDHLSoUIXn</en-crypt>
</en-note>)#");

    auto decryptedTextCache = createDecryptedTextCache();

    decryptedTextCache->addDecryptexTextInfo(
        QStringLiteral("K+sUXSxI2Mt075+pSDxR/gnCNIEnk5XH1P/D0Eie17"
                       "JIWgGnNo5QeMo3L0OeBORARGvVtBlmJx6vJY2Ij/2En"
                       "MVy6/aifSdZXAxRlfnTLvI1IpVgHpTMzEfy6zBVMo+V"
                       "Bt2KglA+7L0iSjA0hs3GEHI6ZgzhGfGj"),
        QStringLiteral("<span style=\"display: inline !important; float: none; "
                       "\">Ok, here's a piece of text I'm going to encrypt "
                       "now</span>"),
        QStringLiteral("my_own_encryption_key_1988"), QStringLiteral("RC2"), 64,
        IDecryptedTextCache::RememberForSession::Yes);

    decryptedTextCache->addDecryptexTextInfo(
        QStringLiteral("RU5DMBwXjfKR+x9ksjSJhtiF+CxfwXn2Hf/WqdVwLwJDX9YX5R34Z5S"
                       "BMSCIOFFr1MUeNkzHGVP5fHEppUlIExDG/Vpjh9KK1uu0VqTFoUWA0I"
                       "XAAMA5eHnbxhBrjvL3CoTQV7prRqJVLpUX77Q0vbNims1quxVWaf7+u"
                       "VeK60YoiJnSOHvEYptoOs1FVfZAwnDDBoCUOsAb2nCh2UZ6LSFneb58"
                       "xQ/6WeoQ7QDDHLSoUIXn"),
        QStringLiteral("Sample text said to be the decrypted one"),
        QStringLiteral("MyEncryptionPassword"), QStringLiteral("AES"), 128,
        IDecryptedTextCache::RememberForSession::Yes);

    auto res = convertEnmlToHtmlAndBackImpl(enml, *decryptedTextCache);
    EXPECT_TRUE(res.isValid()) << res.error().toStdString();
}

TEST(ConverterTest, ConvertEnmlWithEnMediaTagsToHtmlAndBack)
{
    const QString enml = QStringLiteral(
        R"#(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE en-note SYSTEM 
"http://xml.evernote.com/pub/enml2.dtd">
<en-note>
<div>Here's the note with some embedded resources</div>
<br/>
<div>The first resource: simple image</div>
<en-media width="640" height="480" align="right" 
type="image/jpeg" hash="f03c1c2d96bc67eda02968c8b5af9008"/>
<div>The second resource: embedded pdf</div>
<en-media width="600" height="800" title="My cool pdf" 
type="application/pdf" hash="6051a24c8677fd21c65c1566654c228"/>
</en-note>)#");

    auto decryptedTextCache = createDecryptedTextCache();
    auto res = convertEnmlToHtmlAndBackImpl(enml, *decryptedTextCache);
    EXPECT_TRUE(res.isValid()) << res.error().toStdString();
}

class ConverterComplexEnmlTest : public testing::TestWithParam<int>
{};

constexpr std::array gComplexEnmlIndexes{1, 2, 3, 4};

INSTANTIATE_TEST_SUITE_P(
    ConverterComplexEnmlTextInstance, ConverterComplexEnmlTest,
    testing::ValuesIn(gComplexEnmlIndexes));

TEST_P(ConverterComplexEnmlTest, ConvertComplexEnmlToHtmlAndBack)
{
    QFile file{QString::fromUtf8(":/tests/complexNote%1.txt").arg(GetParam())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));

    const QString enml = QString::fromLocal8Bit(file.readAll());

    auto decryptedTextCache = createDecryptedTextCache();
    auto res = convertEnmlToHtmlAndBackImpl(enml, *decryptedTextCache);
    EXPECT_TRUE(res.isValid()) << res.error().toStdString();
}

} // namespace quentier::enml::tests