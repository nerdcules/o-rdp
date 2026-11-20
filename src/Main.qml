import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ORdp

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
    // With a PC on show, the menu is a panel that flies out over it.
    property bool menuOpen: false
    readonly property bool inSession: rdp.session !== null
    readonly property real u: theme.fontScale
    readonly property bool wide: menu.width >= 820 * u
    readonly property var selected: list.currentIndex >= 0 && list.currentIndex < rdp.hosts.length ? rdp.hosts[list.currentIndex] : null
    readonly property var shown: {
        if (!rdp.session) return null
        for (var i = 0; i < rdp.hosts.length; ++i)
            if (rdp.hosts[i].address === rdp.session.address) return rdp.hosts[i]
        return null
    }
    readonly property string shownName: shown ? (shown.name || shown.address) : (rdp.session ? rdp.session.address : "")
    // Typing in a field keeps its letters; the window's single-key
    // shortcuts only act outside text fields.
    readonly property bool typing: activeFocusItem instanceof TextInput
    // While the PC has the keyboard, every key goes to it, Esc and Q too.
    readonly property bool keysToPc: view.activeFocus

    function openMenu() {
        if (!inSession) return
        menuOpen = true
        list.forceActiveFocus()
    }
    function closeMenu() {
        menuOpen = false
        if (inSession) view.forceActiveFocus()
        else list.forceActiveFocus()
    }
    // Full screen belongs to the PC on show; the list stays in a window.
    function placeWindow() {
        var full = inSession && rdp.fullscreen
        if (full && visibility !== Window.FullScreen) visibility = Window.FullScreen
        else if (!full && visibility === Window.FullScreen) visibility = Window.Windowed
    }
    Connections {
        target: rdp
        function onSessionChanged() { win.closeMenu(); win.placeWindow() }
        function onOptionsChanged() { win.placeWindow() }
    }

    function stepBack() {
        if (aboutVisible) aboutVisible = false
        else if (helpVisible) helpVisible = false
        else if (addVisible) addVisible = false
        else if (forgetVisible) forgetVisible = false
        else if (rdp.problem.text) rdp.clearProblem()
        else if (inSession && menuOpen) closeMenu()
        else list.forceActiveFocus()
    }
    function move(delta) {
        if (rdp.hosts.length === 0) return
        list.currentIndex = Math.max(0, Math.min(rdp.hosts.length - 1, list.currentIndex + delta))
    }

    Shortcut { sequence: "Ctrl+I"; enabled: !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.aboutVisible = !win.aboutVisible }
    Shortcut { sequence: "?"; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; enabled: !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.stepBack() }
    Shortcut { sequence: "Q"; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "R"; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: rdp.scan() }
    Shortcut { sequence: "N"; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.addVisible = true }
    Shortcut { sequences: ["J", "Down"]; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.move(1) }
    Shortcut { sequences: ["K", "Up"]; enabled: !win.typing && !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: win.move(-1) }
    Shortcut { sequences: ["Space", "Return", "Enter"]; enabled: !win.typing && !win.keysToPc && win.selected !== null; context: Qt.ApplicationShortcut; onActivated: panel.start() }
    Shortcut { sequence: "Delete"; enabled: !win.typing && !win.keysToPc && win.selected !== null; context: Qt.ApplicationShortcut; onActivated: win.forgetVisible = true }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: theme.increaseFont() }
    Shortcut { sequence: "Ctrl+-"; enabled: !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: theme.decreaseFont() }
    Shortcut { sequence: "Ctrl+0"; enabled: !win.keysToPc; context: Qt.ApplicationShortcut; onActivated: theme.resetFont() }
    // The two keys o-rdp keeps for itself while the PC has the keyboard.
    Shortcut { sequence: "Ctrl+Alt+Home"; enabled: win.inSession; context: Qt.ApplicationShortcut; onActivated: win.menuOpen ? win.closeMenu() : win.openMenu() }
    Shortcut { sequences: ["Ctrl+Alt+Return", "Ctrl+Alt+Enter"]; enabled: win.inSession; context: Qt.ApplicationShortcut; onActivated: rdp.fullscreen = !rdp.fullscreen }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // The PC on show fills the window.
        RdpView {
            id: view
            anchors.fill: parent
            visible: win.inSession
            session: rdp.session
        }
        Label {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 520 * win.u)
            visible: win.inSession && rdp.session.state !== RdpSession.Connected
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: Math.round(15 * win.u)
            text: qsTr("Connecting to %1…").arg(win.shownName)
        }

        // Without one, the menu is the window.
        Item {
            id: fullPane
            anchors.fill: parent
            visible: !win.inSession
        }

        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: win.inSession && win.menuOpen ? 0.45 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            MouseArea { anchors.fill: parent; onClicked: win.closeMenu() }
        }

        Rectangle {
            id: flyout
            readonly property bool out: win.inSession && win.menuOpen
            width: Math.min(parent.width - 48 * win.u, 920 * win.u)
            height: parent.height
            x: out ? 0 : -width - 2
            visible: win.inSession && x > -width - 2
            color: theme.window
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            MouseArea { anchors.fill: parent }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: theme.panelBorder
            }
        }

        // The tab that brings the menu out, at the top left of the PC.
        Rectangle {
            id: handle
            visible: win.inSession && !win.menuOpen
            x: -radius
            y: 12 * win.u
            width: 34 * win.u + radius
            height: 34 * win.u
            radius: theme.radius
            color: theme.panel
            border.color: theme.panelBorder
            opacity: handleArea.containsMouse ? 0.95 : 0.6
            Label {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: handle.radius / 2
                text: "☰"
                color: theme.text
                font.pixelSize: Math.round(16 * win.u)
            }
            MouseArea {
                id: handleArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: win.openMenu()
            }
            ToolTip.visible: handleArea.containsMouse
            ToolTip.delay: 400
            ToolTip.text: qsTr("Menu (Ctrl+Alt+Home)")
        }

        Item {
            id: menu
            parent: win.inSession ? flyout : fullPane
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                // The PC on show, and what can be done with it.
                Rectangle {
                    Layout.fillWidth: true
                    visible: win.inSession
                    implicitHeight: sessionRow.implicitHeight + 16
                    radius: theme.radius
                    color: theme.panel
                    border.color: theme.panelBorder
                    border.width: 1
                    RowLayout {
                        id: sessionRow
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 14
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: theme.text
                            font.pixelSize: Math.round(14 * win.u)
                            font.weight: Font.DemiBold
                            text: qsTr("On show: %1").arg(win.shownName)
                        }
                        Button {
                            flat: true
                            text: qsTr("Send Ctrl+Alt+Del")
                            onClicked: { view.sendCtrlAltDel(); win.closeMenu() }
                        }
                        Button {
                            flat: true
                            text: rdp.fullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
                            onClicked: rdp.fullscreen = !rdp.fullscreen
                        }
                        Button {
                            flat: true
                            text: qsTr("Disconnect")
                            onClicked: rdp.disconnectFrom(rdp.session.address)
                        }
                        PrimaryButton {
                            text: qsTr("Back to the PC (Esc)")
                            onClicked: win.closeMenu()
                        }
                    }
                }

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
                    visible: !!rdp.problem.text
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
                            text: qsTr("%1: %2").arg(rdp.problem.address || "").arg(rdp.problem.text || "")
                        }
                        Label {
                            visible: !!rdp.problem.details
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
                    text: win.inSession
                          ? qsTr("J/K select · Enter connect or show · Esc back to the PC · Ctrl+Alt+Home menu · ? help")
                          : qsTr("J/K select · Enter connect · R scan · N add address · Delete forget · ? help · Q quit")
                    color: theme.muted
                    font.pixelSize: Math.round(12 * win.u)
                    elide: Text.ElideRight
                }
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
