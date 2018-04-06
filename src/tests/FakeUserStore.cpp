#include "FakeUserStore.h"

namespace quentier {

FakeUserStore::FakeUserStore() :
    IUserStore(QSharedPointer<qevercloud::UserStore>(new qevercloud::UserStore(QStringLiteral("127.0.0.1")))),
    m_edamVersionMajor(0),
    m_edamVersionMinor(0),
    m_accountLimits(),
    m_users(),
    m_shouldTriggerRateLimitReachOnNextCall()
{}

qint16 FakeUserStore::edamVersionMajor() const
{
    return m_edamVersionMajor;
}

void FakeUserStore::setEdamVersionMajor(const qint16 edamVersionMajor)
{
    m_edamVersionMajor = edamVersionMajor;
}

qint16 FakeUserStore::edamVersionMinor() const
{
    return m_edamVersionMinor;
}

void FakeUserStore::setEdamVersionMinor(const qint16 edamVersionMinor)
{
    m_edamVersionMinor = edamVersionMinor;
}

const qevercloud::AccountLimits * FakeUserStore::findAccountLimits(const qevercloud::ServiceLevel::type serviceLevel) const
{
    auto it = m_accountLimits.find(serviceLevel);
    if (it != m_accountLimits.end()) {
        return &(it.value());
    }

    return Q_NULLPTR;
}

void FakeUserStore::setAccountLimits(const qevercloud::ServiceLevel::type serviceLevel,
                                     const qevercloud::AccountLimits & limits)
{
    m_accountLimits[serviceLevel] = limits;
}

const User * FakeUserStore::findUser(const qint32 id) const
{
    auto it = m_users.find(id);
    if (it != m_users.end()) {
        return &(it.value());
    }

    return Q_NULLPTR;
}

void FakeUserStore::triggerRateLimitReachOnNextCall()
{
    m_shouldTriggerRateLimitReachOnNextCall = true;
}

IUserStore * FakeUserStore::create(const QString & host) const
{
    Q_UNUSED(host)
    return new FakeUserStore;
}

bool FakeUserStore::checkVersion(const QString & clientName, qint16 edamVersionMajor,
                                 qint16 edamVersionMinor, ErrorString & errorDescription)
{
    Q_UNUSED(clientName);

    if (m_edamVersionMajor != edamVersionMajor) {
        errorDescription.setBase(QStringLiteral("EDAM major version mismatch"));
        return false;
    }

    if (m_edamVersionMinor != edamVersionMinor) {
        errorDescription.setBase(QStringLiteral("EDAM minor version mismatch"));
        return false;
    }

    return true;
}

qint32 FakeUserStore::getUser(User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    if (m_shouldTriggerRateLimitReachOnNextCall) {
        rateLimitSeconds = 0;
        m_shouldTriggerRateLimitReachOnNextCall = false;
        return qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED;
    }

    if (!user.hasId()) {
        errorDescription.setBase(QStringLiteral("User has no id"));
        return qevercloud::EDAMErrorCode::DATA_REQUIRED;
    }

    auto it = m_users.find(user.id());
    if (it == m_users.end()) {
        errorDescription.setBase(QStringLiteral("User data was not found"));
        return qevercloud::EDAMErrorCode::DATA_REQUIRED;
    }

    user = it.value();
    return 0;
}

qint32 FakeUserStore::getAccountLimits(const qevercloud::ServiceLevel::type serviceLevel, qevercloud::AccountLimits & limits,
                                       ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    if (m_shouldTriggerRateLimitReachOnNextCall) {
        rateLimitSeconds = 0;
        m_shouldTriggerRateLimitReachOnNextCall = false;
        return qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED;
    }

    auto it = m_accountLimits.find(serviceLevel);
    if (it == m_accountLimits.end()) {
        errorDescription.setBase(QStringLiteral("Account limits were not found"));
        return qevercloud::EDAMErrorCode::DATA_REQUIRED;
    }

    limits = it.value();
    return 0;
}

} // namespace quentier
