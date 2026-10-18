import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Button {
    id: control
    property bool destructive: false
    property bool accent: false
    property bool primary: false
    property string glyph: ""
    property bool trailingGlyph: false
    property bool passive: false
    implicitHeight: primary ? 52 : 36
    implicitWidth: Math.max(36, body.implicitWidth + (primary ? 64 : 30))
    hoverEnabled: true
    padding: 0
    opacity: enabled ? 1 : 0.5
    focusPolicy: passive ? Qt.NoFocus : Qt.StrongFocus
    Accessible.name: text || ToolTip.text
    ToolTip.visible: hovered && ToolTip.text.length > 0
    ToolTip.delay: 600
    background: Rectangle {
        radius: height / 2
        border.width: 1
        border.color: control.primary ? "transparent" : control.destructive ? "#f1baba" : control.accent ? "#d7ddfb" : control.activeFocus ? "#2f46e2" : control.hovered ? "#c9ced6" : "#e2e5ea"
        gradient: Gradient {
            GradientStop { position: 0; color: control.primary ? (control.hovered ? "#2438c4" : "#2f46e2") : control.accent ? "#eef1ff" : "white" }
            GradientStop { position: 1; color: control.primary ? (control.hovered ? "#2438c4" : "#2f46e2") : control.destructive ? "#fdf2f2" : control.accent ? "#eef1ff" : "#f3f4f6" }
        }
        layer.enabled: !softwareRendering
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: control.primary ? "#592f46e2" : "#16101828"; shadowBlur: control.primary ? 0.7 : 0.3; shadowVerticalOffset: control.primary ? 7 : 3 }
    }
    contentItem: Item {
      Row {
        id: body
        anchors.centerIn: parent
        spacing: 8
        layoutDirection: control.trailingGlyph ? Qt.RightToLeft : Qt.LeftToRight
        Icon { visible: control.glyph.length > 0; kind: control.glyph; width: control.primary ? 20 : 16; height: width; anchors.verticalCenter: parent.verticalCenter; tint: label.color }
        Text {
            id: label
            text: control.text
            visible: text.length > 0
            font.family: control.font.family
            font.pixelSize: control.primary ? 16 : 13
            font.weight: control.primary || control.destructive ? Font.DemiBold : Font.Normal
            color: control.primary ? "white" : control.destructive ? "#dc2626" : control.accent ? "#2f46e2" : control.passive ? "#6b7380" : "#111318"
        }
      }
    }
}
