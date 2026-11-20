import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, over the window, until Esc or a click outside.
Rectangle {
    id: overlay
    signal closed()
    color: Qt.rgba(0, 0, 0, 0.6)

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(560, overlay.width - 48)
        height: column.implicitHeight + 48
        color: theme.panel
        radius: theme.radius
        border.color: theme.panelBorder
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            Label {
                text: qsTr("Keyboard")
                color: theme.text
                font.pixelSize: Math.round(18 * theme.fontScale)
                font.weight: Font.DemiBold
            }

            Repeater {
                model: [
                    ["J / K, ↓ / ↑", qsTr("Select the next or previous PC")],
                    ["Enter, Space", qsTr("Connect (or go to the field still missing); show a PC already open")],
                    ["R", qsTr("Scan the network again")],
                    ["N", qsTr("Add a PC by address or name")],
                    ["Delete", qsTr("Forget the selected PC")],
                    ["Esc", qsTr("Leave a field, or close what is open")],
                    ["Ctrl + Alt + Home", qsTr("Bring out the menu over the PC, or put it away")],
                    ["Ctrl + Alt + Enter", qsTr("Full screen on or off while a PC is on show")],
                    ["Ctrl + / -", qsTr("Make the text larger or smaller")],
                    ["?", qsTr("Show this help")],
                    ["Ctrl + I", qsTr("About this app")],
                    ["Q", qsTr("Quit")]
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Label {
                        Layout.preferredWidth: Math.round(150 * theme.fontScale)
                        text: modelData[0]
                        color: theme.accent
                        font.family: "monospace"
                        font.pixelSize: Math.round(13 * theme.fontScale)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData[1]
                        color: theme.text
                        font.pixelSize: Math.round(13 * theme.fontScale)
                        wrapMode: Text.Wrap
                    }
                }
            }

            Label {
                Layout.topMargin: 6
                text: qsTr("Press Esc or click outside to close")
                color: theme.muted
                font.pixelSize: Math.round(12 * theme.fontScale)
            }
        }
    }
}
