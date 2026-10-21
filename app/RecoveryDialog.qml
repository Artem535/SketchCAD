import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Recovery list after a crash (autosave-recovery.adoc): restore, delete or
// keep every automatic copy; nothing is restored without a choice.
Dialog {
    id: dialog
    objectName: "recoveryDialog"
    required property var theme
    property string errorText: ""
    width: Math.min(640, parent ? parent.width - 40 : 640)
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Восстановление после сбоя")

    function statusText(status) {
        const texts = {
            valid: qsTr("Можно восстановить"),
            invalid: qsTr("Копия повреждена"),
            unsupported: qsTr("Копия из более новой версии SketchCAD"),
            too_large: qsTr("Копия слишком большая"),
            unreadable: qsTr("Не удалось прочитать копию")
        }
        return texts[status] || texts.invalid
    }
    function show() {
        errorText = ""
        recovery.refresh()
        open()
    }

    Connections {
        target: recovery
        function onError(key) {
            const texts = {
                recovery_locked: qsTr("Эту копию сейчас использует другое окно SketchCAD."),
                recovery_missing: qsTr("Копия уже удалена или восстановлена."),
                recovery_discard_failed: qsTr("Не удалось удалить копию. Проверьте доступ к папке.")
            }
            dialog.errorText = texts[key] || qsTr("Не удалось выполнить действие с копией.")
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: recovery.candidates.length
                  ? qsTr("SketchCAD завершился неожиданно. Найдены автоматические копии несохранённой работы. Исходные файлы проектов не изменены.")
                  : qsTr("Автоматических копий нет.")
        }
        Repeater {
            model: recovery.candidates
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "recoveryRow_" + index
                Layout.fillWidth: true
                implicitHeight: rowLayout.implicitHeight + 16
                radius: 8
                color: dialog.theme.surface
                border.color: dialog.theme.line
                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            objectName: "recoveryName_" + row.index
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.bold: true
                            text: row.modelData.source_name || qsTr("Без имени")
                        }
                        Label {
                            objectName: "recoveryStatus_" + row.index
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: row.modelData.restorable ? dialog.theme.ink : dialog.theme.err
                            text: row.modelData.captured_at + " · " + dialog.statusText(row.modelData.status)
                        }
                    }
                    Button {
                        objectName: "recoveryRestore_" + row.index
                        text: qsTr("Восстановить")
                        enabled: row.modelData.restorable && !project.busy
                        onClicked: {
                            const id = row.modelData.session_id
                            dialog.close()
                            project.request_restore(id)
                        }
                    }
                    Button {
                        objectName: "recoveryDiscard_" + row.index
                        text: qsTr("Удалить копию")
                        flat: true
                        enabled: !project.busy
                        onClicked: {
                            dialog.errorText = ""
                            recovery.discard(row.modelData.session_id)
                        }
                    }
                }
            }
        }
        Label {
            objectName: "recoveryError"
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            wrapMode: Text.WordWrap
            color: dialog.theme.err
            text: dialog.errorText
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "recoveryLater"
            text: qsTr("Не сейчас")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    }
}
