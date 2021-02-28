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

#ifndef LIB_QUENTIER_TYPES_SAVED_SEARCH_H
#define LIB_QUENTIER_TYPES_SAVED_SEARCH_H

#include "IFavoritableDataElement.h"

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SavedSearchData)

class QUENTIER_EXPORT SavedSearch : public IFavoritableDataElement
{
public:
    QN_DECLARE_LOCAL_UID
    QN_DECLARE_DIRTY
    QN_DECLARE_LOCAL
    QN_DECLARE_FAVORITED

public:
    using QueryFormat = qevercloud::QueryFormat;
    using SavedSearchScope = qevercloud::SavedSearchScope;

public:
    explicit SavedSearch();
    SavedSearch(const SavedSearch & other);
    SavedSearch(SavedSearch && other);
    SavedSearch & operator=(const SavedSearch & other);
    SavedSearch & operator=(SavedSearch && other);

    explicit SavedSearch(const qevercloud::SavedSearch & search);
    explicit SavedSearch(qevercloud::SavedSearch && search);

    virtual ~SavedSearch() override;

    const qevercloud::SavedSearch & qevercloudSavedSearch() const;
    qevercloud::SavedSearch & qevercloudSavedSearch();

    bool operator==(const SavedSearch & other) const;
    bool operator!=(const SavedSearch & other) const;

    virtual void clear() override;

    static bool validateName(
        const QString & name, ErrorString * pErrorDescription = nullptr);

    virtual bool hasGuid() const override;
    virtual const QString & guid() const override;
    virtual void setGuid(const QString & guid) override;

    virtual bool hasUpdateSequenceNumber() const override;
    virtual qint32 updateSequenceNumber() const override;
    virtual void setUpdateSequenceNumber(const qint32 usn) override;

    virtual bool checkParameters(ErrorString & errorDescription) const override;

    bool hasName() const;
    const QString & name() const;
    void setName(const QString & name);

    bool hasQuery() const;
    const QString & query() const;
    void setQuery(const QString & query);

    bool hasQueryFormat() const;
    QueryFormat queryFormat() const;
    void setQueryFormat(const qint8 queryFormat);

    bool hasIncludeAccount() const;
    bool includeAccount() const;
    void setIncludeAccount(const bool includeAccount);

    bool hasIncludePersonalLinkedNotebooks() const;
    bool includePersonalLinkedNotebooks() const;

    void setIncludePersonalLinkedNotebooks(
        const bool includePersonalLinkedNotebooks);

    bool hasIncludeBusinessLinkedNotebooks() const;
    bool includeBusinessLinkedNotebooks() const;

    void setIncludeBusinessLinkedNotebooks(
        const bool includeBusinessLinkedNotebooks);

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<SavedSearchData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::SavedSearch)

#endif // LIB_QUENTIER_TYPES_SAVED_SEARCH_H
