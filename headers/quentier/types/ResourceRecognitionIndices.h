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

#ifndef LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDICES_H
#define LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDICES_H

#include <quentier/types/ResourceRecognitionIndexItem.h>

#include <QByteArray>
#include <QSharedDataPointer>
#include <QVector>

namespace quentier {

class ResourceRecognitionIndicesData;

class QUENTIER_EXPORT ResourceRecognitionIndices : public Printable
{
public:
    explicit ResourceRecognitionIndices();

    explicit ResourceRecognitionIndices(
        const QByteArray & rawRecognitionIndicesData);

    ResourceRecognitionIndices(const ResourceRecognitionIndices & other);
    ResourceRecognitionIndices(ResourceRecognitionIndices && other) noexcept;

    ResourceRecognitionIndices & operator=(
        const ResourceRecognitionIndices & other);

    ResourceRecognitionIndices & operator=(
        ResourceRecognitionIndices && other) noexcept;

    ~ResourceRecognitionIndices() override;

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isValid() const;

    [[nodiscard]] QString objectId() const;
    [[nodiscard]] QString objectType() const;
    [[nodiscard]] QString recoType() const;
    [[nodiscard]] QString engineVersion() const;
    [[nodiscard]] QString docType() const;
    [[nodiscard]] QString lang() const;

    [[nodiscard]] int objectHeight() const;
    [[nodiscard]] int objectWidth() const;

    [[nodiscard]] QVector<ResourceRecognitionIndexItem> items() const;

    bool setData(const QByteArray & rawRecognitionIndicesData);

    QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ResourceRecognitionIndicesData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_RESOURCE_RECOGNITION_INDICES_H
