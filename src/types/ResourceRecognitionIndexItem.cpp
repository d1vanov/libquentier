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

#include <quentier/types/ResourceRecognitionIndexItem.h>

#include "data/ResourceRecognitionIndexItemData.h"

#include <utility>

namespace quentier {

ResourceRecognitionIndexItem::ResourceRecognitionIndexItem() :
    d(new ResourceRecognitionIndexItemData)
{}

ResourceRecognitionIndexItem::ResourceRecognitionIndexItem(
    const ResourceRecognitionIndexItem & other) = default;

ResourceRecognitionIndexItem::ResourceRecognitionIndexItem(
    ResourceRecognitionIndexItem && other) noexcept = default;

ResourceRecognitionIndexItem & ResourceRecognitionIndexItem::operator=(
    const ResourceRecognitionIndexItem & other) = default;

ResourceRecognitionIndexItem & ResourceRecognitionIndexItem::operator=(
    ResourceRecognitionIndexItem && other) noexcept = default;

ResourceRecognitionIndexItem::~ResourceRecognitionIndexItem() = default;

ResourceRecognitionIndexItem::ITextItem::~ITextItem() = default;

ResourceRecognitionIndexItem::IObjectItem::~IObjectItem() = default;

ResourceRecognitionIndexItem::IShapeItem::~IShapeItem() = default;

ResourceRecognitionIndexItem::IBarcodeItem::~IBarcodeItem() = default;

bool ResourceRecognitionIndexItem::isValid() const
{
    return d->isValid();
}

int ResourceRecognitionIndexItem::x() const
{
    return d->m_x;
}

void ResourceRecognitionIndexItem::setX(const int x)
{
    d->m_x = x;
}

int ResourceRecognitionIndexItem::y() const
{
    return d->m_y;
}

void ResourceRecognitionIndexItem::setY(const int y)
{
    d->m_y = y;
}

int ResourceRecognitionIndexItem::h() const
{
    return d->m_h;
}

void ResourceRecognitionIndexItem::setH(const int h)
{
    d->m_h = h;
}

int ResourceRecognitionIndexItem::w() const
{
    return d->m_w;
}

void ResourceRecognitionIndexItem::setW(const int w)
{
    d->m_w = w;
}

int ResourceRecognitionIndexItem::offset() const
{
    return d->m_offset;
}

void ResourceRecognitionIndexItem::setOffset(const int offset)
{
    d->m_offset = offset;
}

int ResourceRecognitionIndexItem::duration() const
{
    return d->m_duration;
}

void ResourceRecognitionIndexItem::setDuration(const int duration)
{
    d->m_duration = duration;
}

QList<int> ResourceRecognitionIndexItem::strokes() const
{
    return d->m_strokes;
}

void ResourceRecognitionIndexItem::setStrokes(QList<int> strokes)
{
    d->m_strokes = std::move(strokes);
}

QList<ResourceRecognitionIndexItem::ITextItemPtr>
    ResourceRecognitionIndexItem::textItems() const
{
    return d->m_textItems;
}

void ResourceRecognitionIndexItem::setTextItems(QList<ITextItemPtr> textItems)
{
    d->m_textItems = std::move(textItems);
}

QList<ResourceRecognitionIndexItem::IObjectItemPtr>
    ResourceRecognitionIndexItem::objectItems() const
{
    return d->m_objectItems;
}

void ResourceRecognitionIndexItem::setObjectItems(
    QList<IObjectItemPtr> objectItems)
{
    d->m_objectItems = std::move(objectItems);
}

QList<ResourceRecognitionIndexItem::IShapeItemPtr>
    ResourceRecognitionIndexItem::shapeItems() const
{
    return d->m_shapeItems;
}

void ResourceRecognitionIndexItem::setShapeItems(
    QList<IShapeItemPtr> shapeItems)
{
    d->m_shapeItems = std::move(shapeItems);
}

QList<ResourceRecognitionIndexItem::IBarcodeItemPtr>
    ResourceRecognitionIndexItem::barcodeItems() const
{
    return d->m_barcodeItems;
}

void ResourceRecognitionIndexItem::setBarcodeItems(
    QList<IBarcodeItemPtr> barcodeItems)
{
    d->m_barcodeItems = std::move(barcodeItems);
}

QTextStream & ResourceRecognitionIndexItem::print(QTextStream & strm) const
{
    strm << "ResourceRecognitionIndexItem: {\n";

    if (d->m_x >= 0) {
        strm << "  x = " << d->m_x << ";\n";
    }
    else {
        strm << "  x is not set;\n";
    }

    if (d->m_y >= 0) {
        strm << "  y = " << d->m_y << ";\n";
    }
    else {
        strm << "  y is not set;\n";
    }

    if (d->m_h >= 0) {
        strm << "  h = " << d->m_h << ";\n";
    }
    else {
        strm << "  h is not set;\n";
    }

    if (d->m_w >= 0) {
        strm << "  w = " << d->m_w << ";\n";
    }
    else {
        strm << "  w is not set;\n";
    }

    if (d->m_offset >= 0) {
        strm << "  offset = " << d->m_offset << ";\n";
    }
    else {
        strm << "  offset is not set;\n";
    }

    if (d->m_duration >= 0) {
        strm << "  duration = " << d->m_duration << ";\n";
    }
    else {
        strm << "  duration is not set;\n";
    }

    if (!d->m_strokes.isEmpty()) {
        strm << "  stroke list: ";
        for (const auto stroke: std::as_const(d->m_strokes)) {
            strm << stroke << " ";
        }
        strm << ";\n";
    }
    else {
        strm << "  stroke list is not set;\n";
    }

    if (!d->m_textItems.isEmpty()) {
        strm << "  text items: \n";
        for (const auto & item: std::as_const(d->m_textItems)) {
            if (Q_UNLIKELY(!item)) {
                continue;
            }

            strm << "    text: " << item->text()
                 << "; weight = " << item->weight() << ";\n";
        }
    }
    else {
        strm << "  text items are not set;\n";
    }

    if (!d->m_objectItems.isEmpty()) {
        strm << "  object items: \n";

        for (const auto & item: std::as_const(d->m_objectItems)) {
            if (Q_UNLIKELY(!item)) {
                continue;
            }

            strm << "    object type: " << item->objectType()
                 << "; weight: " << item->weight() << ";\n";
        }
    }
    else {
        strm << "  object items are not set;\n";
    }

    if (!d->m_shapeItems.isEmpty()) {
        strm << "  shape items: \n";
        for (const auto & item: std::as_const(d->m_shapeItems)) {
            if (Q_UNLIKELY(!item)) {
                continue;
            }

            strm << "    shape: " << item->shape()
                 << "; weight: " << item->weight() << ";\n";
        }
    }
    else {
        strm << "  shape items are not set;\n";
    }

    if (!d->m_barcodeItems.isEmpty()) {
        strm << "  barcode items: \n";
        for (const auto & item: std::as_const(d->m_barcodeItems)) {
            if (Q_UNLIKELY(!item)) {
                continue;
            }
            strm << "    barcode: " << item->barcode()
                 << "; weight: " << item->weight() << ";\n";
        }
    }
    else {
        strm << "  barcode items are not set;\n";
    }

    strm << "};\n";
    return strm;
}

} // namespace quentier
