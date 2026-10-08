import QtQuick
import QtQuick.Controls.Material
import QtQuick.Shapes

// Sketch canvas: renders the controller's SVG layers and forwards input.
// All geometry, picking and snapping live in C++ (ADR-0002).
Item {
    id: root
    objectName: "sketchCanvas"
    required property var controller
    clip: true

    onWidthChanged: controller.set_viewport_size(width, height)
    onHeightChanged: controller.set_viewport_size(width, height)
    Component.onCompleted: controller.set_viewport_size(width, height)

    readonly property bool dark: Material.theme === Material.Dark
    readonly property color geometryColor: dark ? "#cfe3f7" : "#1d3b5a"
    readonly property color accent: Material.accentColor

    Rectangle {
        anchors.fill: parent
        color: root.dark ? "#161e25" : "#f7f9fb"
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.dark ? "#232f3a" : "#e1e8ef"
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.controller.grid_minor_path }
        }
        ShapePath {
            strokeColor: root.dark ? "#2f3e4b" : "#cdd8e2"
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.controller.grid_major_path }
        }
        ShapePath {
            strokeColor: root.dark ? "#5d7083" : "#9fb0c0"
            strokeWidth: 1.5
            fillColor: "transparent"
            PathSvg { path: root.controller.axes_path }
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
            fillColor: root.dark ? "#161e25" : "#ffffff"
            PathSvg { path: root.controller.points_path }
        }
        ShapePath {
            strokeColor: root.accent
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: root.controller.selected_path }
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
