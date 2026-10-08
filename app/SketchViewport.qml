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
        ShapePath {
            strokeColor: root.geometryColor
            strokeWidth: 2.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.controller.geometry_path }
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
            TapHandler {
                onTapped: root.dimensionClicked(label.modelData.id)
            }
        }
    }

    // Snap indicator: ring on a point, small cross on the grid.
    Rectangle {
        objectName: "snapIndicator"
        visible: root.controller.snap_visible
        width: root.controller.snap_kind === "point" ? 18 : 10
        height: width
        radius: root.controller.snap_kind === "point" ? width / 2 : 0
        x: root.controller.snap_x - width / 2
        y: root.controller.snap_y - height / 2
        color: "transparent"
        border.color: root.accent
        border.width: 2
        rotation: root.controller.snap_kind === "point" ? 0 : 45
    }

    HoverHandler {
        id: hover
        onPointChanged: root.controller.hover(point.position.x,
                                              point.position.y)
    }

    // Drives the active tool: mouse, touchpad, stylus, and touch when
    // fingers draw. Wayland reports laptop touchpads as TouchPad.
    PointHandler {
        id: toolPoint
        objectName: "toolPoint"
        property point last
        acceptedButtons: Qt.LeftButton
        acceptedDevices: root.controller.finger_draws
                         ? PointerDevice.AllDevices
                         : PointerDevice.AllDevices & ~PointerDevice.TouchScreen
        onActiveChanged: {
            if (active) {
                last = point.position
                root.controller.press(last.x, last.y)
            } else {
                root.controller.release(last.x, last.y)
            }
        }
        onPointChanged: {
            if (!active) return
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
