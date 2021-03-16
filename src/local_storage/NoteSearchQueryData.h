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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_DATA_H
#define LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_DATA_H

#include <quentier/types/ErrorString.h>

#include <QSharedData>
#include <QStringList>

namespace quentier {

class Q_DECL_HIDDEN NoteSearchQueryData final :
    public QSharedData,
    public Printable
{
public:
    NoteSearchQueryData();
    NoteSearchQueryData(const NoteSearchQueryData & other);

    void clear();

    bool parseQueryString(const QString & queryString, ErrorString & error);

    virtual QTextStream & print(QTextStream & strm) const override;

    QString m_queryString;
    QString m_notebookModifier;
    bool m_hasAnyModifier = false;
    QStringList m_tagNames;
    QStringList m_negatedTagNames;
    bool m_hasAnyTag = false;
    bool m_hasNegatedAnyTag = false;
    QStringList m_titleNames;
    QStringList m_negatedTitleNames;
    bool m_hasAnyTitleName = false;
    bool m_hasNegatedAnyTitleName = false;
    QVector<qint64> m_creationTimestamps;
    QVector<qint64> m_negatedCreationTimestamps;
    bool m_hasAnyCreationTimestamp = false;
    bool m_hasNegatedAnyCreationTimestamp = false;
    QVector<qint64> m_modificationTimestamps;
    QVector<qint64> m_negatedModificationTimestamps;
    bool m_hasAnyModificationTimestamp = false;
    bool m_hasNegatedAnyModificationTimestamp = false;
    QStringList m_resourceMimeTypes;
    QStringList m_negatedResourceMimeTypes;
    bool m_hasAnyResourceMimeType = false;
    bool m_hasNegatedAnyResourceMimeType = false;
    QVector<qint64> m_subjectDateTimestamps;
    QVector<qint64> m_negatedSubjectDateTimestamps;
    bool m_hasAnySubjectDateTimestamp = false;
    bool m_hasNegatedAnySubjectDateTimestamp = false;
    QVector<double> m_latitudes;
    QVector<double> m_negatedLatitudes;
    bool m_hasAnyLatitude = false;
    bool m_hasNegatedAnyLatitude = false;
    QVector<double> m_longitudes;
    QVector<double> m_negatedLongitudes;
    bool m_hasAnyLongitude = false;
    bool m_hasNegatedAnyLongitude = false;
    QVector<double> m_altitudes;
    QVector<double> m_negatedAltitudes;
    bool m_hasAnyAltitude = false;
    bool m_hasNegatedAnyAltitude = false;
    QStringList m_authors;
    QStringList m_negatedAuthors;
    bool m_hasAnyAuthor = false;
    bool m_hasNegatedAnyAuthor = false;
    QStringList m_sources;
    QStringList m_negatedSources;
    bool m_hasAnySource = false;
    bool m_hasNegatedAnySource = false;
    QStringList m_sourceApplications;
    QStringList m_negatedSourceApplications;
    bool m_hasAnySourceApplication = false;
    bool m_hasNegatedAnySourceApplication = false;
    QStringList m_contentClasses;
    QStringList m_negatedContentClasses;
    bool m_hasAnyContentClass = false;
    bool m_hasNegatedAnyContentClass = false;
    QStringList m_placeNames;
    QStringList m_negatedPlaceNames;
    bool m_hasAnyPlaceName = false;
    bool m_hasNegatedAnyPlaceName = false;
    QStringList m_applicationData;
    QStringList m_negatedApplicationData;
    bool m_hasAnyApplicationData = false;
    bool m_hasNegatedAnyApplicationData = false;
    QVector<qint64> m_reminderOrders;
    QVector<qint64> m_negatedReminderOrders;
    bool m_hasAnyReminderOrder = false;
    bool m_hasNegatedAnyReminderOrder = false;
    QVector<qint64> m_reminderTimes;
    QVector<qint64> m_negatedReminderTimes;
    bool m_hasAnyReminderTime = false;
    bool m_hasNegatedAnyReminderTime = false;
    QVector<qint64> m_reminderDoneTimes;
    QVector<qint64> m_negatedReminderDoneTimes;
    bool m_hasAnyReminderDoneTime = false;
    bool m_hasNegatedAnyReminderDoneTime = false;
    bool m_hasUnfinishedToDo = false;
    bool m_hasNegatedUnfinishedToDo = false;
    bool m_hasFinishedToDo = false;
    bool m_hasNegatedFinishedToDo = false;
    bool m_hasAnyToDo = false;
    bool m_hasNegatedAnyToDo = false;
    bool m_hasEncryption = false;
    bool m_hasNegatedEncryption = false;
    QStringList m_contentSearchTerms;
    QStringList m_negatedContentSearchTerms;

    bool isMatcheable() const;

private:
    QStringList splitSearchQueryString(const QString & searchQueryString) const;

    void parseStringValue(
        const QString & key, QStringList & words, QStringList & container,
        QStringList & negatedContainer, bool & hasAnyValue,
        bool & hasNegatedAnyValue) const;

    bool parseIntValue(
        const QString & key, QStringList & words, QVector<qint64> & container,
        QVector<qint64> & negatedContainer, bool & hasAnyValue,
        bool & hasNegatedAnyValue, ErrorString & error) const;

    bool parseDoubleValue(
        const QString & key, QStringList & words, QVector<double> & container,
        QVector<double> & negatedContainer, bool & hasAnyValue,
        bool & hasNegatedAnyValue, ErrorString & error) const;

    QDateTime parseDateTime(const QString & str) const;

    bool dateTimeStringToTimestamp(
        QString dateTimeString, qint64 & timestamp, ErrorString & error) const;

    bool convertAbsoluteAndRelativeDateTimesToTimestamps(
        QStringList & words, ErrorString & error) const;

    void removeBoundaryQuotesFromWord(QString & word) const;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_DATA_H
