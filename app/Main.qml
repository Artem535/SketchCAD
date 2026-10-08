import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Tablet shell from design/sketchcad-tablet.html, reduced to U01/U02 scope.
ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 800
    minimumWidth: 720
    minimumHeight: 480
    title: qsTr("SketchCAD")
    Material.theme: Material.System
    Material.accent: Material.Blue

    readonly property var tools: [
        { name: "select", label: qsTr("Выбор") },
        { name: "line", label: qsTr("Линия") },
        { name: "polyline", label: qsTr("Ломаная") },
        { name: "rectangle", label: qsTr("Прямоуг.") },
        { name: "circle", label: qsTr("Окружн.") },
        { name: "arc", label: qsTr("Дуга") }
    ]

    readonly property var actionLabels: ({
        coincident: qsTr("Совпадение"), horizontal: qsTr("Горизонтально"),
        vertical: qsTr("Вертикально"), parallel: qsTr("Параллельно"),
        perpendicular: qsTr("Перпендикулярно"), tangent: qsTr("Касание"),
        equal: qsTr("Равенство"), fix: qsTr("Фиксация"), length: qsTr("Длина"),
        distance: qsTr("Расстояние"), angle: qsTr("Угол"), radius: qsTr("Радиус")
    })
    readonly property var dimensionKeys: ["length", "distance", "angle", "radius"]

    function labelOf(key) { return actionLabels[key] || key }

    // Visible text for a controller message key.
    function describe(key) {
        switch (key) {
        case "point_in_use": return qsTr("Точка используется другой геометрией")
        case "conflict": return qsTr("Конфликт ограничений — изменение отменено")
        case "invalid_value": return qsTr("Значение должно быть конечным числом в допустимых пределах")
        case "invalid_geometry": return qsTr("Геометрия вырождена — ограничение не определено")
        case "numerical_failure": return qsTr("Решатель не сошёлся — изменение отменено; попробуйте меньшее изменение значения")
        case "degenerate": return qsTr("Ограничение схлопывает геометрию — изменение отменено")
        case "invalid_action": return qsTr("Ограничение не подходит к выбранным объектам")
        case "invalid_dimension": return qsTr("Размер не подходит к выбранным объектам")
        }
        return ""
    }

    function diagnosisText() {
        switch (sketch.diagnosis) {
        case "conflicting": return qsTr("Конфликт")
        case "unknown": return qsTr("Диагностика недоступна")
        case "redundant": return qsTr("Избыточно · DOF %1").arg(sketch.dof)
        }
        return sketch.dof === 0 ? qsTr("Полностью определён · DOF 0")
                                : qsTr("Свободно: DOF %1").arg(sketch.dof)
    }

    function applyAction(key) {
        const id = sketch.apply(key)
        if (id !== 0 && dimensionKeys.indexOf(key) >= 0) editor.edit(id)
    }

    function statusText() {
        if (sketch.message !== "")
            return describe(sketch.message)
        if (sketch.in_progress) {
            if (sketch.tool === "polyline")
                return qsTr("Касание — вершина; первая точка замыкает, «Готово» — завершить")
            return qsTr("Укажите следующую точку · Esc — отмена")
        }
        return qsTr("Объектов: %1").arg(sketch.entity_count)
    }

    header: ToolBar {
        Material.elevation: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 8
            Label {
                text: qsTr("Эскиз 01")
                font.pixelSize: 18
                font.weight: Font.Medium
            }
            Label {
                objectName: "statusText"
                text: window.statusText()
                opacity: 0.7
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                objectName: "dofStatus"
                flat: true
                text: window.diagnosisText()
                Material.foreground: sketch.diagnosis === "conflicting" ? Material.Red
                                   : sketch.diagnosis === "redundant" ? Material.Orange
                                   : sketch.dof === 0 ? Material.Green : undefined
                onClicked: sketch.highlight_dependent()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Показать избыточные и конфликтующие ограничения")
            }
            ToolButton {
                objectName: "undoButton"
                icon.source: "qrc:/icons/undo.svg"
                enabled: sketch.can_undo
                onClicked: sketch.undo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Отменить")
            }
            ToolButton {
                objectName: "redoButton"
                icon.source: "qrc:/icons/redo.svg"
                enabled: sketch.can_redo
                onClicked: sketch.redo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Повторить")
            }
        }
    }

    SketchViewport {
        id: viewport
        anchors.fill: parent
        controller: sketch
        onDimensionClicked: (id) => editor.edit(id)
    }

    // Left tool rail.
    Pane {
        id: rail
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: 80
        padding: 4
        background: Rectangle {
            color: window.Material.dialogColor
            radius: 16
            border.color: window.Material.dividerColor
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 2
            Repeater {
                model: window.tools
                delegate: ToolButton {
                    required property var modelData
                    objectName: "tool_" + modelData.name
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    display: AbstractButton.TextUnderIcon
                    icon.source: "qrc:/icons/" + modelData.name + ".svg"
                    text: modelData.label
                    font.pixelSize: 11
                    checkable: true
                    checked: sketch.tool === modelData.name
                    onClicked: sketch.tool = modelData.name
                }
            }
            MenuSeparator { Layout.fillWidth: true }
            ToolButton {
                objectName: "snapToggle"
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                display: AbstractButton.TextUnderIcon
                icon.source: "qrc:/icons/snap.svg"
                text: qsTr("Привязка")
                font.pixelSize: 11
                checkable: true
                checked: sketch.snap_enabled
                onToggled: sketch.snap_enabled = checked
            }
            ToolButton {
                objectName: "fingerToggle"
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                display: AbstractButton.TextUnderIcon
                icon.source: "qrc:/icons/finger.svg"
                text: qsTr("Палец")
                font.pixelSize: 11
                checkable: true
                checked: sketch.finger_draws
                onToggled: sketch.finger_draws = checked
            }
            ToolButton {
                objectName: "constraintsToggle"
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                display: AbstractButton.TextUnderIcon
                icon.source: "qrc:/icons/constraints.svg"
                text: qsTr("Связи")
                font.pixelSize: 11
                checkable: true
                checked: constraintPanel.visible
                onToggled: constraintPanel.visible = checked
            }
            Item { Layout.fillHeight: true }
        }
    }

    ConstraintPanel {
        id: constraintPanel
        visible: false
        controller: sketch
        labelOf: window.labelOf
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: zoomPane.top
        anchors.margins: 8
        width: 300
    }

    DimensionEditor {
        id: editor
        controller: sketch
        describe: window.describe
    }

    // Context bar: selection actions or the shape in progress.
    Pane {
        id: contextBar
        objectName: "contextBar"
        visible: sketch.has_selection || sketch.in_progress
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        padding: 6
        background: Rectangle {
            color: window.Material.dialogColor
            radius: height / 2
            border.color: window.Material.dividerColor
        }
        RowLayout {
            spacing: 4
            Repeater {
                model: sketch.in_progress ? [] : sketch.applicable
                delegate: Button {
                    required property string modelData
                    objectName: "action_" + modelData
                    flat: true
                    text: window.labelOf(modelData)
                    Layout.preferredHeight: 48
                    onClicked: window.applyAction(modelData)
                }
            }
            ToolSeparator { visible: sketch.applicable.length > 0 && !sketch.in_progress }
            Button {
                objectName: "deleteSelection"
                visible: sketch.has_selection
                flat: true
                icon.source: "qrc:/icons/delete.svg"
                text: qsTr("Удалить")
                Material.foreground: Material.Red
                onClicked: sketch.delete_selection()
            }
            Button {
                objectName: "finishShape"
                visible: sketch.in_progress && sketch.tool === "polyline"
                flat: true
                icon.source: "qrc:/icons/check.svg"
                text: qsTr("Готово")
                onClicked: sketch.finish()
            }
            Button {
                objectName: "closeContext"
                flat: true
                icon.source: "qrc:/icons/close.svg"
                text: sketch.in_progress ? qsTr("Отмена") : ""
                onClicked: sketch.in_progress ? sketch.cancel()
                                              : sketch.clear_selection()
            }
        }
    }

    // Zoom controls.
    Pane {
        id: zoomPane
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        padding: 2
        background: Rectangle {
            color: window.Material.dialogColor
            radius: width / 2
            border.color: window.Material.dividerColor
        }
        ColumnLayout {
            spacing: 0
            ToolButton {
                objectName: "zoomIn"
                icon.source: "qrc:/icons/zoom_in.svg"
                onClicked: sketch.zoom_at(viewport.width / 2,
                                          viewport.height / 2, 1.25)
            }
            ToolButton {
                objectName: "zoomOut"
                icon.source: "qrc:/icons/zoom_out.svg"
                onClicked: sketch.zoom_at(viewport.width / 2,
                                          viewport.height / 2, 0.8)
            }
            ToolButton {
                objectName: "fitView"
                icon.source: "qrc:/icons/fit.svg"
                onClicked: sketch.fit()
            }
        }
    }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: sketch.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: sketch.redo() }
    // Disabled while the dimension editor owns the keyboard.
    Shortcut {
        enabled: !editor.opened
        sequences: ["Escape"]
        onActivated: sketch.cancel() || sketch.clear_selection()
    }
    Shortcut {
        enabled: !editor.opened
        sequences: [StandardKey.Delete, "Backspace"]
        onActivated: {
            if (constraintPanel.visible && sketch.selected_constraint !== 0)
                sketch.remove_constraint(sketch.selected_constraint)
            else
                sketch.delete_selection()
        }
    }
    Shortcut {
        enabled: !editor.opened
        sequences: ["Return", "Enter"]
        onActivated: sketch.finish()
    }
}
