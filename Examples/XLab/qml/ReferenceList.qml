import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: list
    required property var controller
    required property string mode
    property var items: { controller.references; return controller.referencesFor(mode) }
    property real savedY: 0
    property int columns: Math.max(1, Math.floor((width + 12) / 96))
    onVisibleChanged: {
        if (visible) Qt.callLater(function() { scroller.contentItem.contentY = savedY })
        else savedY = scroller.contentItem.contentY
    }
    ScrollView {
        id: scroller
        anchors.fill: parent; clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Column {
            width: list.width; spacing: 12
            Repeater {
                model: Math.ceil((list.items.length + 1) / list.columns)
                delegate: Row {
                    required property int index
                    property int rowIndex: index
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    Repeater {
                        model: Math.min(list.columns, list.items.length + 1 - rowIndex * list.columns)
                        delegate: Item {
                            id: cell
                            required property int index
                            property int position: rowIndex * list.columns + index
                            property bool upload: position === 0
                            property var entry: upload ? ({}) : list.items[position - 1]
                            objectName: "reference_" + list.mode + "_" + position
                            width: 84; height: 84
                            Rectangle {
                                id: circle
                                width: 64; height: 64; radius: 32; anchors.horizontalCenter: parent.horizontalCenter
                                color: "#f7f8fa"
                                border.color: controller.selected === cell.entry.id ? "#2f46e2" : hit.containsMouse ? "#c7d0f8" : "#f3f4f6"
                                border.width: cell.upload ? 1 : 2
                                RoundedImage {
                                    id: thumbnail
                                    anchors.fill: parent; anchors.margins: 2
                                    source: cell.upload || !list.visible ? "" : cell.entry.thumbnail || ""
                                }
                                BusyIndicator { anchors.centerIn: parent; width: 24; height: 24; running: thumbnail.status === RoundedImage.Loading; visible: running }
                                Icon { visible: !cell.upload && thumbnail.status === RoundedImage.Error; anchors.centerIn: parent; width: 22; height: 22; kind: "warning"; tint: "#6b7380" }
                                Icon { visible: cell.upload; anchors.centerIn: parent; width: 22; height: 22; kind: "plus"; tint: "#6b7380" }
                                Rectangle {
                                    anchors.fill: parent; radius: 32
                                    visible: !cell.upload && cell.entry.state !== "ready"
                                    color: cell.entry.state === "error" ? "#aa781414" : "#99000000"
                                    Text { anchors.centerIn: parent; text: cell.entry.state === "error" ? "Retry" : Math.round((cell.entry.progress || 0) * 100) + "%"; color: "white"; font.pixelSize: 11 }
                                }
                            }
                            Text {
                                anchors.top: circle.bottom; anchors.topMargin: 6; anchors.horizontalCenter: parent.horizontalCenter
                                width: 82; text: cell.upload ? "Upload" : cell.entry.name || ""; horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight; font.pixelSize: 11; color: controller.selected === cell.entry.id ? "#2f46e2" : "#6b7380"
                            }
                            MouseArea {
                                id: hit; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                enabled: !controller.busy
                                onClicked: { if (cell.upload) list.requestUpload(); else { if (thumbnail.status === RoundedImage.Error) thumbnail.retry(); controller.selectReference(cell.entry.id) } }
                                Accessible.role: Accessible.Button
                                Accessible.name: cell.upload ? "Upload reference image" : cell.entry.name || ""
                            }
                            XLabToolTip {
                                visible: hit.containsMouse && hit.enabled
                                text: cell.upload ? "Upload your own reference image" : cell.entry.error || thumbnail.errorString || cell.entry.name || ""
                            }
                        }
                    }
                }
            }
        }
    }
    signal requestUpload()
}
