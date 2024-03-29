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

#ifndef LIB_QUENTIER_NOTE_EDITOR_I_NOTE_EDITOR_RESOURCE_PLUGIN_H
#define LIB_QUENTIER_NOTE_EDITOR_I_NOTE_EDITOR_RESOURCE_PLUGIN_H

#include <quentier/types/ErrorString.h>

#include <QWidget>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(NoteEditorPluginFactory)

/**
 * @brief The INoteEditorResourcePlugin class is the interface for note editor
 * plugin implementing the widget displaying the resources of certain mime types
 * built in the note. For example, such plugin could represent the embedded pdf
 * viewer, embedded video viewer etc.
 */
class Q_DECL_HIDDEN INoteEditorResourcePlugin : public QWidget
{
    Q_OBJECT
protected:
    explicit INoteEditorResourcePlugin(QWidget * parent = nullptr);

public:
    /**
     * @brief clone is pure virtual method cloning the current plugin
     * @return pointer to the new clone of the plugin. NOTE: it is
     * caller's responsibility to take care about the ownership
     * of the returned pointer
     */
    virtual INoteEditorResourcePlugin * clone() const = 0;

    /**
     * @brief initialize is the method used to initialize the note editor plugin
     * @param mimeType          Mime type of the resource data meant to be
     *                          displayed by the plugin
     * @param parameterNames    Names of string parameters stored in HTML
     *                          <object> tag for the plugin
     * @param parameterValues   Values of string parameters stored in HTML
     *                          <object> tag for the plugin
     * @param pluginFactory     Plugin factory object which initializes plugins;
     *                          here intended to be used for setting up
     *                          the signal-slot connections, if necessary
     * @param resource          Const reference to the resource which needs to
     *                          be displayed by the plugin
     * @param errorDescription  Error description in case the plugin can't be
     *                          initialized properly with this set of parameters
     * @return                  True if initialization was successful,
     *                          false otherwise
     */
    virtual bool initialize(
        const QString & mimeType, const QStringList & parameterNames,
        const QStringList & parameterValues,
        const NoteEditorPluginFactory & pluginFactory,
        const Resource & resource, ErrorString & errorDescription) = 0;

    /**
     * @brief mimeTypes         The method telling which are the mime types of
     *                          the resources the plugin can work with
     * @return                  The list of resource mime types the plugin
     *                          supports
     */
    virtual QStringList mimeTypes() const = 0;

    /**
     * @brief fileExtensions is the method telling which file extensions of
     * the data the plugin should be able to handle mapped to mime types
     * the plugin supports as well
     *
     * @return                  The hash of file extensions the plugin supports
     *                          per mime types the plugin supports
     */
    virtual QHash<QString, QStringList> fileExtensions() const = 0;

    /**
     * @brief name - the method returning the name of the plugin
     * @return the name of the plugin
     */
    virtual QString name() const = 0;

    /**
     * @brief description - the optional method
     * @return plugin's description
     */
    virtual QString description() const
    {
        return QString();
    }
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_I_NOTE_EDITOR_RESOURCE_PLUGIN_H
