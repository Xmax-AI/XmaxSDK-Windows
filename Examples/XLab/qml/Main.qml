import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import QtQuick.Effects

ApplicationWindow {
    id: window
    objectName: "xlabWindow"
    width: 1360; height: 850; minimumWidth: 1180; minimumHeight: 760
    visible: true; title: controller.preview ? "X-Lab — UI Preview (no live session)" : "X-Lab — XmaxSDK"
    color: "white"
    readonly property var controller: xlab
    readonly property var modes: ["charx", "clothx", "vibex", "free"]
    readonly property var titles: ["Character Swap", "Virtual Try-On", "Style Transfer", "Free Prompt"]
    readonly property var freeReferences: { controller.references; return controller.referencesFor("free") }
    property string importMode: "charx"
    property bool allowClose: false
    function openImporter(mode) { importMode = mode; importer.open() }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Item {
            id: page
            width: window.width
            height: Math.max(window.height, controller.active ? workspace.height + 140 : 760)
            Item {
                id: header
                x: 28; width: parent.width - 56; height: 60
                y: controller.active ? Math.max(24, (page.height - workspace.height - 104) / 2) : 24
                Image {
                    id: brand
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    height: Math.min(52,Math.max(34,window.width * 0.008 + 24)); width: height * 552 / 172
                    source: "../assets/xmax-wordmark.png"; fillMode: Image.PreserveAspectFit
                    Accessible.name: "Xmax"
                }
                Pill {
                    visible: controller.preview
                    anchors.left: brand.right; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
                    passive: true; accent: true; text: "UI Preview · camera off"
                }
                Row {
                    visible: !controller.active
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                    CameraPicker { visible: !controller.preview; controller: window.controller }
                    Pill {
                        id: modelButton; objectName: "modelButton"
                        text: controller.model.replace("x2", "X2")
                        glyph: "chevron"; trailingGlyph: true; enabled: !controller.busy
                        onClicked: modelPopup.open()
                        Popup {
                            id: modelPopup
                            x: parent.width - width; y: parent.height + 8; width: 240; padding: 8
                            background: Rectangle { color: "white"; radius: 12; border.color: "#e5e7eb" }
                            contentItem: Column {
                                Repeater {
                                    model: ["x2.1"]
                                    delegate: ItemDelegate {
                                        required property string modelData
                                        width: 224; height: 36; text: modelData.replace("x2", "X2")
                                        highlighted: controller.model === modelData
                                        onClicked: { controller.model = modelData; modelPopup.close() }
                                    }
                                }
                            }
                        }
                    }
                    Pill {
                        id: keyButton; objectName: "keyButton"
                        glyph: "key"; text: controller.apiKey.length ? controller.apiKey.slice(0,20) + (controller.apiKey.length > 20 ? "…" : "") : ""
                        enabled: !controller.busy; ToolTip.text: "API Key"
                        onClicked: { keyPopup.open(); keyInput.forceActiveFocus() }
                        Popup {
                            id: keyPopup
                            x: parent.width - width; y: parent.height + 8; width: 300; padding: 10
                            background: Rectangle { color: "white"; radius: 12; border.color: "#e5e7eb" }
                            contentItem: TextField {
                                id: keyInput; objectName: "keyInput"
                                text: controller.apiKey; placeholderText: "API Key"; selectByMouse: true
                                onTextEdited: controller.apiKey = text
                                onAccepted: keyPopup.close()
                                background: Rectangle { radius: 10; color: "#f7f8fa"; border.color: keyInput.activeFocus ? "#2f46e2" : "#e5e7eb" }
                                leftPadding: 12; rightPadding: 12
                            }
                        }
                    }
                }
                Row {
                    visible: controller.active
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                    Pill { text: controller.model.replace("x2", "X2"); passive: true; enabled: false; opacity: 1 }
                    Pill {
                        objectName: "statisticsButton"; glyph: "statistics"; accent: controller.statisticsVisible
                        ToolTip.text: controller.statisticsVisible ? "Hide statistics (collection continues)" : "Show statistics"
                        onClicked: controller.statisticsVisible = !controller.statisticsVisible
                    }
                    Pill {
                        id: volumeButton; glyph: controller.volume > 0 ? "volume" : "muted"; ToolTip.text: "Remote volume"
                        secondaryForeground: true
                        onClicked: volumePopup.open()
                        Popup {
                            id: volumePopup; x: parent.width - width; y: parent.height + 8; width: 240; padding: 14
                            background: Rectangle { color: "white"; radius: 12; border.color: "#e5e7eb" }
                            contentItem: Column {
                                spacing: 10
                                Text { text: "Remote volume                       " + Math.round(controller.volume * 100) + "%"; font.pixelSize: 12; color: "#111318" }
                                Slider { width: 212; from: 0; to: 1; value: controller.volume; onMoved: controller.volume = value }
                            }
                        }
                    }
                    Pill { text: controller.preview ? "Preview" : controller.elapsed; passive: true; enabled: false; opacity: 1; implicitWidth: 72 }
                    Pill {
                        objectName: "recordButton"
                        text: controller.finishingRecording ? "Finishing…" : controller.recordingToSave ? "Saving…" : controller.recording ? "Stop Recording" : "Record"
                        glyph: "record"; glyphSize: 12; contentSpacing: 7; recordingStyle: true
                        accent: controller.recording; loading: controller.finishingRecording
                        enabled: controller.recording || controller.canRecord
                        ToolTip.text: controller.recording ? "Stop recording and save the remote result" : "Record remote video and audio"
                        onClicked: controller.recording ? controller.stopRecording() : controller.startRecording()
                    }
                    Pill { objectName: "stopButton"; glyph: "stop"; text: controller.preview ? "Exit Preview" : "Stop Session"; destructive: true; onClicked: controller.stop() }
                }
            }

            Column {
                id: landing
                objectName: "landingPage"
                visible: !controller.active
                transform: Translate { id: landingShift }
                PageEntry { page: landing; shift: landingShift; controller: window.controller }
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.max(header.y + 108, (page.height - height) / 2 - 5)
                spacing: 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Reimagining Live Video with Xmax"
                    font.pixelSize: Math.min(45, Math.max(27, window.width * 0.01 + 18)); font.weight: Font.Medium; font.letterSpacing: -0.95
                    color: "#111318"
                }
                Item { width: 1; height: Math.min(40, Math.max(26, window.width * 0.011 + 17)) }
                Rectangle {
                    id: stage
                    objectName: "landingStage"
                    width: Math.min(window.width - 56, Math.min(940, Math.max(580, window.width * 0.45)))
                    height: width * 1024 / 1920; radius: 24; color: "transparent"
                    anchors.horizontalCenter: parent.horizontalCenter
                    Checkerboard { anchors.fill: parent }
                    Column {
                        anchors.centerIn: parent; spacing: 20
                        Pill {
                            id: startButton
                            objectName: "startButton"; anchors.horizontalCenter: parent.horizontalCenter
                            primary: true; text: controller.closing ? "Stopping…" : controller.busy ? "Starting…" : controller.preview ? "Preview UI" : "Start Camera"; glyph: "camera"
                            labelSize: Math.min(18, Math.max(15, window.width * 0.003 + 12))
                            glyphSize: 20; contentSpacing: 10
                            contentHorizontalPadding: Math.min(40, Math.max(25, window.width * 0.0075 + 18))
                            implicitHeight: Math.max(20, Math.ceil(startFont.height)) + Math.min(16, Math.max(12, window.width * 0.0028 + 9)) * 2
                            FontMetrics { id: startFont; font.family: startButton.font.family; font.pixelSize: startButton.labelSize; font.weight: Font.DemiBold }
                            enabled: !controller.busy; onClicked: controller.start()
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 158; height: 32; radius: 16; color: "#47000000"; border.color: controller.microphone ? "#4dffffff" : "#29ffffff"
                            Row {
                                anchors.centerIn: parent; spacing: 8
                                Icon { kind: "microphone"; tint: controller.microphone ? "white" : "#8cffffff"; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "Microphone"; color: controller.microphone ? "white" : "#8cffffff"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                Rectangle {
                                    width: 30; height: 18; radius: 9; color: controller.microphone ? "#2f46e2" : "#2effffff"
                                    Rectangle { x: controller.microphone ? 14 : 2; y: 2; width: 14; height: 14; radius: 7; color: "white"; Behavior on x { NumberAnimation { duration: 150 } } }
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: !controller.busy; onClicked: controller.microphone = !controller.microphone; Accessible.name: "Microphone"; Accessible.role: Accessible.CheckBox }
                        }
                    }
                    Text {
                        width: parent.width - 40; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 28
                        text: 'By clicking Start, you agree to our <a style="color:#c3c6cc" href="https://platform.xmaxsi.com/docs/resources/terms">Terms of Use</a> and <a style="color:#c3c6cc" href="https://platform.xmaxsi.com/docs/resources/privacy">Privacy Policy</a>.'
                        textFormat: Text.RichText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                        color: "#9ca3af"; linkColor: "#c3c6cc"; font.pixelSize: Math.min(15,Math.max(12,window.width * 0.0028 + 9.5))
                        onLinkActivated: link => Qt.openUrlExternally(link)
                    }
                }
                Item { width: 1; height: 16 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Copyright © 2026 XMAX.AI PTE.LTD. All rights reserved."
                    font.pixelSize: 13; color: "#6b7380"
                }
            }

            Column {
                id: workspace
                objectName: "generationPage"
                visible: controller.active
                transform: Translate { id: workspaceShift }
                PageEntry { page: workspace; shift: workspaceShift; controller: window.controller }
                x: 28; width: parent.width - 56
                y: header.y + header.height + 8
                spacing: 0
                Row {
                    width: parent.width; spacing: 24
                    VideoPanel { id: localVideo; objectName: "localPanel"; width: (parent.width - 24) / 2; height: width * 1024 / 1920; controller: window.controller; title: controller.cameraName ? "Local · " + controller.cameraName : "Local"; Component.onCompleted: controller.localSink = sink }
                    VideoPanel { id: remoteVideo; width: (parent.width - 24) / 2; height: width * 1024 / 1920; controller: window.controller; title: "Result"; result: true; Component.onCompleted: controller.remoteSink = sink }
                }
                Item { width: 1; height: 34 }
                Rectangle {
                    width: tabs.width + 8; height: 42; radius: 21; color: "#1a787880"; anchors.horizontalCenter: parent.horizontalCenter
                    Row {
                        id: tabs; x: 4; y: 4; spacing: 2
                        Repeater {
                            model: window.modes
                            delegate: Button {
                                required property string modelData
                                required property int index
                                objectName: "mode_" + modelData
                                text: window.titles[index]; width: label.implicitWidth + 44; height: 34
                                hoverEnabled: true
                                onClicked: controller.mode = modelData
                                background: Rectangle { radius: 17; color: controller.mode === modelData ? "white" : parent.hovered ? "#a6ffffff" : "transparent"; border.color: controller.mode === modelData ? "#0d000000" : "transparent" }
                                contentItem: Text { id: label; text: parent.text; color: controller.mode === modelData ? "#111318" : "#6b7380"; font.pixelSize: 14; font.weight: Font.Medium; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                    }
                }
                Item { width: 1; height: 28 }
                Item {
                    width: parent.width; height: 190
                    Repeater {
                        model: ["charx", "clothx", "vibex"]
                        delegate: ReferenceList {
                            required property string modelData
                            anchors.fill: parent; mode: modelData; controller: window.controller
                            visible: controller.mode === mode
                            onRequestUpload: window.openImporter(mode)
                        }
                    }
                    Rectangle {
                        visible: controller.mode === "free"
                        width: Math.min(880,parent.width); height: 136; anchors.horizontalCenter: parent.horizontalCenter
                        radius: 18; color: "#f7f8fa"; border.color: "#e5e7eb"
                        TextArea {
                            id: promptEditor; objectName: "promptEditor"
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: promptTools.left; anchors.margins: 12
                            placeholderText: "Describe what you want to see…"; wrapMode: TextEdit.Wrap; selectByMouse: true
                            text: controller.prompt; onTextChanged: controller.prompt = text
                            font.pixelSize: 14; color: "#111318"; background: null
                        }
                        Row {
                            id: promptTools; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12; spacing: 10
                            Pill { visible: window.freeReferences.length === 0; glyph: "plus"; width: 44; height: 44; ToolTip.text: "Upload reference image"; onClicked: window.openImporter("free") }
                            Item {
                                visible: window.freeReferences.length > 0; width: 44; height: 44
                                RoundedImage { id: freeThumbnail; anchors.fill: parent; source: window.freeReferences.length ? window.freeReferences[0].thumbnail : "" }
                                Icon { visible: freeThumbnail.status === RoundedImage.Error; anchors.centerIn: parent; width: 18; height: 18; kind: "warning"; tint: "#6b7380" }
                                MouseArea { anchors.fill: parent; onClicked: { if (freeThumbnail.status === RoundedImage.Error) freeThumbnail.retry(); if (window.freeReferences.length) controller.selectReference(window.freeReferences[0].id) } }
                                Rectangle {
                                    anchors.fill: parent; radius: 22; visible: window.freeReferences.length > 0 && window.freeReferences[0].state !== "ready"; color: "#99000000"
                                    Text { anchors.centerIn: parent; text: window.freeReferences.length && window.freeReferences[0].state === "error" ? "Retry" : window.freeReferences.length ? Math.round((window.freeReferences[0].progress || 0) * 100) + "%" : ""; color: "white"; font.pixelSize: 10 }
                                }
                                ToolButton {
                                    id: removeReference
                                    objectName: "removeReferenceButton"
                                    x: 22; y: -12; width: 32; height: 32; padding: 0
                                    hoverEnabled: true
                                    background: Item {
                                        Rectangle {
                                            anchors.fill: parent; anchors.margins: 6
                                            radius: width / 2
                                            color: removeReference.down ? "#333333" : removeReference.hovered ? "#525252" : "#3d3d3d"
                                            border.width: removeReference.visualFocus ? 1 : 0
                                            border.color: "#2f46e2"
                                        }
                                    }
                                    contentItem: Item {
                                        Icon { anchors.centerIn: parent; width: 11; height: 11; kind: "close"; tint: "white" }
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    onClicked: controller.removeFreeReference()
                                    Accessible.name: "Remove reference"
                                }
                            }
                            Pill { objectName: "submitButton"; glyph: "arrow"; width: 44; height: 44; primary: true; showsShadow: false; implicitWidth: 44; ToolTip.text: "Submit prompt"; enabled: window.freeReferences.length === 0 || window.freeReferences[0].state === "ready"; onClicked: controller.submitPrompt() }
                        }
                    }
                }
            }
            Row {
                id: socialFooter
                visible: !controller.active
                transform: Translate { id: socialFooterShift }
                PageEntry { page: socialFooter; shift: socialFooterShift; controller: window.controller }
                x: 28; y: page.height - 72; spacing: 18
                Repeater {
                    model: ["wechat", "socialX", "globe"]
                    delegate: ToolButton {
                        id: socialButton
                        required property string modelData
                        width: 24; height: 36; padding: 0
                        hoverEnabled: true
                        contentItem: Item {
                            Icon { anchors.centerIn: parent; width: modelData === "socialX" ? 18 : 20; height: width; kind: modelData; tint: parent.parent.hovered ? "#111318" : "#6b7380" }
                        }
                        background: null
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        XLabToolTip { visible: socialButton.hovered; text: socialButton.Accessible.name }
                        onClicked: { if (modelData === "wechat") wechat.open(); else Qt.openUrlExternally(modelData === "socialX" ? "https://x.com/XmaxAIOfficial" : "https://www.xmaxai.com") }
                        Accessible.name: modelData === "wechat" ? "WeChat" : modelData === "globe" ? "Website" : "X"
                    }
                }
            }
            Row {
                id: linksFooter
                visible: !controller.active
                transform: Translate { id: linksFooterShift }
                PageEntry { page: linksFooter; shift: linksFooterShift; controller: window.controller }
                anchors.right: parent.right; anchors.rightMargin: 28; y: page.height - 72; spacing: 24
                Repeater {
                    model: [
                        { label: "Contact Us", url: "mailto:contact@xmax.ai" },
                        { label: "GitHub", url: "https://github.com/Xmax-AI/XmaxSDK-Windows" },
                        { label: "API Platform", url: "https://platform.xmaxsi.com" }
                    ]
                    delegate: Text {
                        required property var modelData
                        text: modelData.label; color: linkHover.containsMouse ? "#111318" : "#6b7380"; font.pixelSize: 13
                        height: 36; verticalAlignment: Text.AlignVCenter
                        Accessible.role: Accessible.Link; Accessible.name: text
                        MouseArea { id: linkHover; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; hoverEnabled: true; onClicked: Qt.openUrlExternally(modelData.url) }
                    }
                }
            }
        }
    }
    Connections {
        target: controller
        function onApiKeyRequired() { keyPopup.open(); keyInput.forceActiveFocus() }
        function onRecordingReady() {
            recordingSave.selectedFile = controller.defaultRecordingDestination()
            recordingSave.open()
        }
        function onRecordingSaveFailed(message) { recordingRetry.message = message; recordingRetry.open() }
        function onReadyToClose() { window.allowClose = true; Qt.callLater(function() { window.close() }) }
    }
    Rectangle {
        visible: controller.error.length > 0
        z: 100; width: Math.min(600,window.width - 56); height: toastText.implicitHeight + 28
        anchors.horizontalCenter: parent.horizontalCenter; y: 90
        color: "#f5ffffff"; radius: 14; border.color: "#14111318"
        Icon { x: 16; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; kind: "warning"; tint: "#e5484d" }
        Text { id: toastText; x: 46; y: 14; width: parent.width - 90; text: controller.error; wrapMode: Text.Wrap; font.pixelSize: 14; color: "#111318" }
        ToolButton {
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28
            contentItem: Icon { kind: "close"; tint: "#6b7380" }
            background: null; onClicked: controller.dismissError(); Accessible.name: "Dismiss error"
        }
        Timer { interval: 6000; running: controller.error.length > 0; onTriggered: controller.dismissError() }
    }
    Text { visible: controller.preview; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: "UI preview · no camera, network session or generated output"; color: "#9ca3af"; font.pixelSize: 11 }
    FileDialog { id: importer; title: "Choose a reference image"; nameFilters: ["Images (*.jpg *.jpeg *.png *.webp *.bmp)"]; onAccepted: controller.uploadReference(selectedFile, window.importMode) }
    FileDialog {
        id: recordingSave
        title: "Save Recording"; fileMode: FileDialog.SaveFile
        nameFilters: ["MP4 video (*.mp4)"]; defaultSuffix: "mp4"
        onAccepted: controller.saveRecording(selectedFile)
        onRejected: controller.discardRecording()
    }
    Dialog {
        id: recordingRetry
        property string message: ""
        title: "Unable to Save Recording"
        modal: true; anchors.centerIn: parent; closePolicy: Popup.NoAutoClose
        contentItem: Column {
            spacing: 16
            Label { width: 360; text: recordingRetry.message; wrapMode: Text.Wrap }
            Row {
                spacing: 12
                Button { text: "Try Again"; onClicked: { recordingRetry.close(); recordingSave.open() } }
                Button { text: "Discard Recording"; onClicked: { recordingRetry.close(); controller.discardRecording() } }
            }
        }
    }
    Popup {
        id: wechat; parent: Overlay.overlay; anchors.centerIn: parent; padding: 14
        background: Rectangle { color: "white"; radius: 12; border.color: "#e5e7eb" }
        contentItem: Column {
            spacing: 8
            Image { width: 180; height: 180; source: "https://assets.22duck.fun/image/wechatAccount.jpg"; fillMode: Image.PreserveAspectFit; asynchronous: true }
            Text { text: "Follow us on WeChat"; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }
    onClosing: event => {
        if (!window.allowClose) {
            event.accepted = false
            controller.requestClose()
        }
    }
}
