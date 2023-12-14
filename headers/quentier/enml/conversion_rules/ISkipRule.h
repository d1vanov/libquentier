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

#include <quentier/enml/conversion_rules/MatchMode.h>
#include <quentier/utility/Printable.h>

#include <QtGlobal>

namespace quentier::enml::conversion_rules {

/**
 * @brief The ISkipRule interface describes a conversion rule with regards to
 * which some ENML/HTML element/attribute should be skipped during the
 * conversion.
 *
 * ENML format prohibits the use of certain HTML tags and attributes. This
 * interface facilitates skipping these tags and attributes in the process of
 * conversion from HTML to ENML
 */
class ISkipRule : public Printable
{
public:
    ~ISkipRule() override;

    /**
     * Target to be affected by the skip rule
     */
    enum class Target
    {
        /**
         * HTML element
         */
        Element,
        /**
         * HTML attribute with specified name
         */
        AttibuteName,
        /**
         * HTML attribute with specified value
         */
        AttributeValue
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, Target target);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, Target target);

    /**
     * Target to be affected by the skip rule
     */
    [[nodiscard]] virtual Target target() const = 0;

    /**
     * Name or value of the target
     */
    [[nodiscard]] virtual QString value() const = 0;

    /**
     * Match mode for name or value of the target
     */
    [[nodiscard]] virtual MatchMode matchMode() const = 0;

    /**
     * Specifies whether the element contents should be included without
     * the element itself if it needs to be skipped or not
     */
    [[nodiscard]] virtual bool includeContents() const = 0;

    /**
     * Case sensitivity for target name/value check
     */
    [[nodiscard]] virtual Qt::CaseSensitivity caseSensitivity()
        const = 0;

public: // Printable
    QTextStream & print(QTextStream & strm) const override;
};

} // namespace quentier::enml::conversion_rules
