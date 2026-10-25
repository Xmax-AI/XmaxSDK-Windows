import QtQuick

QtObject {
    id: entry
    required property Item page
    required property Translate shift
    required property var controller
    readonly property bool running: animation.running

    function reveal() {
        animation.stop()
        page.opacity = 1
        shift.y = 0
        if (page.visible && !controller.reduceMotion())
            animation.restart()
    }

    property ParallelAnimation animation: ParallelAnimation {
        NumberAnimation {
            target: entry.page; property: "opacity"
            from: 0; to: 1; duration: 450
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0, 0, 0.58, 1, 1, 1]
        }
        NumberAnimation {
            target: entry.shift; property: "y"
            from: 14; to: 0; duration: 450
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0, 0, 0.58, 1, 1, 1]
        }
    }

    property Connections visibilityListener: Connections {
        target: entry.page
        function onVisibleChanged() { entry.reveal() }
    }
    Component.onCompleted: reveal()
}
