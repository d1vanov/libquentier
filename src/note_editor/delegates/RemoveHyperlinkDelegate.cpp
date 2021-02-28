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

#include "RemoveHyperlinkDelegate.h"
#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "RemoveHyperlinkDelegate",                                         \
            "Can't remove hyperlink: no note editor's "                        \
            "page"));                                                          \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

RemoveHyperlinkDelegate::RemoveHyperlinkDelegate(
    NoteEditorPrivate & noteEditor) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor)
{}

void RemoveHyperlinkDelegate::start()
{
    QNDEBUG("note_editor:delegate", "RemoveHyperlinkDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &RemoveHyperlinkDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        findIdOfHyperlinkUnderCursor();
    }
}

void RemoveHyperlinkDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveHyperlinkDelegate"
            << "::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &RemoveHyperlinkDelegate::onOriginalPageConvertedToNote);

    findIdOfHyperlinkUnderCursor();
}

void RemoveHyperlinkDelegate::findIdOfHyperlinkUnderCursor()
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveHyperlinkDelegate"
            << "::findIdOfHyperlinkUnderCursor");

    QString javascript =
        QStringLiteral("hyperlinkManager.findSelectedHyperlinkId();");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &RemoveHyperlinkDelegate::onHyperlinkIdFound));
}

void RemoveHyperlinkDelegate::onHyperlinkIdFound(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveHyperlinkDelegate"
            << "::onHyperlinkIdFound: " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink data "
                       "request from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink data "
                           "request from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't get hyperlink data from JavaScript"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto dataIt = resultMap.find(QStringLiteral("data"));
    if (Q_UNLIKELY(dataIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("No hyperlink data received from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString dataStr = dataIt.value().toString();

    bool conversionResult = false;
    quint64 hyperlinkId = dataStr.toULongLong(&conversionResult);
    if (!conversionResult) {
        ErrorString error(
            QT_TR_NOOP("Can't remove hyperlink under cursor: "
                       "can't convert hyperlink id to a number"));
        QNWARNING("note_editor:delegate", error << ", data from JS: " << data);
        Q_EMIT notifyError(error);
        return;
    }

    removeHyperlink(hyperlinkId);
}

void RemoveHyperlinkDelegate::removeHyperlink(const quint64 hyperlinkId)
{
    QNDEBUG("note_editor:delegate", "RemoveHyperlinkDelegate::removeHyperlink");

    QString javascript = QStringLiteral("hyperlinkManager.removeHyperlink(") +
        QString::number(hyperlinkId) + QStringLiteral(", false);");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &RemoveHyperlinkDelegate::onHyperlinkRemoved));
}

void RemoveHyperlinkDelegate::onHyperlinkRemoved(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveHyperlinkDelegate"
            << "::onHyperlinkRemoved: " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink "
                       "removal from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink removal "
                           "from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't remove hyperlink, JavaScript error"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
