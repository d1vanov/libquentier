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

#ifndef LIB_QUENTIER_UTILITY_APPLICATION_SETTINGS_H
#define LIB_QUENTIER_UTILITY_APPLICATION_SETTINGS_H

#include <quentier/types/Account.h>

#include <QSettings>

namespace quentier {

/**
 * @brief The ApplicationSettings class enhances the functionality of QSettings,
 * in particular it simplifies the way of working with either application-wide
 * or account-specific settings
 */
class QUENTIER_EXPORT ApplicationSettings : public QSettings, public Printable
{
    Q_OBJECT
public:
    /**
     * Constructor for application settings not being account-specific
     *
     * @param settingsName  If not empty, the created application settings
     *                      would manage the settings stored in a file with
     *                      a specific name within the common settings
     *                      storage; otherwise they would be stored in
     *                      the default settings file for the account
     */
    ApplicationSettings(const QString & settingsName = {});

    /**
     * Constructor for application settings specific to the account
     *
     * @param account       The account for which the settings are to be stored
     *                      or read
     * @param settingsName  If not empty, the created application settings
     *                      would manage the settings stored in a file with
     *                      a specific name within the account's settings
     *                      storage; otherwise they would be stored in
     *                      the default settings file for the account
     */
    ApplicationSettings(
        const Account & account, const QString & settingsName = {});

    /**
     * Constructor for application settings specific to the account
     *
     * @param account           The account for which the settings are to be
     *                          stored or read
     * @param settingsName      If not nullptr, the created application settings
     *                          would manage the settings stored in a file with
     *                          a specific name within the account's settings
     *                          storage; otherwise they would be stored in
     *                          the default settings file for the account.
     *                          Must be UTF-8 encoded as internally it is
     *                          converted to QString via QString::fromUtf8
     * @param settingsNameSize  Size of the settingsName string. If negative
     *                          (the default), the settingsName size is taken
     *                          to be strlen(settingsName)
     */
    ApplicationSettings(
        const Account & account, const char * settingsName,
        const int settingsNameSize = -1);

    /**
     * Destructor
     */
    virtual ~ApplicationSettings() override;

public:
    /**
     * Helper struct for RAII style of ensuring the array once began would be
     * closed even if exception is thrown after beginning the array
     */
    struct ArrayCloser
    {
        ArrayCloser(ApplicationSettings & settings) : m_settings(settings) {}

        ~ArrayCloser()
        {
            m_settings.endArray();
            m_settings.sync();
        }

        ApplicationSettings & m_settings;
    };

    /**
     * Helper struct for RAII style of ensuring the group once opened would be
     * closed even if exception is thrown after beginning the group
     */
    struct GroupCloser
    {
        GroupCloser(ApplicationSettings & settings) : m_settings(settings) {}

        ~GroupCloser()
        {
            m_settings.endGroup();
            m_settings.sync();
        }

        ApplicationSettings & m_settings;
    };

public:
    /**
     * Appends prefix to the current group.
     * The call is redirected to QSettings::beginGroup. It is required in this
     * class only to workaround hiding QSettings method due to overloads
     * @param prefix    String containing the prefix name
     */
    void beginGroup(const QString & prefix);

    /**
     * Appends prefix to the current group.
     * Overload of beginGroup accepting const char * and optionally the size of
     * the string
     * @param prefix    String containing the prefix name. Must be UTF-8
     *                  encoded as internally it is converted to QString via
     *                  QString::fromUtf8
     * @param size      Size of the prefix sring. If negative (the default),
     *                  the prefix size is taken to be stren(prefix).
     */
    void beginGroup(const char * prefix, const int size = -1);

    /**
     * Adds prefix to the current group and starts reading from an array.
     * The call is redirected to QSettings::beginReadArray. It is required in
     * this class only to workaround hiding QSettings method due to overloads
     * @param prefix    String containing the prefix name
     * @return          The size of the array
     */
    int beginReadArray(const QString & prefix);

    /**
     * Adds prefix to the current group and starts reading from an array.
     * Overload of beginReadArray accepting const char * and optionally
     * the size of the string
     * @param prefix    String containing the prefix name. Must be UTF-8
     *                  encoded as internally it is converted to QString via
     *                  QString::fromUtf8
     * @param size      Size of the prefix sring. If negative (the default),
     *                  the prefix size is taken to be stren(prefix)
     */
    int beginReadArray(const char * prefix, const int size = -1);

