(function setInitialCaretPosition() {
    console.log("setInitialCaretPosition");

    document.body.focus();

    var range = document.createRange();
    range.selectNodeContents(document.body);
    range.collapse(false);

    var selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);

    window.scrollTo(0, document.body.scrollHeight);
})();
