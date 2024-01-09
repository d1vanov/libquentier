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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QByteArray>
#include <QList>
#include <QSharedDataPointer>

#include <memory>

namespace quentier {

class ResourceRecognitionIndexItemData;

class QUENTIER_EXPORT ResourceRecognitionIndexItem : public Printable
{
public:
    explicit ResourceRecognitionIndexItem();

    ResourceRecognitionIndexItem(const ResourceRecognitionIndexItem & other);

    ResourceRecognitionIndexItem(
        ResourceRecognitionIndexItem && other) noexcept;

    ResourceRecognitionIndexItem & operator=(
        const ResourceRecognitionIndexItem & other);

    ResourceRecognitionIndexItem & operator=(
        ResourceRecognitionIndexItem && other) noexcept;

    ~ResourceRecognitionIndexItem() override;

    [[nodiscard]] bool isValid() const;

    [[nodiscard]] int x() const;
    void setX(int x);

    [[nodiscard]] int y() const;
    void setY(int y);

    [[nodiscard]] int h() const;
    void setH(int h);

    [[nodiscard]] int w() const;
    void setW(int w);

    [[nodiscard]] int offset() const;
    void setOffset(int offset);

    [[nodiscard]] int duration() const;
    void setDuration(int duration);

    [[nodiscard]] QList<int> strokes() const;
    void setStrokes(QList<int> strokes);

    struct QUENTIER_EXPORT ITextItem
    {
        virtual ~ITextItem();

        [[nodiscard]] virtual QString text() const = 0;
        [[nodiscard]] virtual int weight() const = 0;
    };

    using ITextItemPtr = std::shared_ptr<ITextItem>;

    [[nodiscard]] QList<ITextItemPtr> textItems() const;
    void setTextItems(QList<ITextItemPtr> textItems);

    struct QUENTIER_EXPORT IObjectItem
    {
        virtual ~IObjectItem();

        [[nodiscard]] virtual QString objectType() const = 0;
        [[nodiscard]] virtual int weight() const = 0;
    };

    using IObjectItemPtr = std::shared_ptr<IObjectItem>;

    [[nodiscard]] QList<IObjectItemPtr> objectItems() const;
    void setObjectItems(QList<IObjectItemPtr> objectItems);

    struct QUENTIER_EXPORT IShapeItem
    {
        virtual ~IShapeItem();

        [[nodiscard]] virtual QString shape() const = 0;
        [[nodiscard]] virtual int weight() const = 0;
    };

    using IShapeItemPtr = std::shared_ptr<IShapeItem>;

    [[nodiscard]] QList<IShapeItemPtr> shapeItems() const;
    void setShapeItems(QList<IShapeItemPtr> shapeItems);

    struct QUENTIER_EXPORT IBarcodeItem
    {
        virtual ~IBarcodeItem();

        [[nodiscard]] virtual QString barcode() const = 0;
        [[nodiscard]] virtual int weight() const = 0;
    };

    using IBarcodeItemPtr = std::shared_ptr<IBarcodeItem>;

    [[nodiscard]] QList<IBarcodeItemPtr> barcodeItems() const;
    void setBarcodeItems(QList<IBarcodeItemPtr> barcodeItems);

    QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ResourceRecognitionIndexItemData> d;
};

} // namespace quentier
