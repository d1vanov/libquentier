/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "TestRunner.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Initialize.h>
#include <quentier/utility/QuentierApplication.h>

#include <QCommandLineParser>
#include <QDebug>
#include <QTest>

int main(int argc, char * argv[])
{
    quentier::QuentierApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("d1vanov"));
    QCoreApplication::setApplicationName(QStringLiteral("LibquentierTests"));

    // Setup command line option processing
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate(
        "CommandLineParser", "Sync integrational tests"));

    const QString useNetworkTransportLayerOptionName =
        QStringLiteral("use_network_transport_layer");

    QCommandLineOption useNetworkTransportLayerOption{
        useNetworkTransportLayerOptionName};

    useNetworkTransportLayerOption.setDescription(QStringLiteral(
        "When this option is used, tests use local TCP socket and thus involve "
        "network transport layer into the tests; otherwise the network layer "
        "is not involved into the tests, a stub is used instead"));

    parser.addOption(useNetworkTransportLayerOption);
    parser.addHelpOption();

    parser.process(app);

    // Initialize logs and the rest of libquentier facilities
    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Trace);
    // QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    qWarning() << "Logs directory: " << quentier::QuentierLogFilesDirPath();

    quentier::initializeLibquentier();

    // Run sync integrational tests
    quentier::synchronization::tests::TestRunner::Options options;
    options.useNetworkTransportLayer =
        parser.optionNames().contains(useNetworkTransportLayerOptionName);

    // Remove our own command line argument so that only QTest related arguments
    // go to QTest::qExec call.
    QStringList arguments = QCoreApplication::arguments();
    arguments.removeAll(
        QStringLiteral("--") + useNetworkTransportLayerOptionName);

    return QTest::qExec(
        new quentier::synchronization::tests::TestRunner(options), arguments);
}
