/*
 * Copyright 2023 Dmitry Ivanov
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

#pragma once

#include <quentier/enml/IConverter.h>

#include <QSet>
#include <QXmlStreamAttributes>

#include <cstddef>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace quentier::enml {

class Converter : public IConverter
{
public:
    explicit Converter(IENMLTagsConverterPtr enmlTagsConverter);

public: // IConverter
    [[nodiscard]] Result<QString, ErrorString> convertHtmlToEnml(
        const QString & html, IDecryptedTextCache & decryptedTextCache,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {})
        const override;

    [[nodiscard]] Result<void, ErrorString> convertHtmlToDoc(
        const QString & html, QTextDocument & doc,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {})
        const override;

    [[nodiscard]] Result<QString, ErrorString> convertHtmlToXml(
        const QString & html) const override;

    [[nodiscard]] Result<QString, ErrorString> convertHtmlToXhtml(
        const QString & html) const override;

    [[nodiscard]] Result<IHtmlDataPtr, ErrorString> convertEnmlToHtml(
        const QString & enml,
        IDecryptedTextCache & decryptedTextCache) const override;

    [[nodiscard]] Result<QString, ErrorString> convertEnmlToPlainText(
        const QString & enml) const override;

    [[nodiscard]] Result<QStringList, ErrorString> convertEnmlToWordsList(
        const QString & enml) const override;

    [[nodiscard]] QStringList convertPlainTextToWordsList(
        const QString & plainText) const override;

    [[nodiscard]] Result<void, ErrorString> validateEnml(
        const QString & enml) const override;

    [[nodiscard]] Result<QString, ErrorString> validateAndFixupEnml(
        const QString & enml) const override;

    [[nodiscard]] Result<QString, ErrorString> exportNotesToEnex(
        const QList<qevercloud::Note> & notes,
        const QHash<QString, QString> & tagNamesByTagLocalIds,
        EnexExportTags exportTagsOption,
        const QString & version = {}) const override;

    [[nodiscard]] Result<QList<qevercloud::Note>, ErrorString> importEnex(
        const QString & enex) const override;

private:
    struct ConversionState
    {
        int m_writeElementCounter = 0;
        QString m_lastElementName;
        QXmlStreamAttributes m_lastElementAttributes;

        bool m_insideEnCryptElement = false;
        bool m_insideEnMediaElement = false;

        QXmlStreamAttributes m_enMediaAttributes;

        std::size_t m_skippedElementNestingCounter = 0;
        std::size_t m_skippedElementWithPreservedContentsNestingCounter = 0;
    };

    enum class ProcessElementStatus
    {
        ProcessedPartially = 0,
        ProcessedFully,
        Error
    };

    [[nodiscard]] ProcessElementStatus
        processElementForHtmlToNoteContentConversion(
            const QList<conversion_rules::ISkipRulePtr> & skipRules,
            ConversionState & state, IDecryptedTextCache & decryptedTextCache,
            QXmlStreamReader & reader, QXmlStreamWriter & writer,
            ErrorString & errorDescription) const;

    [[nodiscard]] bool isForbiddenXhtmlAttribute(
        const QString & attributeName) const noexcept;

    [[nodiscard]] Result<void, ErrorString> validateAgainstDtd(
        const QString & input, const QString & dtdFilePath) const;

private:
    const IENMLTagsConverterPtr m_enmlTagsConverter;
    const QSet<QString> m_forbiddenXhtmlTags;
    const QSet<QString> m_forbiddenXhtmlAttributes;
    const QSet<QString> m_evernoteSpecificXhtmlTags;
    const QSet<QString> m_allowedXhtmlTags;
    const QSet<QString> m_allowedEnMediaAttributes;
};

} // namespace quentier::enml
