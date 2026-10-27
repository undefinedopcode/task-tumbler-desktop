# org.kde.tasktumbler.macstyle: the Qt Quick Controls style on macOS. It is
# Qt's macOS style plus a Mac-like ToolButton, which that style lacks. Its
# name is also where Kirigami looks for our colours (package-macos.sh).
qt_add_qml_module(tumbler-macstyle
    URI org.kde.tasktumbler.macstyle
    # Numbered like Qt's styles: "auto" hands this version to the macOS
    # style, which is what makes it the fallback for every other control.
    VERSION 6.0
    STATIC
    QML_FILES ToolButton.qml
    IMPORTS QtQuick.Controls.macOS/auto
)
target_link_libraries(tumbler-macstyle PRIVATE Qt6::Quick Qt6::QuickControls2)
