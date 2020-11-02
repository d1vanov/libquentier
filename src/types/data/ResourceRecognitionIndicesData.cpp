/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "ResourceRecognitionIndicesData.h"

#include "ResourceRecognitionIndexItemData.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QStringList>
#include <QXmlStreamReader>

namespace quentier {

bool ResourceRecognitionIndicesData::isValid() const
{
    if (m_objectId.isEmpty()) {
        QNTRACE(
            "types:data",
            "Resource recognition indices' object id is not "
                << "set");
        return false;
    }

    if (m_objectType.isEmpty()) {
        QNTRACE(
            "types:data",
            "Resource recognition indices' object type is "
                << "not set");
        return false;
    }
    else if (
        (m_objectType != QStringLiteral("image")) &&
        (m_objectType != QStringLiteral("ink")) &&
        (m_objectType != QStringLiteral("audio")) &&
        (m_objectType != QStringLiteral("video")) &&
        (m_objectType != QStringLiteral("document")))
    {
        QNTRACE(
            "types:data",
            "Resource recognition indices' object type is "
                << "not valid");
        return false;
    }

    if (m_recoType.isEmpty()) {
        QNTRACE(
            "types:data",
            "Resource recognition indices' recognition type "
                << "is not set");
        return false;
    }
    else if (
        (m_recoType != QStringLiteral("service")) &&
        (m_recoType != QStringLiteral("client")))
    {
        QNTRACE(
            "types:data",
            "Resource recognition indices' recognition type "
                << "is not valid");
        return false;
    }

    if (m_docType.isEmpty()) {
        QNTRACE(
            "types:data",
            "Resource recognition indices' doc type is not "
                << "set");
        return false;
    }
    else if (
        (m_docType != QStringLiteral("printed")) &&
        (m_docType != QStringLiteral("speech")) &&
        (m_docType != QStringLiteral("handwritten")) &&
        (m_docType != QStringLiteral("picture")) &&
        (m_docType != QStringLiteral("unknown")))
    {
        QNTRACE(
            "types:data",
            "Resource recognition indices' doc type is not "
                << "valid");
        return false;
    }

    return true;
}

bool ResourceRecognitionIndicesData::setData(
    const QByteArray & rawRecognitionIndicesData)
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::setData: "
            << rawRecognitionIndicesData);

    if (rawRecognitionIndicesData.isEmpty()) {
        QNTRACE("types:data", "Recognition data is empty");
        clear();
        return true;
    }

    QXmlStreamReader reader(rawRecognitionIndicesData);

    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    ResourceRecognitionIndicesData backup(*this);
    clear();

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            if (lastElementName == QStringLiteral("recoIndex")) {
                parseRecoIndexAttributes(lastElementAttributes);
                continue;
            }

            if (lastElementName == QStringLiteral("item")) {
                ResourceRecognitionIndexItem item;
                parseCommonItemAttributes(lastElementAttributes, item);
                m_items << item;
                continue;
            }

            if (m_items.isEmpty()) {
                continue;
            }

            ResourceRecognitionIndexItem & item = m_items.last();

            if (lastElementName == QStringLiteral("object")) {
                parseObjectItemAttributes(lastElementAttributes, item);
            }
            else if (lastElementName == QStringLiteral("shape")) {
                parseShapeItemAttributes(lastElementAttributes, item);
            }
            else {
                continue;
            }
        }

        if (reader.isCharacters()) {
            QString chars = reader.text().toString().simplified();
            if (chars.isEmpty()) {
                continue;
            }

            if (m_items.isEmpty()) {
                continue;
            }

            ResourceRecognitionIndexItem & item = m_items.last();

            if (lastElementName == QStringLiteral("t")) {
                parseTextItemAttributesAndData(
                    lastElementAttributes, chars, item);
            }
            else if (lastElementName == QStringLiteral("barcode")) {
                parseBarcodeItemAttributesAndData(
                    lastElementAttributes, chars, item);
            }
            else {
                continue;
            }
        }
    }

    if (reader.hasError()) {
        QNWARNING(
            "types:data",
            "Failed to parse resource recognition indices "
                << "data: " << reader.errorString() << " (error code "
                << reader.error()
                << ", original raw data: " << rawRecognitionIndicesData);
        restoreFrom(backup);
        return false;
    }

    m_isNull = false;
    QNTRACE("types:data", "Successfully parsed ResourceRecognitionIndicesData");
    return true;
}

