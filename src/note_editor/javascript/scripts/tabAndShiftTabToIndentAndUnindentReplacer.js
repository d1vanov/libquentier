/*
 * Copyright 2018-2019 Dmitry Ivanov
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

(function() {
    console.log("Setting up the keypress handler for tab and shift + tab catching");
    var handler = function(event) {
        console.log("Inside keypress handler meant to replace tab with shift + tab: key code = " + event.which + ", shiftKey = " + event.shiftKey);
        var keyCode = event.which;
        var matched = false;
        // Catching tab and shift + tab to do indent and outdent respectively
        if ((keyCode == 9) && !event.shiftKey) {
            matched = true;
            managedPageAction('indent');
        }
        else if (((keyCode == 9) || (keyCode == 25)) && event.shiftKey) {
            matched = true;
            managedPageAction('outdent');
        }
        else if ((keyCode == 16) && !event.shiftKey) {
            // That's just Shift, Linux appears to send two events for tab + shift, shift being the second one
            matched = true;
        }

        if (matched) {
            event.preventDefault();
            event.stopPropagation();
        }
    };
    var capture = true;
    document.body.addEventListener("keypress", handler, capture);
})();
