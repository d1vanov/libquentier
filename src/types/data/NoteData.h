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

#ifndef LIB_QUENTIER_TYPES_DATA_NOTE_DATA_H
#define LIB_QUENTIER_TYPES_DATA_NOTE_DATA_H

#include "FavoritableDataElementData.h"

#include <quentier/types/ErrorString.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QByteArray>

namespace quentier {

class Q_DECL_HIDDEN NoteData final : public FavoritableDataElementData
{
public:
    NoteData();

    NoteData(const NoteData & other) = default;
    NoteData(NoteData && other) = default;

    NoteData(const qevercloud::Note & other);

    NoteData & operator=(const NoteData & other) = delete;
    NoteData & operator=(NoteData && other) = delete;

    virtual ~NoteData() override = default;

    void clear();
    bool checkParameters(ErrorString & errorDescription) const;

    QString plainText(ErrorString * pErrorMessage) const;
    QStringList listOfWords(ErrorString * pErrorMessage) const;

    std::pair<QString, QStringList> plainTextAndListOfWords(
        ErrorString * pErrorMessage) const;

    bool containsToDoImpl(const bool checked) const;
    bool containsEncryption() const;

    void setContent(const QString & content);

public:
    struct Q_DECL_HIDDEN ResourceAdditionalInfo
    {
        QString localUid;
        bool isDirty = false;

        bool operator==(const ResourceAdditionalInfo & other) const;
    };

public:
    qevercloud::Note m_qecNote;
    QList<ResourceAdditionalInfo> m_resourcesAdditionalInfo;
    qevercloud::Optional<QString> m_notebookLocalUid;
    QStringList m_tagLocalUids;
    QByteArray m_thumbnailData;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_NOTE_DATA_H
