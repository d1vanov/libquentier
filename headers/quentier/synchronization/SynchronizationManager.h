#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Qt4Helper.h>
#include <quentier/utility/QNLocalizedString.h>
#include <QObject>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerThreadWorker)
QT_FORWARD_DECLARE_CLASS(SynchronizationManagerPrivate)

class QUENTIER_EXPORT SynchronizationManager: public QObject
{
    Q_OBJECT
public:
    SynchronizationManager(const QString & consumerKey, const QString & consumerSecret,
                           LocalStorageManagerThreadWorker & localStorageManagerThreadWorker);
    virtual ~SynchronizationManager();

    bool active() const;
    bool paused() const;

public Q_SLOTS:
    void synchronize();
    void pause();
    void resume();
    void stop();

Q_SIGNALS:
    void failed(QNLocalizedString errorDescription);
    void finished();

    // state signals
    void remoteToLocalSyncPaused(bool pendingAuthenticaton);
    void remoteToLocalSyncStopped();

    void sendLocalChangesPaused(bool pendingAuthenticaton);
    void sendLocalChangesStopped();

    // other informative signals
    void willRepeatRemoteToLocalSyncAfterSendingChanges();
    void detectedConflictDuringLocalChangesSending();
    void rateLimitExceeded(qint32 secondsToWait);

    void remoteToLocalSyncDone();
    void progress(QNLocalizedString message, double workDonePercentage);

private:
    SynchronizationManager() Q_DECL_DELETE;
    Q_DISABLE_COPY(SynchronizationManager)

    SynchronizationManagerPrivate * d_ptr;
    Q_DECLARE_PRIVATE(SynchronizationManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H
