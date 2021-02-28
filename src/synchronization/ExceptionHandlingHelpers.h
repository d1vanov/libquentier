/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_EXCEPTION_HANDLING_HELPERS_H
#define LIB_QUENTIER_SYNCHRONIZATION_EXCEPTION_HANDLING_HELPERS_H

// The macros defines in this file are used in NoteStore.cpp and UserStore.cpp
// for the sake of avoiding the code duplication

// 1) "Implementation" macros containing the bulk of exception handling code

#define CATCH_THRIFT_EXCEPTION_IMPL(...)                                       \
    catch (const qevercloud::ThriftException & thriftException) {              \
        errorDescription.setBase(                                              \
            QT_TRANSLATE_NOOP("synchronization", "Thrift exception"));         \
        errorDescription.details() = QStringLiteral("type = ");                \
        errorDescription.details() += ToString(thriftException.type());        \
        errorDescription.details() += QStringLiteral(": ");                    \
        errorDescription.details() +=                                          \
            QString::fromUtf8(thriftException.what());                         \
        QNWARNING("synchronization", errorDescription);                        \
        __VA_ARGS__;                                                           \
    }

#define CATCH_EVERNOTE_EXCEPTION_IMPL(...)                                     \
    catch (const qevercloud::EvernoteException & evernoteException) {          \
        errorDescription.setBase(QT_TRANSLATE_NOOP(                            \
            "synchronization", "QEverCloud Evernote exception"));              \
        if (evernoteException.exceptionData()) {                               \
            errorDescription.details() =                                       \
                evernoteException.exceptionData()->errorMessage;               \
        }                                                                      \
        QNWARNING("synchronization", errorDescription);                        \
        __VA_ARGS__;                                                           \
    }

#define CATCH_EVER_CLOUD_EXCEPTION_IMPL(...)                                   \
    catch (const qevercloud::EverCloudException & everCloudException) {        \
        errorDescription.setBase(                                              \
            QT_TRANSLATE_NOOP("synchronization", "QEverCloud exception"));     \
        errorDescription.details() =                                           \
            QString::fromUtf8(everCloudException.what());                      \
        QNWARNING("synchronization", errorDescription);                        \
        __VA_ARGS__;                                                           \
    }

#define CATCH_STD_EXCEPTION_IMPL(...)                                          \
    catch (const std::exception & e) {                                         \
        errorDescription.setBase(                                              \
            QT_TRANSLATE_NOOP("synchronization", "std::exception"));           \
        errorDescription.details() = QString::fromUtf8(e.what());              \
        QNWARNING("synchronization", errorDescription);                        \
        __VA_ARGS__;                                                           \
    }

#define CATCH_GENERIC_EXCEPTIONS_IMPL(...)                                     \
    CATCH_THRIFT_EXCEPTION_IMPL(__VA_ARGS__)                                   \
    CATCH_EVERNOTE_EXCEPTION_IMPL(__VA_ARGS__)                                 \
    CATCH_EVER_CLOUD_EXCEPTION_IMPL(__VA_ARGS__)                               \
    CATCH_STD_EXCEPTION_IMPL(__VA_ARGS__)

// 2) Macros returning false in the end of exception handling

#define CATCH_THRIFT_EXCEPTION_RET_FALSE()                                     \
    CATCH_THRIFT_EXCEPTION_IMPL(return false)                                  \
    // CATCH_THRIFT_EXCEPTION_RET_FALSE

#define CATCH_EVERNOTE_EXCEPTION_RET_FALSE()                                   \
    CATCH_EVERNOTE_EXCEPTION_IMPL(return false)

#define CATCH_EVER_CLOUD_EXCEPTION_RET_FALSE()                                 \
    CATCH_EVER_CLOUD_EXCEPTION_IMPL(return false)

#define CATCH_STD_EXCEPTION_RET_FALSE() CATCH_STD_EXCEPTION_IMPL(return false)

#define CATCH_GENERIC_EXCEPTIONS_RET_FALSE()                                   \
    CATCH_THRIFT_EXCEPTION_RET_FALSE()                                         \
    CATCH_EVERNOTE_EXCEPTION_RET_FALSE()                                       \
    CATCH_EVER_CLOUD_EXCEPTION_RET_FALSE()                                     \
    CATCH_STD_EXCEPTION_RET_FALSE()

// 3) Macros returning no value

#define CATCH_THRIFT_EXCEPTION_RET() CATCH_THRIFT_EXCEPTION_IMPL(return )

#define CATCH_EVERNOTE_EXCEPTION_RET() CATCH_EVERNOTE_EXCEPTION_IMPL(return )

#define CATCH_EVER_CLOUD_EXCEPTION_RET()                                       \
    CATCH_EVER_CLOUD_EXCEPTION_IMPL(return )

#define CATCH_STD_EXCEPTION_RET() CATCH_STD_EXCEPTION_IMPL(return )

#define CATCH_GENERIC_EXCEPTIONS_RET()                                         \
    CATCH_THRIFT_EXCEPTION_RET()                                               \
    CATCH_EVERNOTE_EXCEPTION_RET()                                             \
    CATCH_EVER_CLOUD_EXCEPTION_RET()                                           \
    CATCH_STD_EXCEPTION_RET()

// 4) Macros not returning anything

#define CATCH_THRIFT_EXCEPTION_NO_RET() CATCH_THRIFT_EXCEPTION_IMPL()

#define CATCH_EVERNOTE_EXCEPTION_NO_RET() CATCH_EVERNOTE_EXCEPTION_IMPL()

#define CATCH_EVER_CLOUD_EXCEPTION_NO_RET() CATCH_EVER_CLOUD_EXCEPTION_IMPL()

#define CATCH_STD_EXCEPTION_NO_RET() CATCH_STD_EXCEPTION_IMPL()

#define CATCH_GENERIC_EXCEPTIONS_NO_RET()                                      \
    CATCH_THRIFT_EXCEPTION_NO_RET()                                            \
    CATCH_EVERNOTE_EXCEPTION_NO_RET()                                          \
    CATCH_EVER_CLOUD_EXCEPTION_NO_RET()                                        \
    CATCH_STD_EXCEPTION_NO_RET()

#endif // LIB_QUENTIER_SYNCHRONIZATION_EXCEPTION_HANDLING_HELPERS_H
