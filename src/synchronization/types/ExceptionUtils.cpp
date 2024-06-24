/*
 * Copyright 2024 Dmitry Ivanov
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

#include "ExceptionUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/LocalStorageOpenException.h>
#include <quentier/local_storage/LocalStorageOperationException.h>

#include <QException>

namespace quentier::synchronization {

[[nodiscard]] ExceptionInfo exceptionInfo(const QException & e)
{
    try {
        e.raise();
    }
    catch (const InvalidArgument & exc) {
        return ExceptionInfo{&typeid(InvalidArgument), exc.errorMessage()};
    }
    catch (const OperationCanceled & exc) {
        return ExceptionInfo{&typeid(OperationCanceled), exc.errorMessage()};
    }
    catch (const RuntimeError & exc) {
        return ExceptionInfo{&typeid(RuntimeError), exc.errorMessage()};
    }
    catch (const local_storage::LocalStorageOpenException & exc) {
        return ExceptionInfo{
            &typeid(local_storage::LocalStorageOpenException),
            exc.errorMessage()};
    }
    catch (const local_storage::LocalStorageOperationException & exc) {
        return ExceptionInfo{
            &typeid(local_storage::LocalStorageOperationException),
            exc.errorMessage()};
    }
    catch (...) {
    }

    return ExceptionInfo{
        &typeid(RuntimeError), ErrorString{QString::fromUtf8(e.what())}};
}

} // namespace quentier::synchronization
