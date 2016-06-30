#ifndef LIB_QUENTIER_TYPES_USER_ADAPTER_H
#define LIB_QUENTIER_TYPES_USER_ADAPTER_H

#include "IUser.h"

namespace quentier {

/**
 * @brief The UserAdapter class uses reference to external qevercloud::User
 * and adapts its interface to that of IUser. The instances of this class
 * should be used only within the same scope as the referenced external
 * qevercloud::User object, otherwise it is possible to run into undefined behaviour.
 * UserAdapter class is aware of constness of external object it references,
 * it would throw UserAdapterAccessException exception in attempts to use
 * referenced const object in non-const context
 *
 * @see UserAdapterAccessException
 */
class QUENTIER_EXPORT UserAdapter : public IUser
{
public:
    UserAdapter(qevercloud::User & externalEnUser);
    UserAdapter(const qevercloud::User & externalEnUser);
    UserAdapter(const UserAdapter & other);
    UserAdapter(UserAdapter && other);
    virtual ~UserAdapter();

private:
    virtual const qevercloud::User & GetEnUser() const Q_DECL_OVERRIDE;
    virtual qevercloud::User & GetEnUser() Q_DECL_OVERRIDE;

    UserAdapter() Q_DECL_DELETE;
    UserAdapter & operator=(const UserAdapter & other) Q_DECL_DELETE;
    UserAdapter & operator=(UserAdapter && other) Q_DECL_DELETE;

    qevercloud::User * m_pEnUser;
    bool m_isConst;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_USER_ADAPTER_H
