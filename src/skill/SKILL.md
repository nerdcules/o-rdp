---
name: o-rdp
description: Find the PCs with Remote Desktop on the local network and connect to them, through the o-rdp command. Use when asked which machines accept RDP, what a PC on the LAN is called, or to open a Remote Desktop session.
---

# o-rdp

- `o-rdp scan --json`: `{subnet, hosts: [{address, name}]}` for every address
  on the default route's subnet with TCP 3389 open (half-second probes, 256 at
  a time; a /20 or larger is cut to the /24 around this computer). Names come
  from NetBIOS, then reverse DNS, then Avahi. Takes several seconds.
- `o-rdp list --json`: known hosts `[{address, name, user}]`, shared with the
  rdp-connect script (`~/.local/state/rdp-connect/known`).
- `o-rdp connect HOST [--user U]`: opens a FreeRDP window. It asks for the
  password on the terminal unless the keyring holds one; don't drive this for
  the user without their say-so.

Exit 0 on success, 1 with the problem on stderr.
