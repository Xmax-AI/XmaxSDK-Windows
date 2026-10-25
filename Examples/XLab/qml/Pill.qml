import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Button {
    id: control
    property bool destructive: false
    property bool accent: false
    property bool primary: false
    property bool recordingStyle: false
    property bool secondaryForeground: false
    property bool showsShadow: true
    property string glyph: ""
    property bool trailingGlyph: false
    property bool passive: false
    property bool loading: false
    property real labelSize: primary ? 16 : 13
    property real glyphSize: primary ? 20 : 16
    property real contentSpacing: 8
    property real contentHorizontalPadding: primary ? 32 : recordingStyle ? 14 : 15
    implicitHeight: primary ? 52 : 36
    implicitWidth: Math.max(36, body.implicitWidth + contentHorizontalPadding * 2)
    hoverEnabled: true
    padding: 0
    opacity: enabled || loading ? 1 : 0.5
    focusPolicy: passive ? Qt.NoFocus : Qt.StrongFocus
    Accessible.name: text || ToolTip.text
    XLabToolTip {
        text: control.ToolTip.text
        visible: control.hovered && control.enabled && text.length > 0
    }
    background: Rectangle {
        radius: height / 2
        border.width: 1
        border.color: control.primary ? "transparent" : control.destructive ? "#f1baba" : control.accent ? "#d7ddfb" : control.visualFocus ? "#2f46e2" : control.hovered ? "#c9ced6" : "#e2e5ea"
        gradient: Gradient {
            GradientStop { position: 0; color: control.primary ? (control.hovered ? "#2438c4" : "#2f46e2") : control.accent ? "#eef1ff" : "white" }
            GradientStop {
                position: 1
                color: {
                    const base = control.primary ? (control.hovered ? "#2438c4" : "#2f46e2") : control.destructive ? "#fdf2f2" : control.accent ? "#eef1ff" : "#f3f4f6"
                    return control.down && (control.primary || control.recordingStyle) ? Qt.darker(base, 1.06) : base
                }
            }
        }
        layer.enabled: !softwareRendering
        layer.effect: MultiEffect {
            shadowEnabled: control.showsShadow && !(control.recordingStyle && control.accent)
            shadowColor: control.primary ? "#2f46e2" : "#16101828"
            shadowOpacity: control.primary ? 0.35 : 1
            blurMax: control.primary ? 16 : 32
            shadowBlur: control.primary ? 1 : 0.3
            shadowVerticalOffset: control.primary ? 10 : 3
        }
    }
    contentItem: Item {
      Row {
        id: body
        anchors.centerIn: parent
        spacing: control.contentSpacing
        layoutDirection: control.trailingGlyph ? Qt.RightToLeft : Qt.LeftToRight
        Icon { visible: !control.loading && control.glyph.length > 0; kind: control.glyph; width: control.glyphSize; height: width; anchors.verticalCenter: parent.verticalCenter; tint: label.color }
        BusyIndicator { visible: control.loading; running: visible; width: control.glyphSize; height: width; anchors.verticalCenter: parent.verticalCenter }
        Text {
            id: label
            text: control.text
            visible: text.length > 0
            font.family: control.font.family
            font.pixelSize: control.labelSize
            font.weight: control.primary || control.destructive || control.recordingStyle ? Font.DemiBold : Font.Normal
            color: control.primary ? "white" : control.destructive ? "#dc2626" : control.accent ? "#2f46e2" : control.recordingStyle || control.secondaryForeground ? (control.hovered && control.enabled ? "#111318" : "#8b919d") : control.passive ? "#6b7380" : "#111318"
        }
      }
    }
}
