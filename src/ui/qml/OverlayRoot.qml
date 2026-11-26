import QtQuick
import QtQuick.Controls
import Vocaltri 1.0

Item {
    id: root
    width: 720
    implicitHeight: Math.max(76, Math.min(192, card.implicitHeight))
    height: implicitHeight

    readonly property bool active: session.state !== "idle" && session.state !== "done"

    function stateLabel() {
        switch (session.state) {
        case "connecting": return "Connecting"
        case "listening": return "Listening"
        case "finalizing": return "Finalizing"
        case "error": return "Error"
        default: return "Ready"
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: DesignTokens.radius
        color: DesignTokens.background
        border.width: 1
        border.color: session.state === "error" ? DesignTokens.error : DesignTokens.border
        implicitHeight: Math.max(76, transcript.implicitHeight + 36)
        opacity: root.active ? 1 : 0
        scale: root.active ? 1 : 0.94

        Behavior on opacity {
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.08 }
        }

        Row {
            id: content
            anchors.fill: parent
            anchors.margins: DesignTokens.contentPadding
            spacing: 14

            Column {
                width: 82
                spacing: 7

                Row {
                    spacing: 7
                    Rectangle {
                        width: 9
                        height: 9
                        radius: 5
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
                        color: DesignTokens.secondaryText
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                Row {
                    height: 28
                    spacing: 3
                    Repeater {
                        model: 7
                        Rectangle {
                            width: 5
                            height: 8 + (session.level * 18 * (0.55 + (index % 3) * 0.2))
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 3
                            color: DesignTokens.accent
                            Behavior on height { NumberAnimation { duration: 90 } }
                        }
                    }
                }
            }

            Column {
                id: transcript
                width: Math.max(1, parent.width - 170)
                spacing: 4
                implicitHeight: Math.min(120, finalLine.implicitHeight + interimLine.implicitHeight + 4)
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: finalLine
                    width: parent.width
                    text: session.finalText
                    color: DesignTokens.primaryText
                    font.pixelSize: 17
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
                Text {
                    id: interimLine
                    width: parent.width
                    text: session.interimText
                    color: DesignTokens.secondaryText
                    font.pixelSize: 17
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    visible: text.length > 0
                }
                Text {
                    visible: finalLine.text.length === 0 && interimLine.text.length === 0
                    text: session.state === "error" ? "Something went wrong" : "Speak when you are ready…"
                    color: DesignTokens.secondaryText
                    font.pixelSize: 17
                }
            }

            Column {
                width: 46
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: {
                        const total = Math.floor(session.elapsedMs / 1000)
                        const minutes = Math.floor(total / 60)
                        const seconds = total % 60
                        return (minutes < 10 ? "0" : "") + minutes + ":"
                               + (seconds < 10 ? "0" : "") + seconds
                    }
                    color: DesignTokens.secondaryText
                    font.pixelSize: 12
                    font.family: "monospace"
                }
                ToolButton {
                    width: 32
                    height: 28
                    text: "×"
                    font.pixelSize: 20
                    onClicked: session.requestCancel()
                    visible: session.state !== "connecting" && session.state !== "error"
                }
            }
        }
    }
}
