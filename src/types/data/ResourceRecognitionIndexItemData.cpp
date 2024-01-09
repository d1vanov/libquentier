/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include "ResourceRecognitionIndexItemData.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

bool ResourceRecognitionIndexItemData::isValid() const
{
    if (m_textItems.isEmpty() && m_objectItems.isEmpty() &&
        m_shapeItems.isEmpty() && m_barcodeItems.isEmpty())
    {
        QNTRACE("types::data", "Resource recognition index item is empty");
        return false;
    }

    for (const auto & textItem: qAsConst(m_textItems)) {
        if (Q_UNLIKELY(!textItem)) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains null text item");
            return false;
        }

        if (textItem->weight() < 0) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains "
                    << "text item with weight less than 0: " << textItem->text()
                    << ", weight = " << textItem->weight());
            return false;
        }
    }

    for (const auto & objectItem: qAsConst(m_objectItems)) {
        if (Q_UNLIKELY(!objectItem)) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains null object item");
            return false;
        }

        if (objectItem->weight() < 0) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains "
                    << "object item with weight less than 0: "
                    << objectItem->objectType()
                    << ", weight = " << objectItem->weight());
            return false;
        }

        const auto objectType = objectItem->objectType();
        if ((objectType != QStringLiteral("face")) &&
            (objectType != QStringLiteral("sky")) &&
            (objectType != QStringLiteral("ground")) &&
            (objectType != QStringLiteral("water")) &&
            (objectType != QStringLiteral("lake")) &&
            (objectType != QStringLiteral("sea")) &&
            (objectType != QStringLiteral("snow")) &&
            (objectType != QStringLiteral("mountains")) &&
            (objectType != QStringLiteral("verdure")) &&
            (objectType != QStringLiteral("grass")) &&
            (objectType != QStringLiteral("trees")) &&
            (objectType != QStringLiteral("building")) &&
            (objectType != QStringLiteral("road")) &&
            (objectType != QStringLiteral("car")))
        {
            QNTRACE(
                "types::data",
                "Resource recognition index object item has "
                    << "invalid object type: " << objectType);
            return false;
        }
    }

    for (const auto & shapeItem: qAsConst(m_shapeItems)) {
        if (Q_UNLIKELY(!shapeItem)) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains null shape item");
            return false;
        }

        if (shapeItem->weight() < 0) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains "
                    << "shape item with weight less than 0: "
                    << shapeItem->shape()
                    << ", weight = " << shapeItem->weight());
            return false;
        }

        const auto shape = shapeItem->shape();
        if ((shape != QStringLiteral("circle")) &&
            (shape != QStringLiteral("oval")) &&
            (shape != QStringLiteral("rectangle")) &&
            (shape != QStringLiteral("triangle")) &&
            (shape != QStringLiteral("line")) &&
            (shape != QStringLiteral("arrow")) &&
            (shape != QStringLiteral("polyline")))
        {
            QNTRACE(
                "types::data",
                "Resource recognition index shape item has "
                    << "invalid shape type: " << shape);
            return false;
        }
    }

    for (const auto & barcodeItem: qAsConst(m_barcodeItems)) {
        if (Q_UNLIKELY(!barcodeItem)) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains null barcode item");
            return false;
        }

        if (barcodeItem->weight() < 0) {
            QNTRACE(
                "types::data",
                "Resource recognition index item contains "
                    << "barcode item with weight less than 0: "
                    << barcodeItem->barcode()
                    << ", weight = " << barcodeItem->weight());
            return false;
        }
    }

    return true;
}

} // namespace quentier
