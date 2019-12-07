/*
 * Copyright 2019-2019 Dmitry Ivanov
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

// replaces the contents of the style tag in the upper part of the page
function replaceStyle(newStyleTagContents) {
    console.log("replaceStyle: " + newStyleTagContents);

    var styleTag = document.getElementById('bodyStyleTag')
    if (!styleTag) {
        return {
            status:false,
            error:"Cannot replace style: cannot find style tag with bodyStyleTag id"
        };
    }

    observer.stop();

    try {
        styleTag.innerHTML = newStyleTagContents;
    }
    finally {
        observer.start();
    }

    return { status:true, error:"" };
}
