import QtQuick
Image {
    property string kind: "camera"
    property color tint: "#111318"
    readonly property color opaqueTint: Qt.rgba(tint.r,tint.g,tint.b,1)
    readonly property var paths: ({
        camera: '<rect x="2" y="6" width="14" height="12" rx="3"/><path d="m16 10 6-3.5v11l-6-3.5"/>',
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
        record: '<circle cx="12" cy="12" r="8" fill="currentColor" stroke="none"/>',
        globe: '<circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2c7.067 6.667 7.067 13.333 0 20-7.067-6.667-7.067-13.333 0-20"/>',
        socialX: '<path fill="currentColor" stroke="none" fill-rule="evenodd" d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231 5.451-6.231ZM17.083 19.77h1.833L6.95 4.126H5.117L17.083 19.77Z"/>',
        wechat: '<path fill="currentColor" stroke="none" fill-rule="evenodd" transform="matrix(1.15 0 0 1.15 -1.8 -1.8)" d="M9.3 4C5.6 4 2.6 6.6 2.6 9.9c0 1.9 1 3.5 2.6 4.6l-.7 2.2 2.4-1.2c.7.2 1.5.3 2.4.3h.5c-.1-.4-.2-.9-.2-1.3 0-3.1 2.9-5.6 6.4-5.6h.3C15.7 6.2 12.8 4 9.3 4ZM7.3 7.2a.9.9 0 1 1 0 1.8.9.9 0 0 1 0-1.8Zm4.4 0a.9.9 0 1 1 0 1.8.9.9 0 0 1 0-1.8Zm4.1 2.7c-3.1 0-5.6 2-5.6 4.6s2.5 4.6 5.6 4.6c.6 0 1.3-.1 1.9-.3l2 1-.5-1.8c1.3-.8 2.2-2.1 2.2-3.5 0-2.5-2.5-4.6-5.6-4.6Zm-1.6 2.5a.8.8 0 1 1 0 1.6.8.8 0 0 1 0-1.6Zm3.2 0a.8.8 0 1 1 0 1.6.8.8 0 0 1 0-1.6Z"/>',
        close: '<path d="m6 6 12 12M18 6 6 18"/>',
        warning: '<circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 16v1"/>'
    })
    source: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" opacity="' + tint.a + '" color="' + opaqueTint + '" stroke="' + opaqueTint + '" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' + (paths[kind] || paths.camera) + '</svg>')
    sourceSize.width: width * 2
    sourceSize.height: height * 2
    fillMode: Image.PreserveAspectFit
}