    /**
     * Adds prefix to the current group and starts writing an array of size
     * arraySize.
     * The call is redirected to QSettings::beginWriteArray. It is required in
     * this class only to workaround hiding QSettings method due to overloads
     * @param prefix    String containing the prefix name
     * @param arraySize Size of the array to be written. If negative
     *                  (the default), it is automatically determined based on
     *                  the indexes of the entries written.
     */
    void beginWriteArray(const QString & prefix, const int arraySize = -1);

    /**
     * Adds prefix to the current group and starts writing an array of size
     * arraySize.
     * Overload of beginWriteArray accepting const char * and optionally
     * the size of the string
     * @param prefix        String containing the prefix name. Must be UTF-8
     *                      encoded as internally it is converted to QString via
     *                      QString::fromUtf8
     * @param arraySize     Size of the array to be written. If negative
     *                      (the default), it is automatically determined based
     *                      on the indexes of the entries written.
     * @param prefixSize    Size of the prefix sring. If negative (the default),
     *                      the prefix size is taken to be stren(prefix)
     */
    void beginWriteArray(
        const char * prefix, const int arraySize = -1,
        const int prefixSize = -1);

    /**
     * The call is redirected to QSettings::contains. It is required in
     * this class only to workaround hiding QSettings method due to overloads
     * @param key       The key being checked for presence
     * @return          True if there exists a setting called key; false
     *                  otherwise
     */
    bool contains(const QString & key) const;

    /**
     * Overload of contains accepting const char * and optionally the size of
     * the string
     * @param key       String containing the setting name. Must be UTF-8
     *                  encoded as internally it is converted to QString via
     *                  QString::fromUtf8
     * @param size      Size of the key sring. If negative (the default),
     *                  the key size is taken to be stren(key)
     * @return          True if there exists a setting called key; false
     *                  otherwise
     */
    bool contains(const char * key, const int size = -1) const;

    /**
     * Removes the setting key and any sub-settings of key.
     * The call is redirected to QSettings::remove. It is required in
     * this class only to workaround hiding QSettings method due to overloads
     * @param key       String containing the setting name
     */
    void remove(const QString & key);

    /**
     * Removes the setting key and any sub-settings of key.
     * Overload of remove accepting const char * and optionally the size of
     * the string
     * @param key       String containing the setting name. Must be UTF-8
     *                  encoded as internally it is converted to QString via
     *                  QString::fromUtf8
     * @param size      Size of the key sring. If negative (the default),
     *                  the key size is taken to be stren(key).
     */
    void remove(const char * key, const int size = -1);

    /**
     * Sets the value of setting.
     * The call is redirected to QSettings::setValue. It is required in this
     * class only to workaround hiding QSettings method due to overloads
     * @param key       String containing the setting name
     * @param value     Value for setting key
     */
    void setValue(const QString & key, const QVariant & value);

    /**
     * Sets the value of setting.
     * Overload of setValue accepting const char * and optionally the size of
     * the string
     * @param key       String containing the setting name. Must be UTF-8
     *                  encoded as internally it is converted to QString via
     *                  QString::fromUtf8
     * @param value     Value for setting key
     * @param keySize   Size of the key string. If negative (the default),
     *                  the key size is taken to be strlen(key).
     */
    void setValue(
        const char * key, const QVariant & value, const int keySize = -1);

    /**
     * Fetches the value of setting.
     * The call is redirected to QSettings::value. It is required in this class
     * only to workaround hiding QSettings method due to overloads
     * @param key           String containing the setting name
     * @param defautValue   Default value returned if the setting doesn't exist
     * @return              The value for setting key. If the setting doesn't
     *                      exist, returns defaultValue. If no default value is
     *                      specified, a default QVariant is returned.
     */
    QVariant value(
        const QString & key, const QVariant & defaultValue = {}) const;

    /**
     * Fetches the value of setting.
     * Overload of value accepting const char * and optionally
     * the size of the string
     * @param key           String containing the setting name. Must be UTF-8
     *                      encoded as internally it is converted to QString via
     *                      QString::fromUtf8
     * @param defautValue   Default value returned if the setting doesn't exist
     * @param keySize       Size of the key sring. If negative (the default),
     *                      the key size is taken to be stren(key)
     * @return              The value for setting key. If the setting doesn't
     *                      exist, returns defaultValue. If no default value is
     *                      specified, a default QVariant is returned.
     */
    QVariant value(
        const char * key, const QVariant & defaultValue = {},
        const int keySize = -1) const;

public:
    virtual QTextStream & print(QTextStream & strm) const override;

private:
    Q_DISABLE_COPY(ApplicationSettings)
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_APPLICATION_SETTINGS_H
