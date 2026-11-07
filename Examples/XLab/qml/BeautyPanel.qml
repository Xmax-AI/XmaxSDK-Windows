//
//  BeautyPanel.qml
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

import QtQuick
import QtQuick.Controls

Rectangle {
    id: panel
    required property var controller
    width: 226; height: rows.height + 16
    radius: 8; color: "#99000000"; border.color: "#1fffffff"
    Column {
        id: rows
        x: 8; y: 8; width: parent.width - 16; spacing: 2
        Repeater {
            model: [
                { key: "smoothness", title: "Smoothness" },
                { key: "lightening", title: "Lightening" },
                { key: "sharpness", title: "Sharpness" },
                { key: "redness", title: "Redness" }
            ]
            delegate: Row {
                required property var modelData
                width: rows.width; height: 22; spacing: 8
                Text {
                    width: 70; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.title; color: "#e6ffffff"; font.pixelSize: 11
                }
                Slider {
                    id: strength
                    objectName: "beautySlider_" + modelData.key
                    width: 94; height: 22; padding: 0
                    from: 0; to: 1; stepSize: 0.01
                    value: panel.controller.beautyOptions[modelData.key]
                    Accessible.name: modelData.title
                    onMoved: panel.controller.setBeautyStrength(modelData.key, value)
                    background: Rectangle {
                        x: strength.leftPadding; y: (strength.height - height) / 2
                        width: strength.availableWidth; height: 3; radius: 1.5; color: "#40ffffff"
                        Rectangle { width: strength.visualPosition * parent.width; height: 3; radius: 1.5; color: "#9a9ff6" }
                    }
                    handle: Rectangle {
                        x: strength.leftPadding + strength.visualPosition * (strength.availableWidth - width)
                        y: (strength.height - height) / 2
                        width: 10; height: 10; radius: 5; color: "white"
                        border.width: strength.visualFocus ? 2 : 0; border.color: "#9a9ff6"
                    }
                }
                Text {
                    width: 30; anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(panel.controller.beautyOptions[modelData.key] * 100) + "%"
                    color: "#e6ffffff"; font.pixelSize: 11
                }
            }
        }
    }
}
