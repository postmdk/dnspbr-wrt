# dnspbr (DNS Policy-Based Routing for OpenWrt)

`dnspbr` is a high-performance, lightweight C++ network daemon and OpenWrt package designed to perform policy-based routing (PBR) using dynamic DNS resolution tracking.

It intercepts DNS queries for target domains, maps resolved IP addresses into virtual subnet pools, tags matching traffic using `nftables` firewall marks (`fwmark`), and routes the packets through dedicated network interfaces (e.g., WireGuard, AmneziaWG, OpenVPN, or OpenWrt WAN gateways).

## Key Features

* **Multi-Instance Architecture:** Run multiple independent `dnspbr` instances simultaneously under `procd`, each with its own listener port, Fake DNS subnet, `nftables` table/map, firewall mark, routing table, and target interface.
* **High Performance & Low Latency:** Written in native C++ with multi-threading support (`libpthread`).
* **Dynamic IP Mapping:** Automatically intercepts DNS responses and maps resolved IPs into predefined internal subnets (`range`).
* **`nftables` Integration:** Dynamically creates per-instance `nftables` DNAT and mangle tables (`dnsmap_<instance>` and `mangle_dnsmap_<instance>`) to tag matching traffic with `fwmark`.
* **Modern OpenWrt Support:** Dynamic discovery of jail/chroot paths for `dnsmasq` (`/tmp/dnsmasq*d/`) and per-instance domain forwarding configs.
* **LuCI Web UI (`luci-app-dnspbr`):** Full web interface support for managing multiple instances, monitoring per-instance daemon status, and editing domain lists per instance.
* **Multi-Arch & Multi-Version Builds:** Pre-compiled binaries for `aarch64_cortex-a53` and `mipsel_24kc` supporting OpenWrt versions `24.10.x` (`.ipk`) and `25.12.x` (`.apk`).

## Packet Flow Architecture

Below is the conceptual diagram showing how DNS requests and data traffic are processed across multiple `dnspbr` instances and the Linux kernel routing stack.

```
[ Client Device ]
       │
       │ 1. DNS Query (e.g., domain-a.com / domain-b.com)
       ▼
[ dnsmasq ]
       ├──(server=/domain-a.com/127.0.0.1#5353)──► [ dnspbr (awg0) ] (Port 5353)
       └──(server=/domain-b.com/127.0.0.1#5453)──► [ dnspbr (awg1) ] (Port 5453)
                                                           │
                                                           │ 2. Upstream Resolver (Port 5354)
                                                           ▼
                                                   [ Upstream DNS / DoH ]
                                                           │
[ Client Device ] ◄───(3. Mapped IP: 10.64.0.X / 10.32.0.X)
       │
       │ 4. HTTP/TLS Traffic to Mapped IP
       ▼
[ Linux Kernel (nftables) ]
       ├──(Matches 10.64.0.0/16 in dnsmap_awg0)──► Set mark 0x101 ──► Table 101 ──► [ awg0 ]
       └──(Matches 10.32.0.0/16 in dnsmap_awg1)──► Set mark 0x102 ──► Table 102 ──► [ awg1 ]
```

### Detailed Flow Steps:

1. **DNS Interception:** Client sends a request for a target domain. `dnsmasq` forwards matching domains to the corresponding `dnspbr` instance listener (e.g., port `5353` for `awg0` or `5453` for `awg1`).
2. **Upstream Resolution:** `dnspbr` forwards the query to the designated upstream DNS server on port `5354`.
3. **Subnet Mapping:** Upon receiving the real IP from the upstream server, `dnspbr` maps it into the instance's synthetic internal IP pool (e.g., `10.64.0.0/16` or `10.32.0.0/16`) and returns this mapped address back to the client.
4. **Packet Tagging:** When the client sends IP traffic to the mapped IP range, the instance's `nftables` table translates the destination back to the real IP and marks packets with the configured `fwmark` (e.g., `0x101` or `0x102`).
5. **Policy Routing:** The Linux kernel policy routing rules (`ip rule`) evaluate the `fwmark` and route the traffic through the instance's dedicated routing table and interface.

## Configuration & Integration with Dnsmasq

### UCI Configuration (`/etc/config/dnspbr`)

The primary configuration file is located at `/etc/config/dnspbr`. Each `config instance` section defines an isolated `dnspbr` daemon instance managed by `procd`.

> **Important:** When running multiple instances, ensure that `port`, `range`, `table`, `route_table`, and `mark` are unique for each enabled instance to avoid port conflicts and overlapping routing rules.

```uci
config instance 'awg0'
    option enabled '1'
    option listen '127.0.0.1'
    option port '5353'
    option upstream '127.0.0.1'
    option upstream_port '5354'
    option range '10.64.0.0/16'
    option table 'dnsmap_awg0'
    option route_table '101'
    option mark '0x101'
    option interface 'awg0'
    option workers '2'
    option verbose '0'

config instance 'awg1'
    option enabled '1'
    option listen '127.0.0.1'
    option port '5453'
    option upstream '127.0.0.1'
    option upstream_port '5354'
    option range '10.32.0.0/16'
    option table 'dnsmap_awg1'
    option route_table '102'
    option mark '0x102'
    option interface 'awg1'
    option workers '2'
    option verbose '0'
```

### Option Reference

