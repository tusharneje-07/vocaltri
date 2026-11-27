import QtQuick
import Vocaltri 1.0

Item {
    id: root
    width: 720
    implicitHeight: 64
    height: implicitHeight

    readonly property bool active: session.state !== "idle" && session.state !== "done"

    function stateLabel() {
        switch (session.state) {
        case "connecting": return "Connecting…"
        case "listening": return "Listening…"
        case "finalizing": return "Finishing…"
        case "error": return "Error"
        default: return "Listening…"
        }
    }

    function elapsedLabel() {
        const total = Math.floor(session.elapsedMs / 1000)
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
               + (seconds < 10 ? "0" : "") + seconds
    }

    Rectangle {
        anchors.fill: parent
        radius: DesignTokens.radius
        color: DesignTokens.background
        border.width: 1
        border.color: session.state === "error" ? DesignTokens.error : DesignTokens.border

        Row {
            anchors.centerIn: parent
            spacing: 12

            Rectangle {
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: session.state === "error" ? DesignTokens.error : DesignTokens.listening

                SequentialAnimation on opacity {
                    running: root.active && session.state === "listening"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 700 }
                    NumberAnimation { to: 1; duration: 700 }
                }
            }

            Text {
                text: root.stateLabel()
                color: DesignTokens.primaryText
                font.pixelSize: 16
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.elapsedLabel()
                color: DesignTokens.secondaryText
                font.pixelSize: 14
                font.family: "monospace"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
