import QtQuick

// Colour tokens of design/sketchcad-tablet.html (tablet-shell.adoc).
// One instance lives in Main.qml and is passed to the panels.
QtObject {
    property bool dark: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark

    readonly property color bg: dark ? "#12191f" : "#f4f6f9"
    readonly property color surface: dark ? "#1c262f" : "#ffffff"
    readonly property color surface2: dark ? "#26323d" : "#eef2f6"
    readonly property color canvas: dark ? "#161e25" : "#f7f9fb"
    readonly property color grid: dark ? "#232f3a" : "#e1e8ef"
    readonly property color ink: dark ? "#e6edf4" : "#1c2b3a"
    readonly property color muted: dark ? "#9fb2c4" : "#5d7083"
    readonly property color line: dark ? "#364552" : "#d6dfe8"
    readonly property color primary: dark ? "#8ec1ff" : "#1668c0"
    readonly property color onPrimary: dark ? "#0b2a4a" : "#ffffff"
    readonly property color primaryContainer: dark ? "#1f3b57" : "#dbe9fb"
    readonly property color geom: dark ? "#cfe3f7" : "#1d3b5a"
    readonly property color ok: dark ? "#5fd0ac" : "#16745c"
    readonly property color okContainer: dark ? "#17352c" : "#e2f4ee"
    readonly property color err: dark ? "#ffb4ab" : "#b3261e"
    readonly property color errContainer: dark ? "#4a1f1b" : "#fbe5e3"
    readonly property color warn: dark ? "#f2c46b" : "#8a5a00"
    readonly property color shadow: dark ? "#80000000" : "#221c2b3a"
}
