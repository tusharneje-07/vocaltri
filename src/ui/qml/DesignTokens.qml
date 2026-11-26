pragma Singleton

import QtQuick

QtObject {
    readonly property color background: Qt.rgba(0.075, 0.09, 0.13, 0.95)
    readonly property color border: Qt.rgba(0.55, 0.62, 0.75, 0.38)
    readonly property color primaryText: "#f4f7fb"
    readonly property color secondaryText: Qt.rgba(0.84, 0.87, 0.93, 0.68)
    readonly property color accent: "#79a8ff"
    readonly property color listening: "#6ee7b7"
    readonly property color warning: "#f7c873"
    readonly property color error: "#ff7e91"
    readonly property int radius: 18
    readonly property int contentPadding: 18
}