void ResourceRecognitionIndicesData::clear()
{
    QNTRACE("types:data", "ResourceRecognitionIndicesData::clear");

    m_objectId.resize(0);
    m_objectType.resize(0);
    m_recoType.resize(0);
    m_engineVersion.resize(0);
    m_docType.resize(0);
    m_lang.resize(0);
    m_objectHeight = -1;
    m_objectWidth = -1;
    m_items.clear();

    m_isNull = true;
}

void ResourceRecognitionIndicesData::restoreFrom(
    const ResourceRecognitionIndicesData & data)
{
    QNTRACE("types:data", "ResourceRecognitionIndicesData::restoreFrom");

    m_isNull = data.m_isNull;

    m_objectId = data.m_objectId;
    m_objectType = data.m_objectType;
    m_recoType = data.m_recoType;
    m_engineVersion = data.m_engineVersion;
    m_docType = data.m_docType;
    m_lang = data.m_lang;
    m_objectHeight = data.m_objectHeight;
    m_objectWidth = data.m_objectWidth;
    m_items = data.m_items;
}

void ResourceRecognitionIndicesData::parseRecoIndexAttributes(
    const QXmlStreamAttributes & attributes)
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseRecoIndexAttributes");

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("objID")) {
            m_objectId = value.toString();
        }
        else if (name == QStringLiteral("objType")) {
            m_objectType = value.toString();
        }
        else if (name == QStringLiteral("recoType")) {
            m_recoType = value.toString();
        }
        else if (name == QStringLiteral("engineVersion")) {
            m_engineVersion = value.toString();
        }
        else if (name == QStringLiteral("docType")) {
            m_docType = value.toString();
        }
        else if (name == QStringLiteral("lang")) {
            m_lang = value.toString();
        }
        else if (name == QStringLiteral("objHeight")) {
            bool conversionResult = false;
            int objectHeight = value.toInt(&conversionResult);
            if (conversionResult) {
                m_objectHeight = objectHeight;
            }
        }
        else if (name == QStringLiteral("objWidth")) {
            bool conversionResult = false;
            int objectWidth = value.toInt(&conversionResult);
            if (conversionResult) {
                m_objectWidth = objectWidth;
            }
        }
    }
}

void ResourceRecognitionIndicesData::parseCommonItemAttributes(
    const QXmlStreamAttributes & attributes,
    ResourceRecognitionIndexItem & item) const
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseCommonItemAttributes");

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("x")) {
            bool conversionResult = false;
            int x = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setX(x);
            }
        }
        else if (name == QStringLiteral("y")) {
            bool conversionResult = false;
            int y = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setY(y);
            }
        }
        else if (name == QStringLiteral("h")) {
            bool conversionResult = false;
            int h = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setH(h);
            }
        }
        else if (name == QStringLiteral("w")) {
            bool conversionResult = false;
            int w = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setW(w);
            }
        }
        else if (name == QStringLiteral("offset")) {
            bool conversionResult = false;
            int offset = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setOffset(offset);
            }
        }
        else if (name == QStringLiteral("duration")) {
            bool conversionResult = false;
            int duration = value.toInt(&conversionResult);
            if (conversionResult) {
                item.setDuration(duration);
            }
        }
        else if (name == QStringLiteral("strokeList")) {
            QString valueStr = value.toString();
            QStringList valueList = valueStr.split(
                QStringLiteral(","),
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                Qt::SkipEmptyParts);
#else
                QString::SkipEmptyParts);
