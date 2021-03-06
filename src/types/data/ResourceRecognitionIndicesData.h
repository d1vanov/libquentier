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

#ifndef LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H
#define LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H

#include <quentier/types/ResourceRecognitionIndexItem.h>

#include "ResourceRecognitionIndexItemData.h"

#include <QByteArray>
#include <QSharedData>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QXmlStreamAttributes)

namespace quentier {

class Q_DECL_HIDDEN ResourceRecognitionIndicesData final : public QSharedData
{
public:
    bool isValid() const;
    bool setData(const QByteArray & rawRecognitionIndicesData);

private:
    void clear();
    void restoreFrom(const ResourceRecognitionIndicesData & data);

    void parseRecoIndexAttributes(const QXmlStreamAttributes & attributes);

    void parseCommonItemAttributes(
        const QXmlStreamAttributes & attributes,
        ResourceRecognitionIndexItem & item) const;

    void parseTextItemAttributesAndData(
        const QXmlStreamAttributes & attributes, const QString & data,
        ResourceRecognitionIndexItem & item) const;

    void parseObjectItemAttributes(
        const QXmlStreamAttributes & attributes,
        ResourceRecognitionIndexItem & item) const;

    void parseShapeItemAttributes(
        const QXmlStreamAttributes & attributes,
        ResourceRecognitionIndexItem & item) const;

    void parseBarcodeItemAttributesAndData(
        const QXmlStreamAttributes & attributes, const QString & data,
        ResourceRecognitionIndexItem & item) const;

public:
    bool m_isNull = true;

    QString m_objectId;
    QString m_objectType;
    QString m_recoType;
    QString m_engineVersion;
    QString m_docType;
    QString m_lang;

    int m_objectHeight = -1;
    int m_objectWidth = -1;

    QVector<ResourceRecognitionIndexItem> m_items;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H
