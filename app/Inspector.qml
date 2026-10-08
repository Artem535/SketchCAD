import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Right properties panel: selection geometry and the constraint list.
Rectangle {
    id: panel
    objectName: "inspector"
    required property var controller
    required property var theme
    required property var labelOf
    required property var help
    required property var helpPopup
    property bool open: false

    width: 324
    x: open ? parent.width - width - 8 : parent.width + 8
    visible: x < parent.width
    radius: 20
    color: theme.surface
    border.color: theme.dark ? "transparent" : theme.line
    Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    component SectionTitle: Label {
        font.pixelSize: 12
        font.weight: Font.Medium
        font.letterSpacing: 0.5
        font.capitalization: Font.AllUppercase
        color: panel.theme.muted
        Layout.topMargin: 18
        Layout.bottomMargin: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 8
        anchors.topMargin: 12
        anchors.bottomMargin: 18
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 0
                Label {
                    text: qsTr("Свойства")
                    font.pixelSize: 12
                    color: panel.theme.muted
                }
                Label {
                    objectName: "inspectorTitle"
                    text: panel.controller.selection_title
                    font.pixelSize: 18
                    font.weight: Font.Medium
                    color: panel.theme.ink
                }
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                objectName: "closeInspector"
                icon.source: "qrc:/icons/close.svg"
                icon.color: panel.theme.muted
                onClicked: panel.open = false
            }
        }

        SectionTitle {
            visible: geometry.count > 0
            text: qsTr("Геометрия")
        }
        Repeater {
            id: geometry
            model: panel.controller.selection_properties
            delegate: Item {
                id: property
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.rightMargin: 10
                implicitHeight: 52
                RowLayout {
                    anchors.fill: parent
                    spacing: 10
                    Label {
                        objectName: "property_" + property.index
                        text: property.modelData.label
                        font.pixelSize: 14
                        color: panel.theme.muted
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        implicitWidth: Math.max(84, value.implicitWidth + 24)
                        implicitHeight: 40
                        radius: 10
                        color: panel.theme.surface2
                        Label {
                            id: value
                            objectName: "propertyValue_" + property.index
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: property.modelData.value
                            color: panel.theme.ink
                        }
                    }
                    Label {
                        text: property.modelData.unit
                        font.pixelSize: 13
                        color: panel.theme.muted
                        Layout.preferredWidth: 24
                    }
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: panel.theme.line
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            SectionTitle {
                text: qsTr("Ограничения")
                Layout.topMargin: 0
                Layout.bottomMargin: 0
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                objectName: "help_constraints"
                icon.source: "qrc:/icons/help.svg"
                icon.color: panel.theme.muted
                onClicked: panel.helpPopup.showList(qsTr("Ограничения и размеры"),
                                                    ["constraint", "dimension"])
            }
        }
        Label {
            visible: list.count === 0
            text: qsTr("Нет ограничений. Выберите объекты и нажмите действие внизу.")
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            color: panel.theme.muted
            Layout.fillWidth: true
            Layout.rightMargin: 10
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
                readonly property bool bad: modelData.dependent || modelData.violated
                readonly property string state: modelData.violated ? "conflict" : "redundant"
                objectName: "constraint_" + index
                width: ListView.view.width
                height: 56
                leftPadding: 0
                rightPadding: 0
                highlighted: panel.controller.selected_constraint === modelData.id
                onClicked: panel.controller.selected_constraint = modelData.id
                background: Rectangle {
                    radius: 10
                    color: row.highlighted ? panel.theme.primaryContainer : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle {
                        implicitWidth: 32
                        implicitHeight: 32
                        radius: 8
                        color: row.bad ? panel.theme.errContainer : "transparent"
                        border.color: row.bad ? panel.theme.err : panel.theme.line
                        Label {
                            anchors.centerIn: parent
                            text: panel.labelOf(row.modelData.kind).charAt(0)
                            color: row.bad ? panel.theme.err : panel.theme.primary
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: panel.labelOf(row.modelData.kind)
                                  + (row.modelData.value !== undefined
                                     ? " · " + Number(row.modelData.value.toFixed(3))
                                       + (row.modelData.kind === "angle" ? "°" : qsTr(" мм"))
                                     : "")
                            font.pixelSize: 14
                            color: panel.theme.ink
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "constraintHint_" + row.index
                            // A marked row says why instead of what.
                            text: row.bad
                                  ? panel.help.title(row.state) + " · " + panel.help.hint(row.state)
                                  : panel.help.hint(row.modelData.kind)
                            font.pixelSize: 12
                            color: row.bad ? panel.theme.err : panel.theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        id: badge
                        objectName: "constraintBadge_" + row.index
                        visible: row.bad
                        flat: true
                        icon.source: "qrc:/icons/help.svg"
                        icon.color: panel.theme.err
                        implicitWidth: 48
                        implicitHeight: 48
                        onClicked: panel.helpPopup.show(row.state)
                    }
                    ToolButton {
                        objectName: "deleteConstraint_" + row.index
                        icon.source: "qrc:/icons/delete.svg"
                        icon.color: panel.theme.muted
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        onClicked: panel.controller.remove_constraint(row.modelData.id)
                    }
                }
            }
        }
    }
}
