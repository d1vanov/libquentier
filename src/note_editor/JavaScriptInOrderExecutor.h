#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H

#include <quentier/utility/Qt4Helper.h>
#include <QObject>
#include <QQueue>

#ifdef USE_QT_WEB_ENGINE
#include <QWebEngineView>
#else
#include <QWebView>
#endif

#include <boost/function.hpp>

namespace quentier {

class JavaScriptInOrderExecutor: public QObject
{
    Q_OBJECT
private:
    typedef
#ifdef USE_QT_WEB_ENGINE
    QWebEngineView
#else
    QWebView
#endif
    WebView;

public:
    typedef boost::function<void (const QVariant&)> Callback;

    explicit JavaScriptInOrderExecutor(WebView & view, QObject * parent = Q_NULLPTR);

    void append(const QString & script, Callback callback = 0);
    int size() const { return m_javaScriptsQueue.size(); }
    bool empty() const { return m_javaScriptsQueue.empty(); }
    void clear() { m_javaScriptsQueue.clear(); }

    void start();
    bool inProgress() const { return m_inProgress; }

Q_SIGNALS:
    void finished();

private:
    class JavaScriptCallback
    {
    public:
        JavaScriptCallback(JavaScriptInOrderExecutor & executor) :
            m_executor(executor)
        {}

        void operator()(const QVariant & result);

    private:
        JavaScriptInOrderExecutor & m_executor;
    };

    friend class JavaScriptCallback;

    void next(const QVariant & data);

private:
    WebView &                           m_view;
    QQueue<QPair<QString, Callback> >   m_javaScriptsQueue;
    Callback                            m_currentPendingCallback;
    bool                                m_inProgress;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H
