# Qt runtime

M7Arc dynamically links Qt 6.11.2 Core, Gui, Widgets and Concurrent from Qt Base, distributed under the applicable open-source licenses. Qt's license files and third-party notices accompany the bundle. The separately licensed M7Arc and 7-Zip sources keep their own terms.

- Qt Base source: https://download.qt.io/official_releases/qt/6.11/6.11.2/submodules/qtbase-everywhere-src-6.11.2.tar.xz
- Qt Base repository: https://github.com/qt/qtbase/tree/v6.11.2
- Qt third-party component notices: https://doc.qt.io/qt-6/licenses-used-in-qt.html
- Qt build instructions: https://doc.qt.io/qt-6/macos-building.html
- Qt licensing: https://doc.qt.io/qt-6/licensing.html

The application permits replacing its dynamically linked Qt frameworks with compatible modified builds. After replacing a framework in Contents/Frameworks, reapply local ad-hoc signatures from the nested libraries to the outer application. Alternatively, rebuild M7Arc against the modified Qt installation using CMAKE_PREFIX_PATH and package it with that installation's macdeployqt. Reverse engineering for debugging modifications to these libraries is not prohibited.

The release source archive includes the application source, build scripts and complete vendored 7-Zip source. The Qt source URL above identifies the matching unmodified Qt release.
