import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1040
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: "SketchCAD — Rectangle prototype"
    color: "#edf1f5"
    font.family: "Sans Serif"
    font.pixelSize: 16
    header: Rectangle {
        height: 72
        color: "#ffffff"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            Label { text: "SketchCAD"; font.pixelSize: 26; font.bold: true; color: "#1e3348" }
            Item { Layout.fillWidth: true }
            Label { text: "Rectangle sketch"; color: "#53687a" }
            Label { text: "mm"; font.bold: true; color: "#1e3348" }
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 1
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: "#ffffff"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14
                Label { text: "Dimensions"; font.pixelSize: 22; font.bold: true; color: "#1e3348" }
                Label { text: "Width, mm" }
                TextField { id: widthInput; objectName: "widthInput"; text: "50"; Layout.fillWidth: true; Layout.preferredHeight: 48; inputMethodHints: Qt.ImhFormattedNumbersOnly; selectByMouse: true }
                Label { text: "Height, mm" }
                TextField { id: heightInput; objectName: "heightInput"; text: "30"; Layout.fillWidth: true; Layout.preferredHeight: 48; inputMethodHints: Qt.ImhFormattedNumbersOnly; selectByMouse: true }
                Button {
                    objectName: "applyDimensions"
                    text: "Apply dimensions"; Layout.fillWidth: true; Layout.preferredHeight: 48
                    onClicked: sketch.resize(Number(widthInput.text), Number(heightInput.text), sketch.anchored)
                }
                CheckBox {
                    id: anchorBox
                    objectName: "anchorOrigin"
                    text: "Fix corner at origin"
                    checked: sketch.anchored
                    onClicked: {
                        sketch.resize(Number(widthInput.text), Number(heightInput.text), checked)
                        checked = sketch.anchored
                    }
                }
                Label {
                    text: sketch.anchored ? "Corner fixed at (0, 0). Unfix it to move the rectangle." : "Drag a blue corner to move the rectangle. Dimensions stay fixed."
                    wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#53687a"
                }
                Item { Layout.fillHeight: true }
                Button { text: "Try conflicting width"; objectName: "conflictProbe"; Layout.fillWidth: true; Layout.preferredHeight: 48; onClicked: sketch.conflict() }
                Label { text: "Adds a second width +10 mm. The sketch must stay unchanged."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#53687a"; font.pixelSize: 13 }
            }
        }
        Canvas {
            id: canvas
            objectName: "sketchCanvas"
            Layout.fillWidth: true
            Layout.fillHeight: true
            property real scale: 4
            property real originX: width / 2 - 100
            property real originY: height / 2 + 60
            function px(p) { return originX + p.x * scale }
            function py(p) { return originY - p.y * scale }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#edf1f5"
                ctx.fillRect(0, 0, width, height)
                ctx.strokeStyle = "#dce3eb"
                ctx.lineWidth = 1
                ctx.beginPath()
                for (let x = originX % 40; x < width; x += 40) { ctx.moveTo(x, 0); ctx.lineTo(x, height) }
                for (let y = originY % 40; y < height; y += 40) { ctx.moveTo(0, y); ctx.lineTo(width, y) }
                ctx.stroke()
                ctx.strokeStyle = "#a4b3c2"
                ctx.beginPath(); ctx.moveTo(originX, 0); ctx.lineTo(originX, height); ctx.moveTo(0, originY); ctx.lineTo(width, originY); ctx.stroke()
                const p = sketch.points
                if (p.length !== 4) return
                ctx.beginPath(); ctx.moveTo(px(p[0]), py(p[0]))
                for (let i = 1; i < 4; ++i) ctx.lineTo(px(p[i]), py(p[i]))
                ctx.closePath(); ctx.fillStyle = "#dae7f6"; ctx.fill()
                ctx.strokeStyle = "#246bc4"; ctx.lineWidth = 3; ctx.stroke()
                for (let i = 0; i < 4; ++i) {
                    ctx.beginPath(); ctx.arc(px(p[i]), py(p[i]), 7, 0, Math.PI * 2)
                    ctx.fillStyle = sketch.anchored && i === 0 ? "#1e3348" : "#246bc4"; ctx.fill()
                }
                ctx.fillStyle = "#1e3348"; ctx.font = "16px sans-serif"; ctx.textAlign = "center"
                ctx.fillText((p[1].x-p[0].x).toFixed(2) + " mm", (px(p[0])+px(p[1]))/2, py(p[2])-22)
                ctx.textAlign = "left"
                ctx.fillText((p[2].y-p[1].y).toFixed(2) + " mm", px(p[1])+18, (py(p[1])+py(p[2]))/2)
                ctx.font = "13px sans-serif"; ctx.fillStyle = "#53687a"; ctx.fillText("(0, 0)", originX+10, originY+22)
            }
            Connections { target: sketch; function onGeometry_changed() { canvas.requestPaint(); anchorBox.checked = sketch.anchored } }
            MouseArea {
                anchors.fill: parent
                enabled: !sketch.anchored
                property bool dragging: false
                property real lastX: 0
                property real lastY: 0
                onPressed: function(mouse) {
                    dragging = false
                    for (const p of sketch.points) {
                        if (Math.hypot(mouse.x-canvas.px(p), mouse.y-canvas.py(p)) <= 24) dragging = true
                    }
                    lastX = mouse.x; lastY = mouse.y
                }
                onPositionChanged: function(mouse) {
                    if (pressed && dragging) {
                        sketch.translate((mouse.x-lastX)/canvas.scale, -(mouse.y-lastY)/canvas.scale)
                        lastX = mouse.x; lastY = mouse.y
                    }
                }
                onReleased: dragging = false
                onCanceled: dragging = false
            }
        }
    }
    footer: Rectangle {
        height: 48; color: "#ffffff"
        Label { anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter; text: sketch.status; color: sketch.status.startsWith("Rejected") ? "#a02c30" : "#1e3348" }
        Label { anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter; text: "Grid 10 mm"; color: "#53687a" }
    }
}
