import QtQuick
import QtQuick.Controls.Material
import QtQuick.Shapes

// Sketch canvas: renders the controller's SVG layers and forwards input.
// All geometry, picking and snapping live in C++ (ADR-0002).
Item {
    id: root
    objectName: "sketchCanvas"
    required property var controller
    required property var theme
    // A dimension label was tapped.
    signal dimensionClicked(var id)
    // A live dimension of the shape being drawn was tapped.
    signal previewDimensionClicked()
    clip: true

    onWidthChanged: controller.set_viewport_size(width, height)
    onHeightChanged: controller.set_viewport_size(width, height)
    Component.onCompleted: controller.set_viewport_size(width, height)

    readonly property color geometryColor: theme.geom
    readonly property color accent: theme.primary

    Rectangle {
        objectName: "canvasBackground"
        anchors.fill: parent
        color: root.theme.canvas
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.theme.grid
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.controller.grid_minor_path }
        }
        ShapePath {
            strokeColor: root.theme.line
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.controller.grid_major_path }
        }
        ShapePath {
            strokeColor: root.theme.muted
            strokeWidth: 1.5
            fillColor: "transparent"
            PathSvg { path: root.controller.axes_path }
        }
        // ESKD dimension lines and arrows (ADR-0004).
        ShapePath {
            strokeColor: root.theme.muted
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.controller.dimension_path }
        }
        ShapePath {
            strokeColor: root.theme.muted
            strokeWidth: 0.5
            fillColor: root.theme.muted
            PathSvg { path: root.controller.dimension_arrows_path }
        }
        // Live dimensions of the shape being drawn (U08).
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 2]
            fillColor: "transparent"
            PathSvg { path: root.controller.preview_dimension_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 0.5
            fillColor: root.accent
            PathSvg { path: root.controller.preview_dimension_arrows_path }
        }
        ShapePath {
            strokeColor: root.geometryColor
            strokeWidth: 2.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.controller.geometry_path }
        }
        // Construction geometry (U04): thin dashed, not part of the outline.
        ShapePath {
            strokeColor: root.theme.muted
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [6, 4]
            fillColor: "transparent"
            PathSvg { path: root.controller.construction_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            PathSvg { path: root.controller.preview_path }
        }
        ShapePath {
            strokeColor: root.geometryColor
            strokeWidth: 1.5
            fillColor: root.theme.surface
            PathSvg { path: root.controller.points_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 1.5
            fillColor: root.accent
            PathSvg { path: root.controller.fixed_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: root.controller.selected_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 3
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 2]
            fillColor: "transparent"
            PathSvg { path: root.controller.constraint_path }
        }
        ShapePath {
            strokeColor: root.theme.err
            strokeWidth: 4
            strokeStyle: ShapePath.DashLine
            dashPattern: [2.5, 1.25]
            fillColor: "transparent"
            PathSvg { path: root.controller.conflict_path }
        }
    }

    // Dimension texts above their dimension lines (ESKD), each a 48 px
    // touch target; (x, y) is the bottom centre of the text.
    Repeater {
        id: dimensionLabels
        model: root.controller.dimension_labels
        delegate: Item {
            id: label
            required property var modelData
            required property int index
            readonly property real radians: modelData.angle * Math.PI / 180
            objectName: "dimensionLabel_" + index
            width: Math.max(48, text.implicitWidth + 16)
            height: 48
            // Rotating about the centre equals rotating about the text's
            // bottom centre once the centre is moved up by half the text.
            x: modelData.x + Math.sin(radians) * text.implicitHeight / 2 - width / 2
            y: modelData.y - Math.cos(radians) * text.implicitHeight / 2 - height / 2
            rotation: modelData.angle
            Rectangle {
                anchors.centerIn: parent
                width: text.implicitWidth + 4
                height: text.implicitHeight - 4
                color: root.theme.canvas
                opacity: 0.85
            }
            Label {
                id: text
                anchors.centerIn: parent
                text: label.modelData.text
                color: label.modelData.bad ? root.theme.err : root.accent
                font.pixelSize: 15
                font.weight: Font.Medium
            }
        }
    }

    // ID of the committed dimension whose text is at `p` (canvas px), or 0.
    function dimensionAt(p) {
        for (let i = 0; i < dimensionLabels.count; ++i) {
            const label = dimensionLabels.itemAt(i)
            if (label && label.contains(label.mapFromItem(root, p.x, p.y)))
                return label.modelData.id
        }
        return 0
    }

    // True when `p` (canvas px) is on a live dimension: the drawing tool
    // leaves such presses to the label.
    function onLiveLabel(p) {
        for (let i = 0; i < liveLabels.count; ++i) {
            const label = liveLabels.itemAt(i)
            if (label && label.contains(label.mapFromItem(root, p.x, p.y))) return true
        }
        return false
    }

    Repeater {
        id: liveLabels
        model: root.controller.preview_dimension_labels
        delegate: Item {
            id: live
            required property var modelData
            required property int index
            readonly property real radians: modelData.angle * Math.PI / 180
            objectName: "previewDimensionLabel_" + index
            width: Math.max(48, liveText.implicitWidth + 16)
            height: 48
            x: modelData.x + Math.sin(radians) * liveText.implicitHeight / 2 - width / 2
            y: modelData.y - Math.cos(radians) * liveText.implicitHeight / 2 - height / 2
            rotation: modelData.angle
            Rectangle {
                anchors.centerIn: parent
                width: liveText.implicitWidth + 10
                height: liveText.implicitHeight + 2
                radius: 4
                color: root.theme.primaryContainer
            }
            Label {
                id: liveText
                anchors.centerIn: parent
                text: live.modelData.text
                color: root.accent
                font.pixelSize: 15
                font.weight: Font.Medium
            }
            TapHandler { onTapped: root.previewDimensionClicked() }
        }
    }

    // Snap indicator: ring on a point, small ring on a curve, × on an
    // intersection, small diamond on the grid.
    Item {
        id: snapIndicator
        objectName: "snapIndicator"
        readonly property string kind: root.controller.snap_kind
        visible: root.controller.snap_visible
        width: kind === "point" ? 18 : kind === "intersection" ? 16 : 10
        height: width
        x: root.controller.snap_x - width / 2
        y: root.controller.snap_y - height / 2
        Rectangle {
            visible: snapIndicator.kind !== "intersection"
            anchors.fill: parent
            radius: snapIndicator.kind === "grid" ? 0 : width / 2
            rotation: snapIndicator.kind === "grid" ? 45 : 0
            color: "transparent"
            border.color: root.accent
            border.width: 2
        }
        Repeater {
            model: snapIndicator.kind === "intersection" ? [45, -45] : []
            Rectangle {
                required property int modelData
                anchors.centerIn: parent
                width: parent.width
                height: 2.5
                radius: 1
                color: root.accent
                rotation: modelData
            }
        }
    }

    HoverHandler {
        id: hover
        // The rubber band holds still over its own live dimension, so the
        // label can be clicked instead of running away from the pointer.
        onPointChanged: if (!root.onLiveLabel(point.position))
                            root.controller.hover(point.position.x, point.position.y)
    }

    // Drives the active tool: mouse, touchpad, stylus, and touch when
    // fingers draw. Wayland reports laptop touchpads as TouchPad.
    PointHandler {
        id: toolPoint
        objectName: "toolPoint"
        property point last
        // The press started on a live dimension label (U08).
        property bool onLabel: false
        // The press started on a dimension text (U09): a tap edits it, a
        // drag moves it.
        property var dimensionId: 0
        property bool moved: false
        property point pressed
        acceptedButtons: Qt.LeftButton
        acceptedDevices: root.controller.finger_draws
                         ? PointerDevice.AllDevices
                         : PointerDevice.AllDevices & ~PointerDevice.TouchScreen
        onActiveChanged: {
            if (active) {
                last = point.position
                dimensionId = root.dimensionAt(last)
                moved = false
                pressed = last
                onLabel = dimensionId === 0 && root.onLiveLabel(last)
                if (dimensionId === 0 && !onLabel) root.controller.press(last.x, last.y)
            } else if (dimensionId !== 0) {
                if (moved) root.controller.end_dimension_drag()
                else root.dimensionClicked(dimensionId)
                dimensionId = 0
            } else if (onLabel) {
                onLabel = false
            } else {
                root.controller.release(last.x, last.y)
            }
        }
        onPointChanged: {
            if (!active || onLabel) return
            if (dimensionId !== 0) {
                const p = point.position
                if (!moved && Math.hypot(p.x - pressed.x, p.y - pressed.y) > 6)
                    moved = root.controller.begin_dimension_drag(dimensionId)
                if (moved) root.controller.drag_dimension(p.x, p.y)
                return
            }
            last = point.position
            root.controller.drag(last.x, last.y)
        }
    }

    // Middle or right mouse button pans.
    DragHandler {
        target: null
        acceptedButtons: Qt.MiddleButton | Qt.RightButton
        xAxis.onActiveValueChanged: (delta) => root.controller.pan(delta, 0)
        yAxis.onActiveValueChanged: (delta) => root.controller.pan(0, delta)
    }

    // One finger pans when fingers do not draw.
    DragHandler {
        target: null
        enabled: !root.controller.finger_draws
        acceptedDevices: PointerDevice.TouchScreen
        xAxis.onActiveValueChanged: (delta) => root.controller.pan(delta, 0)
        yAxis.onActiveValueChanged: (delta) => root.controller.pan(0, delta)
    }

    PinchHandler {
        target: null
        onTranslationChanged: (delta) => root.controller.pan(delta.x, delta.y)
        onScaleChanged: (delta) => root.controller.zoom_at(
                            centroid.position.x, centroid.position.y, delta)
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => root.controller.zoom_at(
                     event.x, event.y, Math.pow(1.0015, event.angleDelta.y))
    }
}
