<img src="pkgbuild/o-rdp.svg" width="72" alt="">

# o-rdp

*Idea: [nerdcules](https://github.com/nerdcules) · Written by: Claude, in [Claude Code](https://claude.com/claude-code)*

![o-rdp listing the PCs on the network (made-up hosts)](docs/screenshot.png)

o-rdp finds the PCs with Remote Desktop on your local network and connects to
them with FreeRDP, inside its own window. It is the window version of the `rdp-connect` script, and
shares its list of known PCs (`~/.local/state/rdp-connect/known`: address,
name and the last user), so either one picks up what the other learned.

## What it does

- **Scans** the subnet of the connection that carries the default route (so a
  VPN isn't scanned): a TCP connect to port 3389 on every address, 256 at a
  time, half a second each. Anything larger than a /20 is cut to the /24
  around this computer, as the script does. It scans when it opens; R scans
  again.
- **Names** each PC it finds: NetBIOS first (Windows answers with its own
  computer name), then reverse DNS, then Avahi.
- **Connects** in the same window: the PC's desktop fills it, and the menu
  (the list, the connect panel, and Send Ctrl+Alt+Del, Full screen and
  Disconnect for the PC on show) flies out from the left, from the ☰ tab at
  the top left or with Ctrl + Alt + Home. FreeRDP's library (libfreerdp3)
  runs the connection inside o-rdp with the script's options: the desktop
  follows the window's size, the PC's certificate is trusted the first time
  and checked after that, and sound, clipboard text, LAN settings and
  reconnecting are on. A PC that drops the connection again straight after
  each reconnect is given up after three tries in 30 seconds. Several PCs
  can be open at once; the menu switches between them. Full screen is a
  switch; `$RDP_OPTS` adds FreeRDP flags (`/kbd:` sets the PC's keyboard
  layout, which otherwise follows this computer's language).
- **Passwords** are typed in the window. With "Remember the password in the
  keyring" ticked they are kept in the GNOME keyring (`secret-tool`, service
  `o-rdp`, per PC and user); unticking removes the stored one. The window
  hands the password to libfreerdp in memory, never on a command line that
  other programs on the computer can read; `o-rdp connect` sends it to
  `xfreerdp3` through `/args-from:stdin`, a pipe.
- **Says what went wrong** when a connection fails: the user name or
  password turned down, no password given where the PC wants one, the
  account locked or expired, the PC unreachable, the connection broken off,
  a changed certificate, with FreeRDP's error under it.

## Keys

While a PC is on show and the menu is put away, every key goes to the PC
except Ctrl + Alt + Home and Ctrl + Alt + Enter.

| Key | Does |
| --- | --- |
| J / K, ↓ / ↑ | Select the next or previous PC |
| Enter, Space | Connect (or go to the field still missing); show a PC already open |
| R | Scan the network again |
| N | Add a PC by address or name |
| Delete | Forget the selected PC |
| Esc | Leave a field, or close what is open (the menu, over a PC) |
| Ctrl + Alt + Home | Bring out the menu over the PC on show, or put it away |
| Ctrl + Alt + Enter | Full screen on or off while a PC is on show |
| Ctrl + / - / 0 | Text larger, smaller, default |
| ? | Every shortcut |
| Ctrl + I | About o-rdp |
| Q | Quit |

## Commands

```text
o-rdp scan [--json]              the PCs with port 3389 open, with names
o-rdp list [--json]              the PCs o-rdp and rdp-connect know
o-rdp connect HOST [--user U]    connect in an xfreerdp3 window; the password from the keyring or asked here
o-rdp skill install              a short agent skill in ~/.claude/skills/o-rdp
```

## Install

```sh
bin/install
```

builds the package with `makepkg -fsi` (it asks for your password to
install it), so o-rdp appears in the app launcher (Super + Space). Needs
`freerdp` (the window links its libfreerdp3; `o-rdp connect` runs its
`xfreerdp3`); `libsecret` for the keyring and `avahi` for the last naming
step are optional.

### Updating

[`nerdcules-update`](https://github.com/nerdcules/nerdcules#updates) builds
o-rdp from this repository's release tags and installs it, and goes back:

```sh
nerdcules-update apply o-rdp                 # the newest release
nerdcules-update channel o-rdp prerelease    # also pre-releases (v1.2.0-rc.1); or main
nerdcules-update rollback o-rdp              # the version before
```

No release is tagged yet, so the stable channel has nothing to offer; until
one is, `nerdcules-update channel o-rdp main` follows every commit.

## Development

```sh
bin/build     # build/o-rdp
bin/test      # the Qt Test suite, offscreen
```

The tests cover the subnet and address range, the default route, NetBIOS
queries and answers, the known-hosts file's format, FreeRDP's arguments and
error messages, the keyring calls, the scanner against a local port, the
keys' RDP scancodes, how a desktop is fitted into the window, and a
connection made inside o-rdp to a port nothing listens on (shown at once,
then the problem and the list back, the password in no argument). On
2026-11-20 `bin/test` ran 13 tests: 13 passed, 0 failed, 0 skipped.

Connecting was also tried (2026-11-20) against FreeRDP's sample server
(`sfreerdp-server3` 3.32.2, on a local socket, `RDP_OPTS="/sec:tls
/network:auto"`), with `Main.qml` drawn offscreen: the desktop arrived in
the window (1000 × 680), the menu flew out over it, Disconnect brought the
list back with no problem shown, and a new certificate on the same server
was refused with the "not the one seen last time" message. The sample
server has no display-control channel, so resizing the desktop with the
window, keys, mouse, clipboard, pointer shapes and switching between
several open PCs have not been tried against a PC. With `/network:lan` that server drops every connection; it
ended after three reconnects with "The connection to the PC broke off."

On this laptop (2026-11-17) a scan of a /22 took 1,997 ms; `rdp-connect
--list` took 2,590 ms on the same network. Both found no PC with port 3389
open at the time, so connecting to a real PC hasn't been tried yet.

## License

MIT