| Option | Type | Default | Description |
| ----- | ----- | ----- | ----- |
| `enabled` | `boolean` | `0` | Enables (`1`) or disables (`0`) this `dnspbr` instance. |
| `listen` | `ipaddr` | `127.0.0.1` | Local IP address for the instance to listen for incoming DNS queries. |
| `port` | `port` | `5353` | Local UDP port on which the instance accepts DNS traffic from `dnsmasq`. |
| `upstream` | `ipaddr` | `9.9.9.10` | IP address of the upstream resolver (e.g., `127.0.0.1` or SmartDNS). |
| `upstream_port` | `port` | `53` | Upstream DNS port to query for actual public IP resolution. |
| `range` | `cidr` | `10.30.0.0/16` | Virtual subnet range allocated for mapping resolved host IPs for this instance. |
| `table` | `string` | `dnsmap_<name>` | Dedicated `nftables` table name created for this instance. |
| `route_table` | `string` | `101` | Policy routing table ID or name used for this instance. |
| `mark` | `hex` | `0x101` | Hexadecimal firewall mark (`fwmark`) assigned to matching traffic. |
| `interface` | `string` | *(optional)* | Target network interface to route tagged traffic through (e.g., `awg0`, `wg0`). |
| `workers` | `integer` | `2` | Number of worker threads for processing concurrent DNS resolutions. |
| `verbose` | `boolean` | `0` | Enables (`1`) detailed debug logging for this instance. |

### Dnsmasq Configuration

`dnspbr` integrates seamlessly with `dnsmasq` by dynamically injecting per-instance domain rules into active configuration directories (including chroot/jail environments like `/tmp/dnsmasq*d/`).

Disable DNS rebind protection so `dnsmasq` accepts synthetic private IP responses (`10.x.x.x`) from `dnspbr`:
```bash
uci set dhcp.@dnsmasq[0].rebind_protection='0'
uci commit dhcp
service dnsmasq restart
```

Each instance uses its own configuration file under `/etc/dnspbr/dnsmasq/<instance>.conf` (for example, `/etc/dnspbr/dnsmasq/awg0.conf` and `/etc/dnspbr/dnsmasq/awg1.conf`), routing domains to the corresponding instance `listen` IP and `port`:

```text
# /etc/dnspbr/dnsmasq/awg0.conf
server=/example.com/127.0.0.1#5353
```

```text
# /etc/dnspbr/dnsmasq/awg1.conf
server=/another-example.org/127.0.0.1#5453
```

1. Upon startup or reload, `/etc/init.d/dnspbr` automatically copies active `*.conf` files from `/etc/dnspbr/dnsmasq/` into the active `dnsmasq` config directory (`/tmp/dnsmasq*d/dnspbr_<instance>.conf`) and restarts `dnsmasq`.
2. When an instance is disabled (`enabled '0'`), its `nftables` tables, routing rules, and active `dnsmasq` forwarding file are automatically cleaned up.
3. Install `luci-app-dnspbr` to manage instances and per-instance domain lists directly from the OpenWrt LuCI web interface.

## Hardware & Resource Overhead Notes

* **RAM Usage:** ~12–13 MB Resident Set Size (RSS) per running instance under average workload.
* **CPU Usage:** ~2% CPU utilization on low-to-mid range ARM/MIPS routers during active querying.
* **Memory Considerations:** Devices with $\le 128 \text{ MB}$ RAM should limit the number of active instances and run with `workers '1'` to minimize heap allocation and avoid potential Out-Of-Memory (OOM) kernel triggers.

## Installation

### Pre-compiled Binary Packages

Download the latest release for your architecture and OpenWrt version from the [GitHub Releases](https://github.com/postmdk/dnspbr-wrt/releases) page.

#### OpenWrt 24.10.x (`.ipk` packages):

```bash
opkg update
opkg install dnspbr-<pkg_ver>-<arc>_24.10.8.ipk
opkg install luci-app-dnspbr-<pkg_ver>_all.ipk
```

#### OpenWrt 25.12.x (`.apk` packages):

```bash
apk add --allow-untrusted dnspbr-<pkg_ver>-<arc>_25.12.6.apk
apk add --allow-untrusted luci-app-dnspbr-<pkg_ver>.apk
```

## Service Management

Manage the daemon and inspect running instances using standard OpenWrt init and `ubus` commands:

```bash
# Enable service on boot
/etc/init.d/dnspbr enable

# Start all enabled instances
/etc/init.d/dnspbr start

# Check running instances via ubus
ubus call service list '{"name":"dnspbr"}'

# Restart service and reload dnsmasq rules
/etc/init.d/dnspbr restart
```

## Optional: DNS hijacking: Configure firewall to intercept DNS

### Domain Name System traffic:
```bash
uci -q del firewall.dns_int
uci set firewall.dns_int="redirect"
uci set firewall.dns_int.name="Intercept-DNS"
uci set firewall.dns_int.family="any"
uci set firewall.dns_int.proto="tcp udp"
uci set firewall.dns_int.src="lan"
uci set firewall.dns_int.src_dport="53"
uci set firewall.dns_int.target="DNAT"
uci commit firewall
service firewall restart
service dnspbr restart
```

### Notice
Restarting the firewall clears DNAT and mark rules, which is why the `dnspbr` service also needs to be restarted.

Also see: [dnsproxy](https://openwrt.org/docs/guide-user/services/dns/dot_dnsmasq_dnsproxy) DoH/DoH3, DoT, DoQ and DNSCrypt with Dnsmasq and dnsproxy

## Building from Source

To compile `dnspbr` using an official OpenWrt SDK:

1. Clone or link `dnspbr` into your SDK feeds directory:
   ```bash
   echo "src-git dnspbr https://github.com/postmdk/dnspbr-wrt.git" >> feeds.conf.default
   ./scripts/feeds update -a
   ./scripts/feeds install -a
   ```

2. Configure and compile:
   ```bash
   make defconfig
   make package/dnspbr/compile V=s
   make package/luci-app-dnspbr/compile V=s
   ```

3. Compiled output will be available under:
   `bin/packages/<arch>/dnspbr/`
## See also [dnspbr for linux](https://github.com/postmdk/dnspbr.git)
## License

This project is licensed under the [MIT License](LICENSE).
