import QtQuick
import QtQuick.Controls

// The main action of a panel or dialog: filled with the theme's accent, so
// it reads as the thing to press, the same in every renderer.
Rectangle {
    id: button
    property string text
    property bool enabled: true
    signal clicked()
    readonly property real u: theme.fontScale

    implicitWidth: label.implicitWidth + 32 * u
    implicitHeight: Math.round(38 * u)
    radius: Math.min(6, theme.radius)
    color: !enabled ? Qt.alpha(theme.muted, 0.25) : area.containsMouse ? Qt.lighter(theme.accent, 1.08) : theme.accent
    opacity: enabled ? 1 : 0.7

    Label {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.enabled ? theme.accentForeground : theme.muted
        font.pixelSize: Math.round(14 * button.u)
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
