/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDEX_ITEM_H
#define LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDEX_ITEM_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QByteArray>
#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceRecognitionIndexItemData)

class QUENTIER_EXPORT ResourceRecognitionIndexItem : public Printable
{
public:
    explicit ResourceRecognitionIndexItem();
    ResourceRecognitionIndexItem(const ResourceRecognitionIndexItem & other);

    ResourceRecognitionIndexItem & operator=(
        const ResourceRecognitionIndexItem & other);

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

    [[nodiscard]] QVector<int> strokeList() const;
    [[nodiscard]] int numStrokes() const;
    [[nodiscard]] bool strokeAt(int strokeIndex, int & stroke) const;
    [[nodiscard]] bool setStrokeAt(int strokeIndex, int stroke);
    void setStrokeList(const QVector<int> & strokeList);
    void reserveStrokeListSpace(int numItems);
    void addStroke(int stroke);
    [[nodiscard]] bool removeStroke(int stroke);
    [[nodiscard]] bool removeStrokeAt(int strokeIndex);

    struct TextItem
    {
        [[nodiscard]] bool operator==(const TextItem & other) const
        {
            return (m_text == other.m_text) && (m_weight == other.m_weight);
        }

        QString m_text;
        int m_weight = -1;
    };

    [[nodiscard]] QVector<TextItem> textItems() const;
    [[nodiscard]] int numTextItems() const;
    [[nodiscard]] bool textItemAt(int textItemIndex, TextItem & textItem) const;

    [[nodiscard]] bool setTextItemAt(
        int textItemIndex, const TextItem & textItem);

    void setTextItems(const QVector<TextItem> & textItems);
    void reserveTextItemsSpace(int numItems);
    void addTextItem(const TextItem & item);
    [[nodiscard]] bool removeTextItem(const TextItem & item);
    [[nodiscard]] bool removeTextItemAt(int textItemIndex);

    struct ObjectItem
    {
        [[nodiscard]] bool operator==(const ObjectItem & other) const
        {
            return (m_objectType == other.m_objectType) &&
                (m_weight == other.m_weight);
        }

        QString m_objectType;
        int m_weight = -1;
    };

    [[nodiscard]] QVector<ObjectItem> objectItems() const;
    [[nodiscard]] int numObjectItems() const;

    [[nodiscard]] bool objectItemAt(
        int objectItemIndex, ObjectItem & objectItem) const;

    [[nodiscard]] bool setObjectItemAt(
        int objectItemIndex, const ObjectItem & objectItem);

    void setObjectItems(const QVector<ObjectItem> & objectItems);
    void reserveObjectItemsSpace(int numItems);
    void addObjectItem(const ObjectItem & item);
    [[nodiscard]] bool removeObjectItem(const ObjectItem & item);
    [[nodiscard]] bool removeObjectItemAt(int objectItemIndex);

    struct ShapeItem
    {
        [[nodiscard]] bool operator==(const ShapeItem & other) const
        {
            return (m_shapeType == other.m_shapeType) &&
                (m_weight == other.m_weight);
        }

        QString m_shapeType;
        int m_weight = -1;
    };

    [[nodiscard]] QVector<ShapeItem> shapeItems() const;
    [[nodiscard]] int numShapeItems() const;

    [[nodiscard]] bool shapeItemAt(
        int shapeItemIndex, ShapeItem & shapeItem) const;

    [[nodiscard]] bool setShapeItemAt(
        int shapeItemIndex, const ShapeItem & shapeItem);

    void setShapeItems(const QVector<ShapeItem> & shapeItems);
    void reserveShapeItemsSpace(int numItems);
    void addShapeItem(const ShapeItem & item);
    [[nodiscard]] bool removeShapeItem(const ShapeItem & item);
    [[nodiscard]] bool removeShapeItemAt(int shapeItemIndex);

    struct BarcodeItem
    {
        [[nodiscard]] bool operator==(const BarcodeItem & other) const
        {
            return (m_barcode == other.m_barcode) &&
                (m_weight == other.m_weight);
        }

        QString m_barcode;
        int m_weight = -1;
    };

    [[nodiscard]] QVector<BarcodeItem> barcodeItems() const;
    [[nodiscard]] int numBarcodeItems() const;

    [[nodiscard]] bool barcodeItemAt(
        int barcodeItemIndex, BarcodeItem & barcodeItem) const;

    [[nodiscard]] bool setBarcodeItemAt(
        int barcodeItemIndex, const BarcodeItem & barcodeItem);

    void setBarcodeItems(const QVector<BarcodeItem> & barcodeItems);
    void reserveBarcodeItemsSpace(int numItems);
    void addBarcodeItem(const BarcodeItem & item);
    [[nodiscard]] bool removeBarcodeItem(const BarcodeItem & item);
    [[nodiscard]] bool removeBarcodeItemAt(int barcodeItemIndex);

    QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ResourceRecognitionIndexItemData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDEX_ITEM_H
