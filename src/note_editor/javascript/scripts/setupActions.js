/*
 * Copyright 2016-2022 Dmitry Ivanov
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

function disablePaste(e) {
    console.log("disablePaste");
    e.preventDefault();
    actionsWatcher.onPasteActionToggled();
}

function disableCut(e) {
    console.log("disableCut");
    e.preventDefault();
    actionsWatcher.onCutActionToggled();
}

document.body.onpaste = disablePaste;
document.body.oncut = disableCut;

document.onkeydown = function(e) {
    if (navigator.appVersion.indexOf('Mac') != -1) {
        if (e.metaKey) {
            if (e.key === 'z') {
                actionsWatcher.onUndoActionToggled();
                e.preventDefault();
            }
            else if (e.key === 'y') {
                actionsWatcher.onRedoActionToggled();
                e.preventDefault();
            }
        }
    }
    else {
        if (e.ctrlKey) {
            if (e.key === 'z') {
                actionsWatcher.onUndoActionToggled();
                e.preventDefault();
            }
            else if (e.key === 'y') {
                actionsWatcher.onRedoActionToggled();
                e.preventDefault();
            }
        }
    }
}
