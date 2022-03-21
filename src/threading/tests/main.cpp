/*
 * Copyright 2022 Dmitry Ivanov
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

#include <gtest/gtest.h>

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Initialize.h>

#include <QCoreApplication>
#include <QTimer>

int main(int argc, char * argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("d1vanov"));

    quentier::initializeLibquentier();

    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Info);
    QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    QTimer::singleShot(0, [&]() // clazy:exclude=connect-3arg-lambda
    {
        ::testing::InitGoogleTest(&argc, argv); // NOLINT
        auto testResult = RUN_ALL_TESTS();
        QCoreApplication::exit(testResult);
    });

    return QCoreApplication::exec();
}
