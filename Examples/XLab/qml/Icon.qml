import QtQuick
Image {
    property string kind: "camera"
    property color tint: "#111318"
    readonly property color opaqueTint: Qt.rgba(tint.r,tint.g,tint.b,1)
    readonly property var paths: ({
        camera: '<rect x="3" y="6" width="13" height="12" rx="3"/><path d="m16 10 5-3v10l-5-3"/>',
        microphone: '<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10v2a7 7 0 0 0 14 0v-2M12 19v3M8 22h8"/>',
        key: '<circle cx="7.5" cy="15.5" r="5.5"/><path d="m11.4 11.6 9.6-9.6M15.5 7.5l3 3 3.5-3.5-3-3"/>',
        chevron: '<path d="m6 9 6 6 6-6"/>',
        statistics: '<path d="M4 20V11M12 20V4M20 20v-6"/>',
        volume: '<path d="m11 5-6 4H2v6h3l6 4V5ZM15 8a6 6 0 0 1 0 8M18 5a10 10 0 0 1 0 14"/>',
        muted: '<path d="m11 5-6 4H2v6h3l6 4V5ZM16 9l6 6M22 9l-6 6"/>',
        bolt: '<path d="m13 2-8 12h6l-1 8 9-13h-6l1-7Z"/>',
        plus: '<path d="M12 4v16M4 12h16"/>',
        arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
        stop: '<rect x="7" y="7" width="10" height="10" rx="1" fill="currentColor" stroke="none"/>',
        globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c6 6 6 12 0 18-6-6-6-12 0-18"/>',
        socialX: '<path d="m4 3 16 18h-4L1 3h3Zm16 0L4 21"/>',
        wechat: '<path d="M15 14c-2 4-7 4-10 2l-3 1 1-3C-1 7 6 2 12 5M14 9c10-3 12 7 6 9l1 3-4-2c-9 0-10-8-3-10Z"/>',
        close: '<path d="m6 6 12 12M18 6 6 18"/>',
        warning: '<circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 16v1"/>'
    })
    source: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" opacity="' + tint.a + '" color="' + opaqueTint + '" stroke="' + opaqueTint + '" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' + (paths[kind] || paths.camera) + '</svg>')
    sourceSize.width: width * 2
    sourceSize.height: height * 2
    fillMode: Image.PreserveAspectFit
}
