//
//  TrajectoryOverlay.qml
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

import QtQuick

Item {
    id: overlay
    required property var controller
    required property rect videoRect
    property point position
    property var strokes: []
    property var previous: null
    property bool tracking: false
    enabled: controller.remoteReady
    onEnabledChanged: if (!enabled) reset()

    function reset() {
        tracking = false
        previous = null
        strokes = []
        trail.requestPaint()
    }

    Connections {
        target: overlay.controller
        function onTrajectoryReset() { overlay.reset() }
    }

    function move(x, y) {
        position = Qt.point(Math.max(videoRect.x, Math.min(x, videoRect.x + videoRect.width)),
                            Math.max(videoRect.y, Math.min(y, videoRect.y + videoRect.height)))
    }

    function sample() {
        if (!controller.submitTrajectory(position.x, position.y, width, height))
            return
        strokes.push({from: previous || position, to: position, time: Date.now()})
        previous = position
        trail.requestPaint()
        animation.start()
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: mouse => {
            const rect = overlay.videoRect
            if (mouse.x < rect.x || mouse.x > rect.x + rect.width ||
                mouse.y < rect.y || mouse.y > rect.y + rect.height) {
                mouse.accepted = false
                return
            }
            overlay.tracking = true
            overlay.previous = null
            overlay.move(mouse.x, mouse.y)
            overlay.sample()
        }
        onPositionChanged: mouse => { if (overlay.tracking) overlay.move(mouse.x, mouse.y) }
        onReleased: mouse => {
            if (overlay.tracking) {
                overlay.move(mouse.x, mouse.y)
                overlay.sample()
            }
            overlay.tracking = false
            overlay.previous = null
        }
        onCanceled: overlay.reset()
    }

    Timer {
        interval: 33
        repeat: true
        running: overlay.enabled && overlay.tracking
        onTriggered: overlay.sample()
    }

    Timer {
        id: animation
        interval: 16
        repeat: true
        onTriggered: {
            const now = Date.now()
            overlay.strokes = overlay.strokes.filter(stroke => now - stroke.time < 420)
            trail.requestPaint()
            if (overlay.strokes.length === 0)
                stop()
        }
    }

    Canvas {
        id: trail
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = "#3aff89"
            context.lineWidth = 5
            context.lineCap = "round"
            const now = Date.now()
            for (const stroke of overlay.strokes) {
                context.globalAlpha = Math.max(0, 1 - (now - stroke.time) / 420)
                context.beginPath()
                context.moveTo(stroke.from.x, stroke.from.y)
                context.lineTo(stroke.to.x + 0.01, stroke.to.y)
                context.stroke()
            }
            context.globalAlpha = 1
        }
    }
}
