import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Tablet shell from design/sketchcad-tablet.html (tablet-shell.adoc).
ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 800
    minimumWidth: 720
    minimumHeight: 480
    title: qsTr("SketchCAD")
    color: theme.bg
    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.primary
    Material.primary: theme.surface
    Material.background: theme.bg
    Material.foreground: theme.ink
    font.pixelSize: 16

    Theme { id: theme }
    // Test hook and future settings: the active theme.
    property alias darkTheme: theme.dark

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
        return sketch.dof === 0 ? qsTr("DOF 0 · Полностью определён")
                                : qsTr("Свободно: DOF %1").arg(sketch.dof)
    }

    function applyAction(key) {
        const id = sketch.apply(key)
        if (id !== 0 && dimensionKeys.indexOf(key) >= 0) editor.edit(id)
    }

    function inputText() {
        if (sketch.in_progress) {
            if (sketch.tool === "polyline")
                return qsTr("Касание — вершина; первая точка замыкает, «Готово» — завершить")
            return qsTr("Укажите следующую точку · Esc — отмена")
        }
        return sketch.finger_draws ? qsTr("Палец рисует") : qsTr("Палец двигает вид")
    }

    // Rail button: icon over a short label, active tool highlighted.
    component RailButton: ToolButton {
        id: railButton
        property string iconName
        Layout.preferredWidth: 64
        Layout.preferredHeight: 56
        Layout.alignment: Qt.AlignHCenter
        display: AbstractButton.TextUnderIcon
        icon.source: "qrc:/icons/" + iconName + ".svg"
        icon.width: 26
        icon.height: 26
        icon.color: checked ? theme.primary : theme.muted
        font.pixelSize: 11
        font.weight: checked ? Font.Medium : Font.Normal
        spacing: 2
        padding: 2
        checkable: true
        Material.foreground: checked ? theme.primary : theme.muted
        background: Rectangle {
            radius: 16
            color: railButton.checked || railButton.down ? theme.primaryContainer
                                                         : "transparent"
        }
    }

    // Round 48 dp icon button of the app bar and the zoom control.
    component IconButton: ToolButton {
        property string iconName
        icon.source: "qrc:/icons/" + iconName + ".svg"
        icon.color: theme.ink
        implicitWidth: 48
        implicitHeight: 48
    }

    // Pill chip of the context bar.
    component Chip: Button {
        id: chip
        property color tint: theme.ink
        flat: true
        implicitHeight: 52
        leftPadding: 14
        rightPadding: 14
        topInset: 0
        bottomInset: 0
        font.pixelSize: 14
        font.capitalization: Font.MixedCase
        icon.color: tint
        Material.foreground: tint
        background: Rectangle {
            radius: 26
            color: chip.down ? theme.primaryContainer : "transparent"
        }
    }

    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Layout.topMargin: 8
        Layout.bottomMargin: 8
        color: theme.line
    }

    // Segment of the mode switch.
    component ModeButton: AbstractButton {
        id: modeButton
        property string caption
        implicitHeight: 42
        leftPadding: 16
        rightPadding: 16
        checkable: true
        opacity: enabled ? 1 : 0.5
        contentItem: Row {
            spacing: 4
            Label {
                text: modeButton.text
                font.pixelSize: 14
                font.weight: modeButton.checked ? Font.Medium : Font.Normal
                color: modeButton.checked ? theme.primary : theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                visible: modeButton.caption !== ""
                text: modeButton.caption
                font.pixelSize: 11
                color: theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        background: Rectangle {
            objectName: modeButton.objectName + "_background"
            radius: 21
            color: modeButton.checked ? theme.primaryContainer : "transparent"
        }
    }

    header: Rectangle {
        objectName: "appBar"
        height: 56
        color: theme.surface
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.line
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 6
            ColumnLayout {
                spacing: 0
                Label {
                    text: qsTr("Эскиз 01")
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    color: theme.ink
                }
                Label {
                    text: qsTr("Не сохранено · объектов: %1").arg(sketch.entity_count)
                    font.pixelSize: 12
                    color: theme.muted
                }
            }
            Rectangle {
                Layout.leftMargin: 8
                implicitWidth: modes.implicitWidth + 6
                implicitHeight: 48
                radius: 24
                color: theme.surface2
                Row {
                    id: modes
                    anchors.centerIn: parent
                    spacing: 2
                    ModeButton { objectName: "mode_sketch"; text: qsTr("Эскиз"); checked: true }
                    ModeButton { objectName: "mode_part"; text: qsTr("Деталь"); caption: "1.0"; enabled: false }
                    ModeButton { objectName: "mode_assembly"; text: qsTr("Сборка"); caption: "1.0"; enabled: false }
                    ModeButton { objectName: "mode_drawing"; text: qsTr("Чертёж"); caption: "1.0"; enabled: false }
                }
            }
            Item { Layout.fillWidth: true }
            AbstractButton {
                id: dof
                objectName: "dofStatus"
                readonly property string state: sketch.diagnosis === "conflicting"
                                                || sketch.diagnosis === "unknown" ? "err"
                                              : sketch.diagnosis === "redundant" ? "warn"
                                              : sketch.dof === 0 ? "ok" : "free"
                readonly property color tint: state === "err" ? theme.err
                                            : state === "warn" ? theme.warn
                                            : state === "ok" ? theme.ok : theme.primary
                text: window.diagnosisText()
                implicitHeight: 40
                leftPadding: 10
                rightPadding: 14
                onClicked: sketch.highlight_dependent()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Показать избыточные и конфликтующие ограничения")
                contentItem: Row {
                    spacing: 8
                    Label {
                        text: dof.state === "ok" ? "✓" : dof.state === "free" ? "↔" : "!"
                        color: dof.tint
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: dof.text
                        color: dof.tint
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                background: Rectangle {
                    objectName: "dofStatus_background"
                    radius: 20
                    color: dof.state === "err" ? theme.errContainer
                         : dof.state === "warn" ? theme.surface2
                         : dof.state === "ok" ? theme.okContainer : theme.primaryContainer
                }
            }
            IconButton {
                objectName: "undoButton"
                iconName: "undo"
                enabled: sketch.can_undo
                onClicked: sketch.undo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Отменить")
            }
            IconButton {
                objectName: "redoButton"
                iconName: "redo"
                enabled: sketch.can_redo
                onClicked: sketch.redo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Повторить")
            }
            IconButton {
                id: moreButton
                objectName: "moreButton"
                iconName: "more"
                onClicked: moreMenu.open()
                Menu {
                    id: moreMenu
                    objectName: "moreMenu"
                    y: moreButton.height
                    background: Rectangle {
                        implicitWidth: 240
                        radius: 12
                        color: theme.surface
                        border.color: theme.line
                    }
                    MenuItem {
                        objectName: "darkThemeToggle"
                        text: qsTr("Тёмная тема")
                        checkable: true
                        checked: theme.dark
                        onTriggered: theme.dark = !theme.dark
                    }
                    MenuItem {
                        objectName: "fingerMenuToggle"
                        text: qsTr("Палец рисует")
                        checkable: true
                        checked: sketch.finger_draws
                        onTriggered: sketch.finger_draws = !sketch.finger_draws
                    }
                }
            }
        }
    }

    SketchViewport {
        id: viewport
        anchors.fill: parent
        controller: sketch
        theme: theme
        onDimensionClicked: (id) => editor.edit(id)
    }

    // Floating tool rail.
    Rectangle {
        id: rail
        objectName: "toolRail"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: 72
        radius: 20
        color: theme.surface
        border.color: theme.dark ? "transparent" : theme.line
        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            spacing: 2
            Repeater {
                model: window.tools
                delegate: RailButton {
                    required property var modelData
                    objectName: "tool_" + modelData.name
                    iconName: modelData.name
                    text: modelData.label
                    checked: sketch.tool === modelData.name
                    onClicked: sketch.tool = modelData.name
                }
            }
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 1
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                color: theme.line
            }
            RailButton {
                objectName: "snapToggle"
                iconName: "snap"
                text: qsTr("Привязка")
                checked: sketch.snap_enabled
                onToggled: sketch.snap_enabled = checked
            }
            RailButton {
                objectName: "fingerToggle"
                iconName: "finger"
                text: qsTr("Палец")
                checked: sketch.finger_draws
                onToggled: sketch.finger_draws = checked
            }
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 1
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                color: theme.line
            }
            RailButton {
                objectName: "constraintsToggle"
                iconName: "constraints"
                text: qsTr("Связи")
                checked: inspector.open
                onToggled: inspector.open = checked
            }
            Item { Layout.fillHeight: true }
        }
    }

    // Units, snap and input mode.
    Rectangle {
        id: bottomStatus
        objectName: "bottomStatus"
        readonly property string text: [qsTr("мм"),
            sketch.snap_enabled ? qsTr("Привязки: сетка %1 мм").arg(Number(sketch.grid_step.toFixed(3)))
                                : qsTr("Привязки выкл."),
            window.inputText()].join(" · ")
        anchors.left: rail.right
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        visible: !contextBar.visible || contextBar.x > x + width + 8
        width: statusLabel.implicitWidth + 24
        height: 28
        radius: 14
        color: theme.surface
        border.color: theme.dark ? "transparent" : theme.line
        Label {
            id: statusLabel
            anchors.centerIn: parent
            text: bottomStatus.text
            font.pixelSize: 12
            color: theme.muted
        }
    }

    Inspector {
        id: inspector
        controller: sketch
        theme: theme
        labelOf: window.labelOf
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
    }

    DimensionEditor {
        id: editor
        controller: sketch
        theme: theme
        describe: window.describe
    }

    // Context bar: selection actions or the shape in progress.
    Rectangle {
        id: contextBar
        objectName: "contextBar"
        // Centred in the free space between the rail and the zoom control.
        readonly property real freeLeft: rail.x + rail.width + 12
        readonly property real freeRight: zoomControl.x - 12
        visible: sketch.has_selection || sketch.in_progress
        x: Math.max(freeLeft, (freeLeft + freeRight - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: Math.min(chips.implicitWidth + 12, freeRight - freeLeft)
        height: 64
        radius: 28
        color: theme.surface
        border.color: theme.dark ? "transparent" : theme.line
        clip: true
        RowLayout {
            id: chips
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 6
            spacing: 4
            Repeater {
                model: sketch.in_progress ? [] : sketch.applicable
                delegate: Chip {
                    required property string modelData
                    objectName: "action_" + modelData
                    text: window.labelOf(modelData)
                    onClicked: window.applyAction(modelData)
                }
            }
            Separator { visible: sketch.applicable.length > 0 && !sketch.in_progress }
            Chip {
                objectName: "deleteSelection"
                visible: sketch.has_selection
                icon.source: "qrc:/icons/delete.svg"
                text: qsTr("Удалить")
                tint: theme.err
                onClicked: sketch.delete_selection()
            }
            Chip {
                objectName: "finishShape"
                visible: sketch.in_progress && sketch.tool === "polyline"
                icon.source: "qrc:/icons/check.svg"
                text: qsTr("Готово")
                tint: theme.primary
                onClicked: sketch.finish()
            }
            Chip {
                objectName: "closeContext"
                icon.source: "qrc:/icons/close.svg"
                text: sketch.in_progress ? qsTr("Отмена") : ""
                onClicked: sketch.in_progress ? sketch.cancel()
                                              : sketch.clear_selection()
            }
        }
    }

    // Zoom control; moves aside while the inspector is open.
    Rectangle {
        id: zoomControl
        objectName: "zoomControl"
        x: parent.width - width - 16 - (inspector.open ? inspector.width + 8 : 0)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 48
        height: zoomColumn.implicitHeight
        radius: 24
        color: theme.surface
        border.color: theme.dark ? "transparent" : theme.line
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Column {
            id: zoomColumn
            IconButton {
                objectName: "zoomIn"
                iconName: "zoom_in"
                onClicked: sketch.zoom_at(viewport.width / 2, viewport.height / 2, 1.25)
            }
            IconButton {
                objectName: "zoomOut"
                iconName: "zoom_out"
                onClicked: sketch.zoom_at(viewport.width / 2, viewport.height / 2, 0.8)
            }
            IconButton {
                objectName: "fitView"
                iconName: "fit"
                onClicked: sketch.fit()
            }
        }
    }

    // Rejected actions appear as a toast at the top.
    Rectangle {
        id: toast
        objectName: "toast"
        property string text: ""
        property string shownKey: ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        width: Math.min(toastLabel.implicitWidth + 40, 640)
        height: toastLabel.implicitHeight + 28
        radius: 14
        color: theme.err
        visible: false
        z: 10
        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 600)
            text: toast.text
            // White on the light error colour; dark text on the pale dark one.
            color: theme.dark ? theme.errContainer : "#ffffff"
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Timer {
            id: toastTimer
            interval: 2600
            onTriggered: toast.visible = false
        }
        Connections {
            target: sketch
            function onChanged() {
                if (sketch.message === toast.shownKey) return
                toast.shownKey = sketch.message
                const text = window.describe(sketch.message)
                if (text === "") return
                toast.text = text
                toast.visible = true
                toastTimer.restart()
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
            if (inspector.open && sketch.selected_constraint !== 0)
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
