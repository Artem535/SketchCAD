import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Numeric editor for one driving dimension: on-screen keypad plus keyboard.
Popup {
    id: editor
    required property var controller
    required property var theme
    // Turns a controller message key into visible text.
    required property var describe

    property var dimensionId: 0
    property string kind: ""
    property string buffer: ""
    property bool fresh: true
    property string error: ""

    modal: true
    focus: true
    // Keypad card anchored bottom right, as in the mockup.
    parent: Overlay.overlay
    x: parent ? parent.width - width - 12 : 0
    y: parent ? parent.height - height - 12 : 0
    width: 372
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Explicit backgrounds: the Material defaults render transparent here.
    background: Rectangle {
        color: editor.theme.surface
        radius: 24
        border.color: editor.theme.line
    }
    Overlay.modal: Rectangle { color: "#33000000" }

    function format(value) {
        return String(Number(value.toFixed(3)))
    }

    // Opens the editor for dimension `id` with its current value.
    function edit(id) {
        const list = controller.dimensions
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id !== id) continue
            dimensionId = id
            kind = list[i].kind
            buffer = format(list[i].value)
            fresh = true
            error = ""
            open()
            return
        }
    }

    function press(key) {
        if (fresh) buffer = ""
        fresh = false
        error = ""
        if (key === "back") buffer = buffer.slice(0, -1)
        else if (key === ".") { if (buffer.indexOf(".") < 0) buffer += buffer === "" ? "0." : "." }
        else buffer += key
    }

    function accept() {
        const value = Number(buffer)
        if (buffer === "" || !isFinite(value)) {
            error = describe("invalid_value")
            return
        }
        if (controller.set_dimension(dimensionId, value)) close()
        else error = describe(controller.message)
    }

    contentItem: ColumnLayout {
        objectName: "dimensionEditor"
        spacing: 8
        focus: true

        Keys.onPressed: (event) => {
            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) editor.press(event.text)
            else if (event.key === Qt.Key_Period || event.key === Qt.Key_Comma) editor.press(".")
            else if (event.key === Qt.Key_Backspace) editor.press("back")
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) editor.accept()
            else return
            event.accepted = true
        }

        Label {
            text: {
                switch (editor.kind) {
                case "length": return qsTr("Длина")
                case "distance": return qsTr("Расстояние")
                case "angle": return qsTr("Угол")
                case "radius": return qsTr("Радиус")
                }
                return qsTr("Размер")
            }
            font.pixelSize: 13
            color: editor.theme.muted
            Layout.leftMargin: 10
            Layout.topMargin: 4
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Label {
                objectName: "dimensionValue"
                text: editor.buffer
                font.pixelSize: 34
                font.weight: Font.Medium
                font.features: { "tnum": 1 }
                color: editor.theme.ink
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
            Label {
                text: editor.kind === "angle" ? "°" : qsTr("мм")
                font.pixelSize: 18
                color: editor.theme.muted
            }
        }
        Label {
            objectName: "dimensionError"
            text: editor.error
            visible: text !== ""
            color: editor.theme.err
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
        }
        GridLayout {
            columns: 4
            rowSpacing: 6
            columnSpacing: 6
            Layout.fillWidth: true
            Repeater {
                model: ["7", "8", "9", "back", "4", "5", "6", ".", "1", "2", "3", "0"]
                delegate: Button {
                    id: key
                    required property string modelData
                    objectName: modelData === "." ? "key_dot" : "key_" + modelData
                    text: modelData === "back" ? "⌫" : modelData
                    font.pixelSize: 22
                    Material.foreground: editor.theme.ink
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    topInset: 0
                    bottomInset: 0
                    focusPolicy: Qt.NoFocus
                    onClicked: editor.press(modelData)
                    background: Rectangle {
                        radius: 16
                        color: key.down ? editor.theme.primaryContainer
                                        : editor.theme.surface2
                    }
                }
            }
            Button {
                id: cancelKey
                objectName: "cancelDimension"
                text: qsTr("Отмена")
                font.pixelSize: 15
                Material.foreground: editor.theme.muted
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                topInset: 0
                bottomInset: 0
                focusPolicy: Qt.NoFocus
                onClicked: editor.close()
                background: Rectangle {
                    radius: 16
                    color: cancelKey.down ? editor.theme.primaryContainer
                                          : editor.theme.surface2
                }
            }
            Button {
                id: applyKey
                objectName: "applyDimension"
                text: qsTr("Применить")
                font.pixelSize: 15
                Material.foreground: editor.theme.onPrimary
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                topInset: 0
                bottomInset: 0
                focusPolicy: Qt.NoFocus
                onClicked: editor.accept()
                background: Rectangle {
                    radius: 16
                    color: editor.theme.primary
                    opacity: applyKey.down ? 0.8 : 1
                }
            }
        }
    }
}
