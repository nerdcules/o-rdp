import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Connecting to the selected PC: the user (last one used, remembered as
// rdp-connect does), the password (from the keyring when it was kept there),
// and how the window opens.
Rectangle {
    id: panel
    property var host: null
    readonly property real u: theme.fontScale
    readonly property bool canConnect: host !== null && !rdp.freerdpMissing && !host.connected && user.text.trim() !== ""

    color: theme.panel
    radius: theme.radius
    border.color: theme.panelBorder
    border.width: 1
    implicitHeight: column.implicitHeight + 32

    // Enter or Space in the list: connect if the form is ready, otherwise go
    // to the first field that's missing.
    function start() {
        if (!host) return
        if (user.text.trim() === "") { user.forceActiveFocus(); return }
        if (password.text === "") { password.forceActiveFocus(); return }
        connect()
    }
    function connect() {
        if (!canConnect) return
        rdp.connectTo(host.address, user.text.trim(), password.text, remember.checked)
        password.text = remember.checked ? password.text : ""
    }
    // A different host or user: its saved password, if any.
    function load() {
        if (!host) return
        user.text = host.user || ""
        refreshPassword()
    }
    function refreshPassword() {
        var saved = host ? rdp.savedPassword(host.address, user.text.trim()) : ""
        password.text = saved
        remember.checked = saved !== ""
        fromKeyring.visible = saved !== ""
    }
    onHostChanged: if (!host || host.address !== loadedFor) { loadedFor = host ? host.address : ""; load() }
    property string loadedFor: ""

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10 * panel.u

        Label {
            visible: panel.host === null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            text: qsTr("Pick a PC on the left.")
        }

        ColumnLayout {
            visible: panel.host !== null
            Layout.fillWidth: true
            spacing: 10 * panel.u

            Label {
                Layout.fillWidth: true
                text: panel.host ? (panel.host.name || panel.host.address) : ""
                color: theme.text
                font.pixelSize: Math.round(18 * panel.u)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                text: panel.host ? panel.host.address : ""
                color: theme.muted
                font.family: "monospace"
                font.pixelSize: Math.round(12 * panel.u)
            }
            TextField {
                id: user
                Layout.fillWidth: true
                placeholderText: qsTr("User, or email@outlook.com")
                onEditingFinished: panel.refreshPassword()
                onAccepted: password.forceActiveFocus()
                Keys.onEscapePressed: list.forceActiveFocus()
            }
            TextField {
                id: password
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                onAccepted: panel.connect()
                Keys.onEscapePressed: list.forceActiveFocus()
            }
            Label {
                id: fromKeyring
                visible: false
                color: theme.muted
                font.pixelSize: Math.round(11 * panel.u)
                text: qsTr("From the keyring.")
            }
            CheckBox {
                id: remember
                visible: rdp.keyringAvailable
                text: qsTr("Remember the password in the keyring")
            }
            CheckBox {
                text: qsTr("Full screen")
                checked: rdp.fullscreen
                onToggled: rdp.fullscreen = checked
            }
            CheckBox {
                text: qsTr("Use all monitors")
                checked: rdp.allMonitors
                onToggled: rdp.allMonitors = checked
            }
            PrimaryButton {
                Layout.fillWidth: true
                enabled: panel.canConnect
                text: panel.host && panel.host.connected ? qsTr("Connected") : qsTr("Connect (Enter)")
                onClicked: panel.connect()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: Math.round(11 * panel.u)
                text: qsTr("The password goes to FreeRDP through a pipe, never on a command line. The PC's certificate is trusted the first time and checked after that.")
            }
        }
    }
}
