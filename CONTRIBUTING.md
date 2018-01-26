# Contributing to libquentier

Thank you very much for considering contributing to libquentier! In order to ensure the successful adoption of your contributions please follow this guide:

* [How to report a bug](#how-to-report-a-bug)
* [How to contribute a bugfix](#how-to-contribute-a-bugfix)
* [How to suggest a new feature or improvement](#how-to-suggest-a-new-feature-or-improvement)
* [Submitting pull requests: which proposed changes are generally NOT approved](#submitting-pull-requests-which-proposed-changes-are-generally-not-approved)

### How to report a bug

If you think you have found a bug in libquentier, you are kindly encouraged to create an issue and describe the bug you have encountered. However, before doing that please look through the existing [open](https://github.com/d1vanov/libquentier/issues) and [closed](https://github.com/d1vanov/libquentier/issues?q=is%3Aissue+is%3Aclosed) issues to see whether the bug you've encountered has already been reported or maybe even fixed in some version of libquentier. If you don't see anything like what you experience, please proceed to creating a new issue.

If you are using libquentier in some application and it crashes or throws exception and it seems the reason is not in your application but in libquentier library itself, please provide some context for troubleshooting: for example, run your application under the debugger and provide the backtrace/call stack on crash or exception or at least a part of the backtrace/call stack containing the calls of libquentier's methods. See some examples how to do it:
* [with gdb](http://www.cs.toronto.edu/~krueger/csc209h/tut/gdb_tutorial.html)
* [with Visual Studio on Windows](http://www.codeproject.com/Articles/79508/Mastering-Debugging-in-Visual-Studio-2010-A-Beginn#heading0031)

If you don't have a crash or exception but have some method of libquentier returning an unexpected result to your application, please provide the context: which method returns wrong result, which parameters do you pass to the method, what do you expect to get, what do you actually get.

### How to contribute a bugfix

If you have not only found a bug but have also identified its reason and would like to contribute a bugfix, please note the following:

* `master` branch is meant to contain the current stable version of libquentier. Small and safe bugfixes are ok to land in `master`. More complicated bugfixes changing a lot of code or breaking the API/ABI compatibility are not indended for `master`. Instead, please contribute them to `development` branch.
* Make sure the changes you propose agree with the [coding style](CodingStyle.md) of the project. If they don't but the bugfix you are contributing is good enough, there's a chance your contribution would be accepted but the coding style would have to be cleaned up after that. You are encouraged to be a good citizen and not force others to clean up after you.

### How to suggest a new feature or improvement

If you have a new feature or improvement suggestion, please make sure you describe it with the appropriate level of detail and also describe at least one more or less realistic use case dictating the necessity of the change.

If you'd like to implement the feature or improvement you're suggesting yourself (or the one someone else suggested before), please mention that in the issue and briefly describe your implementation ideas. Like

> I think it could be solved by adding method B to class A

or

> A good way to get it done seems to introduce class C which would handle D

It is important to discuss that for both preventing the duplication of effort and to ensure the implementation won't confront the vision of others and would be accepted when ready. Please don't start the actual work on the new features before the vision agreement is achieved - it can lead to problems with code acceptance to libquentier.

When working on a feature or improvement implementation, please comply with the [coding style](CodingStyle.md) of the project.

All the new features and major improvements should be contributed to `development` branch, not `master`.

### Submitting pull requests: which proposed changes are generally NOT approved

* Breaking backward compatibility without a really good reason for that.
* The introduction of dependencies on strictly the latest versions of anything, like, on some feature existing only in the latest version of Qt.
* The direct unconditional usage of C++11/14/17 features breaking the build with older compilers not supporting them (see the [coding style](CodingStyle.md) doc for more info on that)
* The breakage of building/working with Qt4
