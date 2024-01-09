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

#pragma once

#include <quentier/types/ResourceRecognitionIndexItem.h>

#include <QSharedData>
#include <QString>
#include <QVector>

namespace quentier {

class ResourceRecognitionIndexItemData final : public QSharedData
{
public:
    [[nodiscard]] bool isValid() const;

public:
    using ITextItem = ResourceRecognitionIndexItem::ITextItem;
    using ITextItemPtr = ResourceRecognitionIndexItem::ITextItemPtr;

    using IObjectItem = ResourceRecognitionIndexItem::IObjectItem;
    using IObjectItemPtr = ResourceRecognitionIndexItem::IObjectItemPtr;

    using IShapeItem = ResourceRecognitionIndexItem::IShapeItem;
    using IShapeItemPtr = ResourceRecognitionIndexItem::IShapeItemPtr;

    using IBarcodeItem = ResourceRecognitionIndexItem::IBarcodeItem;
    using IBarcodeItemPtr = ResourceRecognitionIndexItem::IBarcodeItemPtr;

    struct TextItem : public ResourceRecognitionIndexItem::ITextItem
    {
        [[nodiscard]] QString text() const override
        {
            return m_text;
        }

        [[nodiscard]] int weight() const noexcept override
        {
            return m_weight;
        }

        QString m_text;
        int m_weight = 0;
    };

    struct ObjectItem : public ResourceRecognitionIndexItem::IObjectItem
    {
        [[nodiscard]] QString objectType() const override
        {
            return m_objectType;
        }

        [[nodiscard]] int weight() const noexcept override
        {
            return m_weight;
        }

        QString m_objectType;
        int m_weight = 0;
    };

    struct ShapeItem : public ResourceRecognitionIndexItem::IShapeItem
    {
        [[nodiscard]] QString shape() const override
        {
            return m_shape;
        }

        [[nodiscard]] int weight() const noexcept override
        {
            return m_weight;
        }

        QString m_shape;
        int m_weight = 0;
    };

    struct BarcodeItem : public ResourceRecognitionIndexItem::IBarcodeItem
    {
        [[nodiscard]] QString barcode() const override
        {
            return m_barcode;
        }

        [[nodiscard]] int weight() const noexcept override
        {
            return m_weight;
        }

        QString m_barcode;
        int m_weight = 0;
    };

public:
    int m_x = -1;
    int m_y = -1;
    int m_h = -1;
    int m_w = -1;

    int m_offset;
    int m_duration;

    QList<int> m_strokes;
    QList<ITextItemPtr> m_textItems;
    QList<IObjectItemPtr> m_objectItems;
    QList<IShapeItemPtr> m_shapeItems;
    QList<IBarcodeItemPtr> m_barcodeItems;
};

} // namespace quentier
