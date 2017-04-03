function disableKeyboardModifiers(e) {
    if (e.ctrlKey e.altKey)) {
        e.preventDefault();
    }
}

document.onkeydown = disableKeyboardModifiers;

