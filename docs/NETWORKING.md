# Networking with NE2000

FreeDOS emulates an NE2000-compatible Ethernet adapter based on the DP8390
chipset. This is the same NIC that QEMU and most DOS-era emulators provide,
so standard DOS networking software works out of the box.

## Hardware Configuration

| Parameter | Value |
|-----------|-------|
| I/O Base  | 0x300 |
| IRQ       | 3     |
| MAC Address | 52:54:00:12:34:56 |
| Buffer RAM | 16 KB (pages 0x40–0x7F) |

These are the defaults used by virtually all NE2000 packet drivers.

## Enabling Networking

### Command-Line Interface

Pass the `-net` flag when starting the emulator:

```bash
./dosemu_cli -a fd/freedos.img -net
./dosemu_cli -c fd/freedos_hd.img -boot c -net
```

### iOS / macOS App

Networking support in the app is planned for a future release.

## Software Setup

You need two pieces of DOS software:

1. **NE2000 packet driver** — provides the low-level interface between
   the NIC hardware and TCP/IP applications
2. **TCP/IP stack** — mTCP is recommended for its small footprint and
   FreeDOS compatibility

### Step 1: Get the Software

- **Crynwr NE2000 packet driver**: included with FreeDOS, or download from
  [crynwr.com](http://crynwr.com/drivers/)
- **mTCP**: download from [brutman.com/mTCP](http://www.brutman.com/mTCP/)

### Step 2: Load the Packet Driver

The NE2000 packet driver takes three arguments: software interrupt number,
hardware IRQ, and I/O base address.

```
C:\DRIVERS> NE2000 0x60 3 0x300
```

- `0x60` — software interrupt used by TCP/IP applications to talk to the driver
- `3` — hardware IRQ (must match the emulated NIC)
- `0x300` — I/O base address (must match the emulated NIC)

You should see a message like:

```
Packet driver for NE2000, version X.X
Packet driver functionality is at interrupt 0x60
My Ethernet address is 52:54:00:12:34:56
```

### Step 3: Configure mTCP

Create a configuration file (e.g., `C:\MTCP\MTCP.CFG`):

```
PACKETINT 0x60
HOSTNAME  DOSEMU
IPADDR    192.168.1.100
NETMASK   255.255.255.0
GATEWAY   192.168.1.1
NAMESERVER 8.8.8.8
```

Or leave out IPADDR/NETMASK/GATEWAY and use DHCP (see below).

Set the environment variable:

```
SET MTCPCFG=C:\MTCP\MTCP.CFG
```

### Step 4: Get an IP Address

If your network has a DHCP server:

```
C:\MTCP> DHCP
```

This will automatically fill in IPADDR, NETMASK, GATEWAY, and NAMESERVER
in your config file.

For a static IP, edit `MTCP.CFG` directly with the values above.

### Step 5: Use the Network

mTCP includes several network applications:

| Program | Description |
|---------|-------------|
| `FTP` | FTP client |
| `TELNET` | Telnet client |
| `IRC` | IRC client |
| `PING` | Ping utility |
| `NETCAT` | TCP connection tool |
| `HTGET` | HTTP file downloader |
| `SNTP` | Set clock from NTP server |

Examples:

```
C:\MTCP> PING 8.8.8.8
C:\MTCP> FTP ftp.example.com
C:\MTCP> TELNET bbs.example.com
C:\MTCP> HTGET http://example.com/file.zip file.zip
```

## AUTOEXEC.BAT Example

To load networking automatically at boot:

```bat
@ECHO OFF
PATH C:\FREEDOS;C:\DRIVERS;C:\MTCP
SET MTCPCFG=C:\MTCP\MTCP.CFG
NE2000 0x60 3 0x300
DHCP
```

## Troubleshooting

**"No NE2000 found at 0x300"**
- Make sure you started the emulator with the `-net` flag
- Verify the I/O base (0x300) and IRQ (3) match the driver arguments

**DHCP times out**
- Check that the host machine has network connectivity
- The emulator's virtual NIC must be bridged to a real network interface
  (CLI mode uses the host's network stack directly)

**Ping works but FTP/HTTP doesn't**
- Check your NAMESERVER setting in MTCP.CFG
- Try connecting by IP address instead of hostname to isolate DNS issues

**Packet driver loads but no traffic**
- Ensure the PIC interrupt mask allows IRQ 3. The packet driver handles
  this automatically, but custom configurations might interfere.
