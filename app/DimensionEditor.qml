import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Numeric editor for one driving dimension: on-screen keypad plus keyboard.
Popup {
    id: editor
    required property var controller
    // Turns a controller message key into visible text.
    required property var describe

    property var dimensionId: 0
    property string kind: ""
    property string buffer: ""
    property bool fresh: true
    property string error: ""

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Explicit backgrounds: the Material defaults render transparent here.
    background: Rectangle {
        color: editor.Material.dialogColor
        radius: 16
        border.color: editor.Material.dividerColor
    }
    Overlay.modal: Rectangle { color: "#66000000" }

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
        spacing: 12
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
            font.pixelSize: 16
            font.weight: Font.Medium
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "dimensionValue"
                text: editor.buffer
                font.pixelSize: 28
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
                Layout.minimumWidth: 200
            }
            Label {
                text: editor.kind === "angle" ? "°" : qsTr("мм")
                font.pixelSize: 18
                opacity: 0.7
            }
        }
        Label {
            objectName: "dimensionError"
            text: editor.error
            visible: text !== ""
            color: Material.color(Material.Red)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.maximumWidth: 280
        }
        GridLayout {
            columns: 3
            rowSpacing: 4
            columnSpacing: 4
            Repeater {
                model: ["7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "back"]
                delegate: Button {
                    required property string modelData
                    objectName: modelData === "." ? "key_dot" : "key_" + modelData
                    text: modelData === "back" ? "⌫" : modelData
                    font.pixelSize: 20
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 56
                    focusPolicy: Qt.NoFocus
                    onClicked: editor.press(modelData)
                    background: Rectangle {
                        radius: 10
                        color: parent.down ? editor.Material.dividerColor
                                           : editor.Material.backgroundColor
                        border.color: editor.Material.dividerColor
                    }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button {
                objectName: "cancelDimension"
                text: qsTr("Отмена")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: editor.close()
            }
            Button {
                objectName: "applyDimension"
                text: qsTr("Применить")
                focusPolicy: Qt.NoFocus
                onClicked: editor.accept()
                Material.foreground: "white"
                background: Rectangle {
                    radius: 10
                    color: editor.Material.accentColor
                    opacity: parent.down ? 0.8 : 1
                }
            }
        }
    }
}
