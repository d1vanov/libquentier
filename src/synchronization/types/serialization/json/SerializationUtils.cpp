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

#include "SerializationUtils.h"

#include "../Utils.h"
#include "../../ExceptionUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/LocalStorageOpenException.h>
#include <quentier/local_storage/LocalStorageOperationException.h>
#include <quentier/types/ErrorString.h>

#include <string_view>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gExceptionTypeKey = "type"sv;
constexpr auto gExceptionMessageKey = "message"sv;

constexpr auto gInvalidArgumentName = "InvalidArgument"sv;
constexpr auto gOperationCanceledName = "OperationCanceled"sv;
constexpr auto gRuntimeErrorName = "RuntimeError"sv;
constexpr auto gLocalStorageOpenExceptionName = "LocalStorageOpenException"sv;
constexpr auto gLocalStorageOperationExceptionName =
    "LocalStorageOperationException"sv;

[[nodiscard]] std::string_view exceptionTypeName(const ExceptionInfo & info)
{
    if (info.type_info == &typeid(InvalidArgument)) {
        return gInvalidArgumentName;
    }

    if (info.type_info == &typeid(OperationCanceled)) {
        return gOperationCanceledName;
    }

    if (info.type_info == &typeid(RuntimeError)) {
        return gRuntimeErrorName;
    }

    if (info.type_info == &typeid(local_storage::LocalStorageOpenException)) {
        return gLocalStorageOpenExceptionName;
    }

    if (info.type_info ==
        &typeid(local_storage::LocalStorageOperationException))
    {
        return gLocalStorageOperationExceptionName;
    }

    return gRuntimeErrorName;
}

} // namespace

QJsonObject serializeException(const QException & e)
{
    auto info = exceptionInfo(e);
    QJsonObject object;
    object[toString(gExceptionTypeKey)] = toString(exceptionTypeName(info));
    object[toString(gExceptionMessageKey)] =
        info.errorText.nonLocalizedString();
    return object;
}

std::shared_ptr<QException> deserializeException(const QJsonObject & json)
{
    const auto exceptionTypeIt = json.constFind(toString(gExceptionTypeKey));
    if (exceptionTypeIt == json.constEnd() || !exceptionTypeIt->isString()) {
        return nullptr;
    }

    const auto exceptionMessageIt =
        json.constFind(toString(gExceptionMessageKey));
    if (exceptionMessageIt == json.constEnd() ||
        !exceptionMessageIt->isString())
    {
        return nullptr;
    }

    const auto exceptionType = exceptionTypeIt->toString().toStdString();
    auto exceptionMessage = ErrorString{exceptionMessageIt->toString()};

    if (exceptionType == gInvalidArgumentName) {
        return std::make_shared<InvalidArgument>(std::move(exceptionMessage));
    }

    if (exceptionType == gOperationCanceledName) {
        return std::make_shared<OperationCanceled>();
    }

    if (exceptionType == gRuntimeErrorName) {
        return std::make_shared<RuntimeError>(std::move(exceptionMessage));
    }

    if (exceptionType == gLocalStorageOpenExceptionName) {
        return std::make_shared<local_storage::LocalStorageOpenException>(
            std::move(exceptionMessage));
    }

    if (exceptionType == gLocalStorageOperationExceptionName) {
        return std::make_shared<local_storage::LocalStorageOperationException>(
            std::move(exceptionMessage));
    }

    return nullptr;
}

} // namespace quentier::synchronization
