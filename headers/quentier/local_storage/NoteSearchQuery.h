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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_H
#define LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_H

#include <quentier/types/ErrorString.h>

#include <QList>
#include <QSharedDataPointer>

namespace quentier {

class QUENTIER_EXPORT NoteSearchQuery : public Printable
{
public:
    explicit NoteSearchQuery();

    NoteSearchQuery(const NoteSearchQuery & other);
    NoteSearchQuery(NoteSearchQuery && other) noexcept;

    NoteSearchQuery & operator=(const NoteSearchQuery & other);
    NoteSearchQuery & operator=(NoteSearchQuery && other) noexcept;

    ~NoteSearchQuery() override;

    [[nodiscard]] bool isEmpty() const;

    void clear();

    /**
     * Returns the original non-parsed query string
     */
    [[nodiscard]] QString queryString() const;

    [[nodiscard]] bool setQueryString(
        const QString & queryString, ErrorString & error);

    /**
     * If query string has "notebook:<notebook name>" scope modifier,
     * this method returns the name of the notebook, otherwise it returns
     * empty string
     */
    [[nodiscard]] QString notebookModifier() const;

    [[nodiscard]] bool hasAnyModifier() const;

    [[nodiscard]] const QStringList & tagNames() const;
    [[nodiscard]] const QStringList & negatedTagNames() const;
    [[nodiscard]] bool hasAnyTag() const;
    [[nodiscard]] bool hasNegatedAnyTag() const;

    [[nodiscard]] const QStringList & titleNames() const;
    [[nodiscard]] const QStringList & negatedTitleNames() const;
    [[nodiscard]] bool hasAnyTitleName() const;
    [[nodiscard]] bool hasNegatedAnyTitleName() const;

    [[nodiscard]] const QList<qint64> & creationTimestamps() const;
    [[nodiscard]] const QList<qint64> & negatedCreationTimestamps() const;
    [[nodiscard]] bool hasAnyCreationTimestamp() const;
    [[nodiscard]] bool hasNegatedAnyCreationTimestamp() const;

    [[nodiscard]] const QList<qint64> & modificationTimestamps() const;
    [[nodiscard]] const QList<qint64> & negatedModificationTimestamps() const;
    [[nodiscard]] bool hasAnyModificationTimestamp() const;
    [[nodiscard]] bool hasNegatedAnyModificationTimestamp() const;

    [[nodiscard]] const QStringList & resourceMimeTypes() const;
    [[nodiscard]] const QStringList & negatedResourceMimeTypes() const;
    [[nodiscard]] bool hasAnyResourceMimeType() const;
    [[nodiscard]] bool hasNegatedAnyResourceMimeType() const;

    [[nodiscard]] const QList<qint64> & subjectDateTimestamps() const;
    [[nodiscard]] const QList<qint64> & negatedSubjectDateTimestamps() const;
    [[nodiscard]] bool hasAnySubjectDateTimestamp() const;
    [[nodiscard]] bool hasNegatedAnySubjectDateTimestamp() const;

    [[nodiscard]] const QList<double> & latitudes() const;
    [[nodiscard]] const QList<double> & negatedLatitudes() const;
    [[nodiscard]] bool hasAnyLatitude() const;
    [[nodiscard]] bool hasNegatedAnyLatitude() const;

    [[nodiscard]] const QList<double> & longitudes() const;
    [[nodiscard]] const QList<double> & negatedLongitudes() const;
    [[nodiscard]] bool hasAnyLongitude() const;
    [[nodiscard]] bool hasNegatedAnyLongitude() const;

    [[nodiscard]] const QList<double> & altitudes() const;
    [[nodiscard]] const QList<double> & negatedAltitudes() const;
    [[nodiscard]] bool hasAnyAltitude() const;
    [[nodiscard]] bool hasNegatedAnyAltitude() const;

    [[nodiscard]] const QStringList & authors() const;
    [[nodiscard]] const QStringList & negatedAuthors() const;
    [[nodiscard]] bool hasAnyAuthor() const;
    [[nodiscard]] bool hasNegatedAnyAuthor() const;

    [[nodiscard]] const QStringList & sources() const;
    [[nodiscard]] const QStringList & negatedSources() const;
    [[nodiscard]] bool hasAnySource() const;
    [[nodiscard]] bool hasNegatedAnySource() const;

    [[nodiscard]] const QStringList & sourceApplications() const;
    [[nodiscard]] const QStringList & negatedSourceApplications() const;
    [[nodiscard]] bool hasAnySourceApplication() const;
    [[nodiscard]] bool hasNegatedAnySourceApplication() const;

    [[nodiscard]] const QStringList & contentClasses() const;
    [[nodiscard]] const QStringList & negatedContentClasses() const;
    [[nodiscard]] bool hasAnyContentClass() const;
    [[nodiscard]] bool hasNegatedAnyContentClass() const;

    [[nodiscard]] const QStringList & placeNames() const;
    [[nodiscard]] const QStringList & negatedPlaceNames() const;
    [[nodiscard]] bool hasAnyPlaceName() const;
    [[nodiscard]] bool hasNegatedAnyPlaceName() const;

    [[nodiscard]] const QStringList & applicationData() const;
    [[nodiscard]] const QStringList & negatedApplicationData() const;
    [[nodiscard]] bool hasAnyApplicationData() const;
    [[nodiscard]] bool hasNegatedAnyApplicationData() const;

    [[nodiscard]] const QList<qint64> & reminderOrders() const;
    [[nodiscard]] const QList<qint64> & negatedReminderOrders() const;
    [[nodiscard]] bool hasAnyReminderOrder() const;
    [[nodiscard]] bool hasNegatedAnyReminderOrder() const;

    [[nodiscard]] const QList<qint64> & reminderTimes() const;
    [[nodiscard]] const QList<qint64> & negatedReminderTimes() const;
    [[nodiscard]] bool hasAnyReminderTime() const;
    [[nodiscard]] bool hasNegatedAnyReminderTime() const;

    [[nodiscard]] const QList<qint64> & reminderDoneTimes() const;
    [[nodiscard]] const QList<qint64> & negatedReminderDoneTimes() const;
    [[nodiscard]] bool hasAnyReminderDoneTime() const;
    [[nodiscard]] bool hasNegatedAnyReminderDoneTime() const;

    [[nodiscard]] bool hasUnfinishedToDo() const;
    [[nodiscard]] bool hasNegatedUnfinishedToDo() const;

    [[nodiscard]] bool hasFinishedToDo() const;
    [[nodiscard]] bool hasNegatedFinishedToDo() const;

    [[nodiscard]] bool hasAnyToDo() const;
    [[nodiscard]] bool hasNegatedAnyToDo() const;

    [[nodiscard]] bool hasEncryption() const;
    [[nodiscard]] bool hasNegatedEncryption() const;

    [[nodiscard]] const QStringList & contentSearchTerms() const;
    [[nodiscard]] const QStringList & negatedContentSearchTerms() const;
    [[nodiscard]] bool hasAnyContentSearchTerms() const;

    [[nodiscard]] bool isMatcheable() const;

    QTextStream & print(QTextStream & strm) const override;

private:
    class Data;
    QSharedDataPointer<Data> d;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const NoteSearchQuery & lhs, const NoteSearchQuery & rhs);

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const NoteSearchQuery & lhs, const NoteSearchQuery & rhs);

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_NOTE_SEARCH_QUERY_H
