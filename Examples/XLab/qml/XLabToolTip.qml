import QtQuick
import QtQuick.Controls

ToolTip {
    id: tip
    objectName: "xlabToolTip"
    width: Math.min(280, implicitWidth)
    x: (parent.width - width) / 2
    y: -implicitHeight - 8
    margins: 8
    leftPadding: 12; rightPadding: 12
    topPadding: 8; bottomPadding: 8
    delay: 600
    timeout: 5000
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    contentItem: Text {
        text: tip.text
        font.family: Qt.application.font.family
        font.pixelSize: 12
        color: "#f3f4f6"
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        color: "#24272e"
        radius: 8
        border.width: 1
        border.color: "#3b4049"
    }
}
