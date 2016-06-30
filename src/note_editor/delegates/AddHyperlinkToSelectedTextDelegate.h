#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_HYPERLINK_TO_SELECTED_TEXT_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_HYPERLINK_TO_SELECTED_TEXT_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"
#include <quentier/utility/Qt4Helper.h>
#include <quentier/types/Note.h>
#include <QObject>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)

/**
 * @brief The AddHyperlinkToSelectedTextDelegate class encapsulates a chain of callbacks
 * required for proper implementation of adding a hyperlink to the currently selected text
 * considering the details of wrapping this action around undo stack and necessary switching
 * of note editor page during the process
 */
class AddHyperlinkToSelectedTextDelegate: public QObject
{
    Q_OBJECT
public:
    explicit AddHyperlinkToSelectedTextDelegate(NoteEditorPrivate & noteEditor, const quint64 hyperlinkIdToAdd);
    void start();
    void startWithPresetHyperlink(const QString & presetHyperlink);

Q_SIGNALS:
    void finished();
    void cancelled();
    void notifyError(QString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);
    void onInitialHyperlinkDataReceived(const QVariant & data);

    void onAddHyperlinkDialogFinished(QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty);

    void onHyperlinkSetToSelection(const QVariant & data);

private:
    void requestPageScroll();
    void addHyperlinkToSelectedText();
    void raiseAddHyperlinkDialog(const QString & initialText);
    void setHyperlinkToSelection(const QString & url, const QString & text);

private:
    typedef JsResultCallbackFunctor<AddHyperlinkToSelectedTextDelegate> JsCallback;

private:
    NoteEditorPrivate &     m_noteEditor;

    bool                    m_shouldGetHyperlinkFromDialog;
    QString                 m_presetHyperlink;

    bool                    m_initialTextWasEmpty;

    const quint64           m_hyperlinkId;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_HYPERLINK_TO_SELECTED_TEXT_DELEGATE_H
