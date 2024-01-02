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

#include <QByteArray>
#include <QSharedData>
#include <QString>

class QXmlStreamAttributes;

namespace quentier {

class ResourceRecognitionIndicesData final : public QSharedData
{
public:
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool setData(const QByteArray & rawRecognitionIndicesData);

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
