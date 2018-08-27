/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_DATABASE_UPGRADER_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_DATABASE_UPGRADER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/ApplicationSettings.h>
#include <QObject>
#include <QSet>

QT_FORWARD_DECLARE_CLASS(QSqlDatabase)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

/**
 * @brief The LocalStorageDatabaseUpgrader encapsulates the logic of local storage database
 * upgrades which occur when the database schema needs to be changed (between different releases of libquentier)
 */
class Q_DECL_HIDDEN LocalStorageDatabaseUpgrader: public QObject
{
    Q_OBJECT
public:
    explicit LocalStorageDatabaseUpgrader(const Account & account,
                                          LocalStorageManagerPrivate & localStorageManager,
                                          QSqlDatabase & database,
                                          QObject * parent = Q_NULLPTR);

    /**
     * @brief upgradeDatabase method performs the upgrade of local storage database if upgrade is required; upgradeProgress
     * signal is emitted to inform any listeners of the progress of the upgrade
     * @param errorDescription      Textual description of the error if the database upgrade failed,
     *                              otherwise this parameter is not touched by the method
     * @return true if database was upgraded successfully, false if database was not upgraded
     * either due to error or due to the fact the database doesn't require an upgrade
     */
    bool upgradeDatabase(ErrorString & errorDescription);

Q_SIGNALS:
    void upgradeProgress(double progress);

private:
    int currentDatabaseVersion(ErrorString & errorDescription) const;

    bool upgradeDatabaseFromVersion1ToVersion2(ErrorString & errorDescription);

    // Helper methods for upgrading the database from version 1 to version 2
    QStringList listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription);
    void filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(QStringList & resourceLocalUids);
    bool ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(LocalStorageDatabaseUpgrader)

private:
    struct StringFilterPredicate
    {
        StringFilterPredicate(QSet<QString> & filteredStrings) : m_filteredStrings(filteredStrings) {}

        bool operator()(const QString & str) const
        {
            return m_filteredStrings.contains(str);
        }

        QSet<QString> &     m_filteredStrings;
    };

    struct ApplicationSettingsArrayCloser
    {
        ApplicationSettingsArrayCloser(ApplicationSettings & settings) : m_settings(settings) {}
        ~ApplicationSettingsArrayCloser() { m_settings.endArray(); m_settings.sync(); }

        ApplicationSettings & m_settings;
    };

private:
    Account                         m_account;
    LocalStorageManagerPrivate &    m_localStorageManager;
    QSqlDatabase &                  m_sqlDatabase;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_DATABASE_UPGRADER_H
