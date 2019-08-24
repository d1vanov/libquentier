/*
 * Copyright 2019 Dmitry Ivanov
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

function setFontFamily(fontFamily) {
    console.log("setFontFamily: " + fontFamily);

    // First check if document body is empty, if so, apply the font family to
    // the "default" body style
    body = document.body.textContent;
    body.replace(/ /g, "")
    if (body == "") {
        observer.stop();
        try {
            document.body.style.fontFamily = fontFamily;
        }
        finally {
            observer.start();
        }
        console.log("Applied font family to body style CSS");
        return {
            status:true,
            appliedTo:"bodyStyle",
            error:""
        };
    }

    var ret = managedPageAction("fontName", fontFamily);
    return {
        status:ret.status,
        appliedTo:"selection",
        error:ret.error
    };
}
