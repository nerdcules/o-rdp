import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One PC: a screen drawn in the theme's colours, its name and address, the
// last user, and whether the scan found it or a session is open.
Rectangle {
    id: row
    property var host
    property bool selected: false
    signal clicked()
    signal doubleClicked()
    readonly property real u: theme.fontScale

    height: content.implicitHeight + 20 * u
    radius: theme.radius
    color: selected ? Qt.alpha(theme.accent, 0.14) : theme.panel
    border.color: selected ? theme.accent : theme.panelBorder
    border.width: 1

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 14
        spacing: 14

        // A small monitor: screen and stand.
        Item {
            Layout.preferredWidth: 34 * row.u
            Layout.preferredHeight: 28 * row.u
            Rectangle {
                width: parent.width
                height: parent.height * 0.72
                radius: Math.min(3, theme.radius)
                color: "transparent"
                border.color: row.host.connected ? theme.accent : row.host.online ? theme.text : theme.muted
                border.width: 2
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: parent.width * 0.4
                height: 2
                color: row.host.online ? theme.text : theme.muted
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: row.host.name || row.host.address
                color: theme.text
                font.pixelSize: Math.round(15 * row.u)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: row.host.address + (row.host.user ? "  ·  " + row.host.user : "")
                color: theme.muted
                font.family: "monospace"
                font.pixelSize: Math.round(12 * row.u)
                elide: Text.ElideRight
            }
        }

        RowLayout {
            spacing: 6
            Rectangle {
                width: 8 * row.u
                height: width
                radius: width / 2
                color: row.host.connected ? theme.accent : row.host.online ? theme.good : theme.muted
            }
            Label {
                text: row.host.connected ? qsTr("Connected") : row.host.online ? qsTr("On the network") : qsTr("Not seen in this scan")
                color: row.host.connected ? theme.accent : row.host.online ? theme.text : theme.muted
                font.pixelSize: Math.round(12 * row.u)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: row.clicked()
        onDoubleClicked: row.doubleClicked()
    }
}
