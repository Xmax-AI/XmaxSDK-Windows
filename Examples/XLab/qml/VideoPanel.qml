import QtQuick
import QtQuick.Controls
import QtMultimedia
import QtQuick.Effects

Rectangle {
    id: panel
    property string title: "Local"
    property bool result: false
    property var controller
    readonly property alias sink: video.videoSink
    color: "black"
    radius: 24
    layer.enabled: !softwareRendering
    layer.effect: MultiEffect { maskEnabled: true; maskSource: mask }
    Rectangle { id: mask; anchors.fill: parent; radius: 24; color: "white"; visible: false; layer.enabled: true }
    VideoOutput { id: video; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
    Text {
        anchors.centerIn: parent
        visible: panel.result && !controller.remoteReady
        text: controller.preview ? "Waiting for connection…" : "Generating…"
        color: "#8cffffff"; font.pixelSize: 14
    }
    Column {
        visible: controller.statisticsVisible
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 14
        spacing: 8
        Repeater {
            model: panel.result ? [
                [["Downlink resolution", "downResolution"], ["Downlink frame rate", "downFps"], ["Downlink bitrate", "downBitrate"]],
                [["Playback buffer delay", "buffer"], ["Downlink packet loss", "downLoss"], ["Downlink network quality", "downQuality"], ["RTT (cloud)", "rtt"], ["E2E (RTC estimate)", "e2e"]]
            ] : [
                [["Create session", "session"], ["RTC join", "join"], ["Publish local stream", "publish"], ["First frame", "firstFrame"], ["Total startup", "total"]],
                [["Uplink resolution", "upResolution"], ["Uplink frame rate", "upFps"], ["Uplink bitrate", "upBitrate"]],
                [["Uplink packet loss", "upLoss"], ["Uplink network quality", "upQuality"], ["Uplink RTT", "upRtt"]]
            ]
            delegate: Rectangle {
                required property var modelData
                width: 250; height: rows.height + 16
                radius: 10; color: "#99000000"; border.color: "#1fffffff"
                Column {
                    id: rows
                    x: 10; y: 8; width: parent.width - 20
                    Repeater {
                        model: modelData
                        delegate: Item {
                            required property var modelData
                            width: rows.width; height: modelData[1] === "total" ? 25 : 17
                            Rectangle { visible: modelData[1] === "total"; width: parent.width; height: 1; color: "#30ffffff" }
                            Text { y: modelData[1] === "total" ? 7 : 0; text: modelData[0]; color: "#e6ffffff"; font.pixelSize: 11; font.weight: modelData[1] === "total" ? Font.DemiBold : Font.Normal }
                            Text { anchors.right: parent.right; y: modelData[1] === "total" ? 7 : 0; text: controller.metrics[modelData[1]] || "—"; color: "#e6ffffff"; font.pixelSize: 11; font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas" }
                        }
                    }
                }
            }
        }
    }
    Text { visible: panel.result; anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 16; text: "✦ Xmax"; color: "#ccffffff"; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        x: 14; y: parent.height - 35; width: tag.width + 16; height: 23; color: "#59000000"; radius: 8
        Text { id: tag; anchors.centerIn: parent; text: panel.title; color: "#a6ffffff"; font.pixelSize: 12 }
    }
}
