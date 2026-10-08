import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Card with one help entry or a list of entries (ui-help.adoc).
Popup {
    id: popup
    required property var help
    required property var theme

    property var keys: []
    property string heading: ""
    property string stateText: ""
    readonly property bool single: keys.length === 1

    // One entry, optionally with a line about the current sketch state.
    function show(key, state) {
        keys = [key]
        heading = ""
        stateText = state || ""
        open()
    }
    function showList(title, list) {
        keys = list
        heading = title
        stateText = ""
        open()
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(420, parent ? parent.width - 32 : 420)
    height: Math.min(implicitHeight, parent ? parent.height - 96 : implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(72, (parent.height - height) / 2) : 0
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: popup.theme.surface
        radius: 20
        border.color: popup.theme.line
    }
    Overlay.modal: Rectangle { color: "#33000000" }

    contentItem: ColumnLayout {
        objectName: "helpPopup"
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: popup.single ? "helpTitle" : "helpHeading"
                text: popup.single ? popup.help.title(popup.keys[0]) : popup.heading
                font.pixelSize: 18
                font.weight: Font.Medium
                color: popup.theme.ink
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "closeHelp"
                icon.source: "qrc:/icons/close.svg"
                icon.color: popup.theme.muted
                onClicked: popup.close()
            }
        }
        Label {
            objectName: "helpText"
            visible: popup.single
            text: popup.single ? popup.help.text(popup.keys[0]) : ""
            wrapMode: Text.WordWrap
            font.pixelSize: 15
            lineHeight: 1.15
            color: popup.theme.ink
            Layout.fillWidth: true
        }
        Label {
            objectName: "helpState"
            visible: popup.single && popup.stateText !== ""
            text: popup.stateText
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            font.weight: Font.Medium
            color: popup.theme.primary
            Layout.fillWidth: true
        }
        // Every entry is instantiated (Repeater, not ListView), so the list
        // can be scrolled and tested as a whole.
        Flickable {
            visible: !popup.single
            clip: true
            contentHeight: entries.implicitHeight
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: entries.implicitHeight
            ScrollBar.vertical: ScrollBar {}
            Column {
                id: entries
                width: parent.width
                spacing: 14
                Repeater {
                    model: popup.single ? [] : popup.keys
                    delegate: Column {
                        required property string modelData
                        objectName: "helpEntry_" + modelData
                        width: entries.width
                        spacing: 2
                        Label {
                            text: popup.help.title(parent.modelData)
                            font.pixelSize: 15
                            font.weight: Font.Medium
                            color: popup.theme.ink
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            text: popup.help.text(parent.modelData)
                            font.pixelSize: 14
                            color: popup.theme.muted
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
