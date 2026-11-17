import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1000
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: "o-rdp"
    color: theme.window

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool aboutVisible: false
    property bool addVisible: false
    property bool forgetVisible: false
    readonly property real u: theme.fontScale
    readonly property bool wide: width >= 860 * u
    readonly property var selected: list.currentIndex >= 0 && list.currentIndex < rdp.hosts.length ? rdp.hosts[list.currentIndex] : null
    // Typing in a field keeps its letters; the window's single-key
    // shortcuts only act outside text fields.
    readonly property bool typing: activeFocusItem instanceof TextInput

    function stepBack() {
        if (aboutVisible) aboutVisible = false
        else if (helpVisible) helpVisible = false
        else if (addVisible) addVisible = false
        else if (forgetVisible) forgetVisible = false
        else if (rdp.problem.text) rdp.clearProblem()
        else list.forceActiveFocus()
    }
    function move(delta) {
        if (rdp.hosts.length === 0) return
        list.currentIndex = Math.max(0, Math.min(rdp.hosts.length - 1, list.currentIndex + delta))
    }

    Shortcut { sequence: "Ctrl+I"; context: Qt.ApplicationShortcut; onActivated: win.aboutVisible = !win.aboutVisible }
    Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.stepBack() }
    Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "R"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: rdp.scan() }
    Shortcut { sequence: "N"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.addVisible = true }
    Shortcut { sequences: ["J", "Down"]; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.move(1) }
    Shortcut { sequences: ["K", "Up"]; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.move(-1) }
    Shortcut { sequences: ["Space", "Return", "Enter"]; enabled: !win.typing && win.selected !== null; context: Qt.ApplicationShortcut; onActivated: panel.start() }
    Shortcut { sequence: "Delete"; enabled: !win.typing && win.selected !== null; context: Qt.ApplicationShortcut; onActivated: win.forgetVisible = true }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; context: Qt.ApplicationShortcut; onActivated: theme.increaseFont() }
    Shortcut { sequence: "Ctrl+-"; context: Qt.ApplicationShortcut; onActivated: theme.decreaseFont() }
    Shortcut { sequence: "Ctrl+0"; context: Qt.ApplicationShortcut; onActivated: theme.resetFont() }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Label {
                    text: "o-rdp"
                    color: theme.text
                    font.pixelSize: Math.round(20 * win.u)
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    color: theme.muted
                    font.pixelSize: Math.round(13 * win.u)
                    elide: Text.ElideRight
                    text: {
                        translator.language
                        if (rdp.scanning)
                            return qsTr("Scanning %1 for Remote Desktop… %2 of %3").arg(rdp.subnet).arg(rdp.probed).arg(rdp.total)
                        var online = rdp.hosts.filter(function (h) { return h.online }).length
                        if (!rdp.scannedOnce)
                            return ""
                        return rdp.subnet === "/0" || rdp.subnet === ""
                               ? qsTr("No network connection to scan.")
                               : qsTr("%n PCs with Remote Desktop on %1", "", online).arg(rdp.subnet)
                    }
                }
                ProgressBar {
                    visible: rdp.scanning
                    Layout.preferredWidth: 120 * win.u
                    from: 0
                    to: Math.max(1, rdp.total)
                    value: rdp.probed
                }
                Button {
                    text: qsTr("Scan again (R)")
                    enabled: !rdp.scanning
                    flat: true
                    onClicked: rdp.scan()
                }
                Button {
                    text: qsTr("Add address (N)")
                    flat: true
                    onClicked: win.addVisible = true
                }
            }

            // FreeRDP missing, or what went wrong with the last connection.
            Rectangle {
                Layout.fillWidth: true
                visible: rdp.freerdpMissing || !!rdp.problem.text
                implicitHeight: problemColumn.implicitHeight + 20
                radius: theme.radius
                color: Qt.alpha(theme.warn, 0.12)
                border.color: Qt.alpha(theme.warn, 0.5)
                border.width: 1
                ColumnLayout {
                    id: problemColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 14
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: theme.warn
                        font.pixelSize: Math.round(13 * win.u)
                        text: rdp.freerdpMissing
                              ? qsTr("FreeRDP isn't installed, so o-rdp can't connect. Install it with: omarchy-pkg-add freerdp")
                              : qsTr("%1: %2").arg(rdp.problem.address || "").arg(rdp.problem.text || "")
                    }
                    Label {
                        visible: !rdp.freerdpMissing && !!rdp.problem.details
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        color: theme.muted
                        font.family: "monospace"
                        font.pixelSize: Math.round(10 * win.u)
                        maximumLineCount: 6
                        text: rdp.problem.details || ""
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: win.wide ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 160 * win.u
                    clip: true
                    focus: true
                    spacing: 6
                    model: rdp.hosts
                    currentIndex: rdp.hosts.length > 0 ? 0 : -1
                    ScrollBar.vertical: ScrollBar { }
                    delegate: HostRow {
                        required property var modelData
                        required property int index
                        width: list.width
                        host: modelData
                        selected: ListView.isCurrentItem
                        onClicked: list.currentIndex = index
                        onDoubleClicked: { list.currentIndex = index; panel.start() }
                    }

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        visible: rdp.hosts.length === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: theme.muted
                        font.pixelSize: Math.round(14 * win.u)
                        text: rdp.scanning ? qsTr("Looking for PCs with Remote Desktop…")
                                           : qsTr("No PC with Remote Desktop found. Check that it's turned on in the PC's settings and that the PC is awake, or press N to type an address.")
                    }
                }

                ConnectPanel {
                    id: panel
                    host: win.selected
                    Layout.preferredWidth: win.wide ? 340 * win.u : -1
                    Layout.fillWidth: !win.wide
                    Layout.fillHeight: win.wide
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("J/K select · Enter connect · R scan · N add address · Delete forget · ? help · Q quit")
                color: theme.muted
                font.pixelSize: Math.round(12 * win.u)
                elide: Text.ElideRight
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }

        // Typing an address the scan can't find (another subnet, a name).
        Rectangle {
            anchors.fill: parent
            visible: win.addVisible
            color: Qt.rgba(0, 0, 0, 0.6)
            onVisibleChanged: if (visible) { address.text = ""; address.forceActiveFocus() }
            MouseArea { anchors.fill: parent; onClicked: win.addVisible = false }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(440 * win.u, parent.width - 48)
                height: addColumn.implicitHeight + 40
                radius: theme.radius
                color: theme.panel
                border.color: theme.panelBorder
                MouseArea { anchors.fill: parent }
                ColumnLayout {
                    id: addColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    Label {
                        text: qsTr("Add a PC")
                        color: theme.text
                        font.pixelSize: Math.round(16 * win.u)
                        font.weight: Font.DemiBold
                    }
                    TextField {
                        id: address
                        Layout.fillWidth: true
                        placeholderText: qsTr("Address or name, e.g. 192.168.8.50")
                        onAccepted: addButton.clicked()
                    }
                    Label {
                        id: addError
                        visible: false
                        color: theme.warn
                        text: qsTr("That isn't an address or a computer name.")
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button { flat: true; text: qsTr("Cancel"); onClicked: win.addVisible = false }
                        PrimaryButton {
                            id: addButton
                            text: qsTr("Add")
                            onClicked: {
                                if (rdp.addAddress(address.text)) {
                                    win.addVisible = false
                                    addError.visible = false
                                    for (var i = 0; i < rdp.hosts.length; ++i)
                                        if (rdp.hosts[i].address === address.text.trim()) list.currentIndex = i
                                } else {
                                    addError.visible = true
                                }
                            }
                        }
                    }
                }
            }
        }

        // Forgetting a host asks first: its name and last user go with it.
        Rectangle {
            anchors.fill: parent
            visible: win.forgetVisible && win.selected !== null
            color: Qt.rgba(0, 0, 0, 0.6)
            MouseArea { anchors.fill: parent; onClicked: win.forgetVisible = false }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(440 * win.u, parent.width - 48)
                height: forgetColumn.implicitHeight + 40
                radius: theme.radius
                color: theme.panel
                border.color: theme.panelBorder
                MouseArea { anchors.fill: parent }
                ColumnLayout {
                    id: forgetColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: win.selected ? qsTr("Forget %1? Its name and last user go too; a scan finds it again if it's on.").arg(win.selected.name || win.selected.address) : ""
                        color: theme.text
                        font.pixelSize: Math.round(14 * win.u)
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button { flat: true; text: qsTr("Cancel"); onClicked: win.forgetVisible = false }
                        PrimaryButton {
                            text: qsTr("Forget")
                            onClicked: { rdp.forget(win.selected.address); win.forgetVisible = false }
                        }
                    }
                }
            }
        }
    }

    AboutOverlay {
        visible: win.aboutVisible
        appName: "o-rdp"
        purpose: qsTr("Find the PCs with Remote Desktop on your network and connect to them")
        repository: "github.com/nerdcules/o-rdp"
        onDismissed: win.aboutVisible = false
    }
}
