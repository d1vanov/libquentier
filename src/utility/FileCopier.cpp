/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include <quentier/utility/FileCopier.h>

#include "FileCopier_p.h"

#include <QDebug>
#include <QTextStream>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename T>
void printState(T & t, const FileCopier::State state)
{
    switch (state) {
    case FileCopier::State::Idle:
        t << "Idle";
        break;
    case FileCopier::State::Copying:
        t << "Copying";
        break;
    case FileCopier::State::Cancelling:
        t << "Cancelling";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

FileCopier::FileCopier(QObject * parent) :
    QObject(parent), d_ptr(new FileCopierPrivate(this))
{
    QObject::connect(
        d_ptr, &FileCopierPrivate::progressUpdate, this,
        &FileCopier::progressUpdate);

    QObject::connect(
        d_ptr, &FileCopierPrivate::finished, this, &FileCopier::finished);

    QObject::connect(
        d_ptr, &FileCopierPrivate::cancelled, this, &FileCopier::cancelled);

    QObject::connect(
        d_ptr, &FileCopierPrivate::notifyError, this, &FileCopier::notifyError);
}

FileCopier::State FileCopier::state() const
{
    Q_D(const FileCopier);

    if (d->isIdle()) {
        return State::Idle;
    }

    if (d->isCancelled()) {
        return State::Cancelling;
    }

    return State::Copying;
}

QString FileCopier::sourceFilePath() const
{
    Q_D(const FileCopier);
    return d->sourceFilePath();
}

QString FileCopier::destinationFilePath() const
{
    Q_D(const FileCopier);
    return d->destinationFilePath();
}

double FileCopier::currentProgress() const
{
    Q_D(const FileCopier);
    return d->currentProgress();
}

void FileCopier::copyFile(QString sourcePath, QString destPath)
{
    Q_D(FileCopier);
    d->copyFile(sourcePath, destPath);
}

void FileCopier::cancel()
{
    Q_D(FileCopier);
    d->cancel();
}

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(QDebug & dbg, const FileCopier::State state)
{
    printState(dbg, state);
    return dbg;
}

QTextStream & operator<<(QTextStream & strm, const FileCopier::State state)
{
    printState(strm, state);
    return strm;
}

} // namespace quentier
