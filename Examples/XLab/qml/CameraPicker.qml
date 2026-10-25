import QtQuick
import QtQuick.Controls

Pill {
    id: picker
    required property var controller
    objectName: "cameraButton"
    text: controller.cameraName || "Select Camera"
    implicitWidth: Math.min(218, label.implicitWidth + 68)
    enabled: !controller.busy && !controller.active
    ToolTip.text: text
    Accessible.name: "Select Camera"
    onClicked: cameraPopup.open()

    contentItem: Item {
        Row {
            anchors.centerIn: parent; spacing: 8
            Icon { kind: "camera"; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter; tint: "#111318" }
            Text {
                id: label
                text: picker.text; width: Math.min(implicitWidth, picker.width - 68)
                elide: Text.ElideRight; color: "#111318"; font.pixelSize: 12
                font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : Qt.application.font.family
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon { kind: "chevron"; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter; tint: "#111318" }
        }
    }

    Popup {
        id: cameraPopup; objectName: "cameraPopup"
        x: picker.width - width; y: picker.height + 8; width: 276; padding: 8
        background: Rectangle { color: "white"; radius: 12; border.color: "#e5e7eb" }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: picker.controller.cameras
                delegate: ItemDelegate {
                    id: option
                    required property var modelData
                    required property int index
                    objectName: "cameraOption_" + index
                    width: 260; height: 34
                    text: modelData.name + (highlighted ? "  ✓" : "")
                    highlighted: picker.controller.cameraId === modelData.id
                    ToolTip.visible: hovered; ToolTip.text: modelData.name
                    background: Rectangle { radius: 8; color: option.highlighted ? "#eef1ff" : option.hovered ? "#f3f4f6" : "transparent" }
                    contentItem: Text {
                        text: option.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                        color: option.highlighted ? "#2f46e2" : "#111318"; font.pixelSize: 12
                        font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : Qt.application.font.family
                    }
                    onClicked: { picker.controller.cameraId = modelData.id; cameraPopup.close() }
                }
            }
            Text {
                visible: picker.controller.cameras.length === 0
                width: 260; height: visible ? 36 : 0
                text: "No cameras found"; leftPadding: 8; verticalAlignment: Text.AlignVCenter
                color: "#6b7380"; font.pixelSize: 12
            }
            Item {
                width: 260; height: 12
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: "#e5e7eb" }
            }
            ItemDelegate {
                id: refresh; objectName: "cameraRefresh"
                width: 260; height: 34; text: "Refresh Cameras"
                background: Rectangle { radius: 8; color: refresh.hovered ? "#f3f4f6" : "transparent" }
                contentItem: Text { text: refresh.text; color: "#111318"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                onClicked: picker.controller.refreshCameras()
            }
            CheckDelegate {
                objectName: "cameraMirror"
                width: 260; height: 34
                text: "Mirror preview and result"
                checked: picker.controller.videoMirrored
                onClicked: picker.controller.videoMirrored = !picker.controller.videoMirrored
                ToolTip.visible: hovered
                ToolTip.text: "Display only; the published video is unchanged"
            }
        }
    }

    onEnabledChanged: if (!enabled) cameraPopup.close()
}
