/*
 * Copyright 2016 Dmitry Ivanov
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

class QUENTIER_EXPORT ResourceRecognitionIndexItem: public Printable
{
public:
    ResourceRecognitionIndexItem();
    ResourceRecognitionIndexItem(const ResourceRecognitionIndexItem & other);
    ResourceRecognitionIndexItem & operator=(const ResourceRecognitionIndexItem & other);
    virtual ~ResourceRecognitionIndexItem();

    bool isValid() const;

    int x() const;
    void setX(const int x);

    int y() const;
    void setY(const int y);

    int h() const;
    void setH(const int h);

    int w() const;
    void setW(const int w);

    int offset() const;
    void setOffset(const int offset);

    int duration() const;
    void setDuration(const int duration);

    QVector<int> strokeList() const;
    int numStrokes() const;
    bool strokeAt(const int strokeIndex, int & stroke) const;
    bool setStrokeAt(const int strokeIndex, const int stroke);
    void setStrokeList(const QVector<int> & strokeList);
    void reserveStrokeListSpace(const int numItems);
    void addStroke(const int stroke);
    bool removeStroke(const int stroke);
    bool removeStrokeAt(const int strokeIndex);

    struct TextItem
    {
        TextItem() : m_text(), m_weight(-1) {}

        bool operator==(const TextItem & other) const { return (m_text == other.m_text) && (m_weight == other.m_weight); }

        QString     m_text;
        int         m_weight;
    };

    QVector<TextItem> textItems() const;
    int numTextItems() const;
    bool textItemAt(const int textItemIndex, TextItem & textItem) const;
    bool setTextItemAt(const int textItemIndex, const TextItem & textItem);
    void setTextItems(const QVector<TextItem> & textItems);
    void reserveTextItemsSpace(const int numItems);
    void addTextItem(const TextItem & item);
    bool removeTextItem(const TextItem & item);
    bool removeTextItemAt(const int textItemIndex);

    struct ObjectItem
    {
        ObjectItem() : m_objectType(), m_weight(-1) {}

        bool operator==(const ObjectItem & other) const { return (m_objectType == other.m_objectType) && (m_weight == other.m_weight); }

        QString     m_objectType;
        int         m_weight;
    };

    QVector<ObjectItem> objectItems() const;
    int numObjectItems() const;
    bool objectItemAt(const int objectItemIndex, ObjectItem & objectItem) const;
    bool setObjectItemAt(const int objectItemIndex, const ObjectItem & objectItem);
    void setObjectItems(const QVector<ObjectItem> & objectItems);
    void reserveObjectItemsSpace(const int numItems);
    void addObjectItem(const ObjectItem & item);
    bool removeObjectItem(const ObjectItem & item);
    bool removeObjectItemAt(const int objectItemIndex);

    struct ShapeItem
    {
        ShapeItem() : m_shapeType(), m_weight(-1) {}

        bool operator==(const ShapeItem & other) const { return (m_shapeType == other.m_shapeType) && (m_weight == other.m_weight); }

        QString     m_shapeType;
        int         m_weight;
    };

    QVector<ShapeItem> shapeItems() const;
    int numShapeItems() const;
    bool shapeItemAt(const int shapeItemIndex, ShapeItem & shapeItem) const;
    bool setShapeItemAt(const int shapeItemIndex, const ShapeItem & shapeItem);
    void setShapeItems(const QVector<ShapeItem> & shapeItems);
    void reserveShapeItemsSpace(const int numItems);
    void addShapeItem(const ShapeItem & item);
    bool removeShapeItem(const ShapeItem & item);
    bool removeShapeItemAt(const int shapeItemIndex);

    struct BarcodeItem
    {
        BarcodeItem() : m_barcode(), m_weight(-1) {}

        bool operator==(const BarcodeItem & other) const { return (m_barcode == other.m_barcode) && (m_weight == other.m_weight); }

        QString     m_barcode;
        int         m_weight;
    };

    QVector<BarcodeItem> barcodeItems() const;
    int numBarcodeItems() const;
    bool barcodeItemAt(const int barcodeItemIndex, BarcodeItem & barcodeItem) const;
    bool setBarcodeItemAt(const int barcodeItemIndex, const BarcodeItem & barcodeItem);
    void setBarcodeItems(const QVector<BarcodeItem> & barcodeItems);
    void reserveBarcodeItemsSpace(const int numItems);
    void addBarcodeItem(const BarcodeItem & item);
    bool removeBarcodeItem(const BarcodeItem & item);
    bool removeBarcodeItemAt(const int barcodeItemIndex);

    virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

private:
    QSharedDataPointer<ResourceRecognitionIndexItemData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDEX_ITEM_H
