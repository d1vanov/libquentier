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

#include <quentier/enml/Fwd.h>
#include <quentier/enml/conversion_rules/Fwd.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Result.h>
#include <quentier/utility/Linkage.h>

#include <QList>
#include <QStringList>
#include <QTextDocument>

#include <variant>

namespace quentier::enml {

/**
 * @brief The IConverter interface encapsulates a set of methods
 * performing conversions between ENML and other note content formats, namely
 * HTML for use in the note editor
 */
class QUENTIER_EXPORT IConverter
{
public:
    virtual ~IConverter();

    [[nodiscard]] virtual Result<QString, ErrorString> cleanupExternalHtml(
        const QString & html) const = 0;

    [[nodiscard]] virtual Result<QString, ErrorString> convertHtmlToEnml(
        const QString & html, IDecryptedTextCache & decryptedTextCache,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {}) const = 0;

    [[nodiscard]] virtual Result<QTextDocument, ErrorString> convertHtmlToDoc(
        const QString & html,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {}) const = 0;

    struct HtmlData
    {
        QString m_html;
        quint32 m_numEnToDoNodes = 0;
        quint32 m_numHyperlinkNodes = 0;
        quint32 m_numEnCryptNodes = 0;
        quint32 m_numEnDecryptedNodes = 0;
    };

    [[nodiscard]] virtual Result<HtmlData, ErrorString> convertEnmlToHtml(
        const QString & enml,
        IDecryptedTextCache & decryptedTextCache) const = 0;

    [[nodiscard]] virtual Result<QString, ErrorString> convertEnmlToPlainText(
        const QString & enml) const = 0;

    [[nodiscard]] virtual Result<QStringList, ErrorString>
        convertEnmlToWordsList(const QString & enml) const = 0;

    [[nodiscard]] virtual QStringList convertPlainTextToWordsList(
        const QString & plainText) const = 0;

    [[nodiscard]] virtual Result<void, ErrorString> validateEnml(
        const QString & enml) const = 0;

    [[nodiscard]] virtual Result<QString, ErrorString> validateAndFixupEnml(
        const QString & enml) const = 0;
};

} // namespace quentier::enml
