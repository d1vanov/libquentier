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
    using TextItem = ResourceRecognitionIndexItem::TextItem;
    using ObjectItem = ResourceRecognitionIndexItem::ObjectItem;
    using ShapeItem = ResourceRecognitionIndexItem::ShapeItem;
    using BarcodeItem = ResourceRecognitionIndexItem::BarcodeItem;

public:
    int m_x = -1;
    int m_y = -1;
    int m_h = -1;
    int m_w = -1;

    int m_offset;
    int m_duration;

    QVector<int> m_strokeList;
    QVector<TextItem> m_textItems;
    QVector<ObjectItem> m_objectItems;
    QVector<ShapeItem> m_shapeItems;
    QVector<BarcodeItem> m_barcodeItems;
};

} // namespace quentier