#endif
            const int numValues = valueList.size();
            for (int i = 0; i < numValues; ++i) {
                const QString & strokeStr = valueList[i];
                bool conversionResult = false;
                int stroke = strokeStr.toInt(&conversionResult);
                if (conversionResult) {
                    item.addStroke(stroke);
                }
            }
        }
    }
}

void ResourceRecognitionIndicesData::parseTextItemAttributesAndData(
    const QXmlStreamAttributes & attributes, const QString & data,
    ResourceRecognitionIndexItem & item) const
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseTextItemAttributesAndData: "
            << "data = " << data);

    int weight = -1;

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("w")) {
            bool conversionResult = false;
            int parsedWeight = value.toInt(&conversionResult);
            if (conversionResult) {
                weight = parsedWeight;
            }
        }
    }

    if (weight < 0) {
        return;
    }

    ResourceRecognitionIndexItem::TextItem textItem;
    textItem.m_weight = weight;
    textItem.m_text = data;
    item.addTextItem(textItem);

    QNTRACE(
        "types:data",
        "Added text item: text = " << data << "; weight = " << weight);
}

void ResourceRecognitionIndicesData::parseObjectItemAttributes(
    const QXmlStreamAttributes & attributes,
    ResourceRecognitionIndexItem & item) const
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseObjectItemAttributes");

    QString objectType;
    int weight = -1;

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("type")) {
            objectType = value.toString();
        }
        else if (name == QStringLiteral("w")) {
            bool conversionResult = false;
            int parsedWeight = value.toString().toInt(&conversionResult);
            if (conversionResult) {
                weight = parsedWeight;
            }
        }
    }

    if (weight < 0) {
        return;
    }

    ResourceRecognitionIndexItem::ObjectItem objectItem;
    objectItem.m_objectType = objectType;
    objectItem.m_weight = weight;
    item.addObjectItem(objectItem);

    QNTRACE(
        "types:data",
        "Added object item: type = " << objectType << ", weight = " << weight);
}

void ResourceRecognitionIndicesData::parseShapeItemAttributes(
    const QXmlStreamAttributes & attributes,
    ResourceRecognitionIndexItem & item) const
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseShapeItemAttributes");

    QString shapeType;
    int weight = -1;

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("type")) {
            shapeType = value.toString();
        }
        else if (name == QStringLiteral("w")) {
            bool conversionResult = false;
            int parsedWeight = value.toInt(&conversionResult);
            if (conversionResult) {
                weight = parsedWeight;
            }
        }
    }

    if (weight < 0) {
        return;
    }

    ResourceRecognitionIndexItem::ShapeItem shapeItem;
    shapeItem.m_shapeType = shapeType;
    shapeItem.m_weight = weight;
    item.addShapeItem(shapeItem);

    QNTRACE(
        "types:data",
        "Added shape item: type = " << shapeType << ", weight = " << weight);
}

void ResourceRecognitionIndicesData::parseBarcodeItemAttributesAndData(
    const QXmlStreamAttributes & attributes, const QString & data,
    ResourceRecognitionIndexItem & item) const
{
    QNTRACE(
        "types:data",
        "ResourceRecognitionIndicesData::parseBarcodeItemAttributesAndData: "
            << data);

    int weight = -1;

    for (const auto & attribute: qAsConst(attributes)) {
        const QStringRef & name = attribute.name();
        const QStringRef & value = attribute.value();

        if (name == QStringLiteral("w")) {
            bool conversionResult = false;
            int parsedWeight = value.toInt(&conversionResult);
            if (conversionResult) {
                weight = parsedWeight;
            }
        }
    }

    if (weight < 0) {
        return;
    }

    ResourceRecognitionIndexItem::BarcodeItem barcodeItem;
    barcodeItem.m_weight = weight;
    barcodeItem.m_barcode = data;
    item.addBarcodeItem(barcodeItem);

    QNTRACE(
        "types:data",
        "Added barcode item: barcode = " << data << "; weight = " << weight);
}

} // namespace quentier
