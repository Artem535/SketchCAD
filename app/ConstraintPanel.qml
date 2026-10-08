import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Lists constraints and dimensions; tap highlights, the button deletes.
Pane {
    id: panel
    objectName: "constraintPanel"
    required property var controller
    required property var labelOf

    padding: 8
    background: Rectangle {
        color: panel.Material.dialogColor
        radius: 16
        border.color: panel.Material.dividerColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        Label {
            text: qsTr("Ограничения")
            font.pixelSize: 16
            font.weight: Font.Medium
            Layout.margins: 8
        }
        Label {
            visible: list.count === 0
            text: qsTr("Нет ограничений. Выберите объекты и нажмите действие внизу.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            Layout.fillWidth: true
            Layout.margins: 8
        }
        ListView {
            id: list
            objectName: "constraintList"
            model: panel.controller.constraints
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 52
                highlighted: panel.controller.selected_constraint === modelData.id
                onClicked: panel.controller.selected_constraint = modelData.id
                contentItem: RowLayout {
                    spacing: 6
                    Label {
                        text: panel.labelOf(row.modelData.kind)
                              + (row.modelData.value !== undefined
                                 ? " · " + Number(row.modelData.value.toFixed(3))
                                   + (row.modelData.kind === "angle" ? "°" : qsTr(" мм"))
                                 : "")
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: row.modelData.dependent || row.modelData.violated
                        text: row.modelData.violated ? qsTr("конфликт") : qsTr("избыточно")
                        color: row.modelData.violated ? Material.color(Material.Red)
                                                      : Material.color(Material.Orange)
                        font.pixelSize: 12
                    }
                    ToolButton {
                        objectName: "deleteConstraint_" + row.index
                        icon.source: "qrc:/icons/delete.svg"
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        onClicked: panel.controller.remove_constraint(row.modelData.id)
                    }
                }
            }
        }
    }
}
