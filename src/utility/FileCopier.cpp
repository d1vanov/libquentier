/*
 * Copyright 2018 Dmitry Ivanov
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

namespace quentier {

FileCopier::FileCopier(QObject * parent) :
    QObject(parent),
    d_ptr(new FileCopierPrivate(this))
{}

FileCopier::State::type FileCopier::state() const
{
    Q_D(const FileCopier);

    if (d->idle()) {
        return State::Idle;
    }

    if (d->cancelled()) {
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

} // namespace quentier
