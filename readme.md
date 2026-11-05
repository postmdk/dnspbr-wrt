# dnspbr (DNS Policy-Based Routing for OpenWrt)

[![Build and Release](https://github.com/postmdk/dnspbr-wrt/actions/workflows/build.yml/badge.svg)](https://github.com/postmdk/dnspbr-wrt/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

`dnspbr` is a high-performance, lightweight C++ network daemon and OpenWrt package designed to perform policy-based routing (PBR) using dynamic DNS resolution tracking. 

It intercepts DNS queries for target domains, maps resolved IP addresses into virtual subnet pools, tags matching traffic using `nftables` firewall marks (`fwmark`), and routes the packets through dedicated network interfaces (e.g., WireGuard, AmneziaWG, OpenVPN, or OpenWrt WAN gateways).

---

## Key Features

* **High Performance & Low Latency:** Written in native C++ with multi-threading support (`libpthread`).
* **Dynamic IP Mapping:** Automatically intercepts DNS responses and maps resolved IPs into a predefined internal subnet (`route_subnet`).
* **`nftables` Integration:** Automatically applies firewall marks (`0x40`) for targeted routing rules.
* **Modern OpenWrt Support:** Dynamic discovery of jail/chroot paths for `dnsmasq` (`/tmp/dnsmasq.*.d/`).
* **Multi-Arch & Multi-Version Builds:** Pre-compiled binaries for `aarch64_cortex-a53` and `mipsel_24kc` supporting OpenWrt versions `24.10.x` (`.ipk`) and `25.12.x` (`.apk`).

---

## Packet Flow Architecture

Below is the conceptual diagram showing how DNS requests and data traffic are processed by `dnspbr` and the Linux kernel routing stack.

```
[ Client Device ]
       │
       │ 1. DNS Query (e.g., target.com)
       ▼
[ dnsmasq / Unbound ] ───(Forward matching domains)───► [ dnspbr Daemon ] (Port 5353)
                                                                │
                                                                │ 2. Upstream Resolver (Port 5354)
                                                                ▼
                                                        [ Upstream DNS / DoH ]
                                                                │
[ Client Device ] ◄───(3. Modified IP response)───────── [ dnspbr Daemon ]
       │
       │ 4. HTTP/TLS Traffic to Mapped IP (e.g., 10.30.0.X)
       ▼
[ Linux Kernel (nftables) ] ───(Matches 10.30.0.0/16)───► Set mark (0x40)
       │
       │ 5. PBR Rule (ip rule fwmark 0x40 lookup 100)
       ▼
[ Target Interface ] (e.g., awg0 / wg0 / tun0)
```

### Detailed Flow Steps:

1. **DNS Interception:** Client sends a request for a target domain. `dnsmasq` forwards targeted domains to `dnspbr` listening on port `5353`.
2. **Upstream Resolution:** `dnspbr` forwards the query to the designated upstream DNS server on port `5354`.
3. **Subnet Mapping:** Upon receiving the real IP from the upstream server, `dnspbr` maps the IP into a synthetic internal IP pool (e.g., `10.30.0.0/16`) and returns this mapped address back to the client.
4. **Packet Tagging:** When the client sends IP traffic to the mapped IP range, `nftables` detects the target subnet and marks the IP packets with `fwmark 0x40`.
5. **Policy Routing:** The Linux kernel routing table evaluates `fwmark 0x40` and redirects the traffic through the specified interface (e.g., AmneziaWG interface `awg0`).

---

## UCI Configuration Guide

The primary configuration file is located at `/etc/config/dnspbr`.

### Configuration Example

```uci
config dnspbr 'main'
    option enabled '1'
    option listen_ip '127.0.0.1'
    option listen_port '5353'
    option upstream_ip '127.0.0.1'
    option upstream_port '5354'
    option route_subnet '10.30.0.0/16'
    option fw_mark '0x40'
    option interface 'awg0'
    option workers '2'
```

### Option Reference

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `enabled` | `boolean` | `0` | Enables (`1`) or disables (`0`) the service daemon on startup. |
| `listen_ip` | `ipaddr` | `127.0.0.1` | Local IP address for `dnspbr` to listen for incoming DNS queries. |
| `listen_port` | `port` | `5353` | Local UDP port on which `dnspbr` accepts DNS traffic from `dnsmasq`. |
| `upstream_ip` | `ipaddr` | `127.0.0.1` | IP address of the upstream resolver (e.g., `127.0.0.1` or SmartDNS). |
| `upstream_port` | `port` | `5354` | Upstream DNS port to query for actual public IP resolution. |
| `route_subnet` | `cidr` | `10.30.0.0/16` | Virtual subnet range allocated for mapping resolved host IPs. |
| `fw_mark` | `hex` | `0x40` | Hexadecimal firewall mark (`fwmark`) assigned to marked traffic. |
| `interface` | `string` | `awg0` | Target network interface to route tagged traffic through. |
| `workers` | `integer` | `2` | Number of worker threads for processing concurrent DNS resolutions. |

---

## Hardware & Resource Overhead Notes

* **RAM Usage:** ~12 MB Resident Set Size (RSS) under average workload.
* **CPU Usage:** ~2% CPU utilization on low-to-mid range ARM/MIPS routers during active querying.
* **Memory Considerations:** Devices with $\le 128 \text{ MB}$ RAM should run with `workers '1'` to minimize heap allocation and avoid potential Out-Of-Memory (OOM) kernel triggers.

---

## Installation

### Pre-compiled Binary Packages

Download the latest release for your architecture and OpenWrt version from the [GitHub Releases](https://github.com/postmdk/dnspbr-wrt/releases) page.

#### OpenWrt 24.10.x (`.ipk` packages):
```bash
opkg update
opkg install dnspbr-1.1.3-r1-aarch64_cortex-a53_24.10.8.ipk
```

#### OpenWrt 25.12.x (`.apk` packages):
```bash
apk add --allow-untrusted dnspbr-1.1.3-r1-aarch64_cortex-a53_25.12.6.apk
```

---

## Service Management

Manage the daemon using standard OpenWrt init commands:

```bash
# Enable service on boot
/etc/init.d/dnspbr enable

# Start daemon
/etc/init.d/dnspbr start

# Check service status
/etc/init.d/dnspbr status

# Restart daemon
/etc/init.d/dnspbr restart

# Tamplate for dnsmasq
/etc/dnspbr/dnsmasq/dnsmasq.conf.examle
You can rename it to `*.conf` and add your own configuration; the daemon will then automatically configure Dnsmasq upon startup.
```

---

## Building from Source

To compile `dnspbr` using an official OpenWrt SDK:

1. Clone or link `dnspbr` into your SDK feeds directory:
   ```bash
   echo "src-git dnspbr https://github.com/postmdk/dnspbr-wrt.git" >> feeds.conf.default
   ./scripts/feeds update dnspbr
   ./scripts/feeds install dnspbr
   ```

2. Configure and compile:
   ```bash
   make defconfig
   make package/dnspbr/compile V=s
   ```

3. Compiled output will be available under:
   `bin/packages/<arch>/dnspbr/`

---

## License

This project is licensed under the [MIT License](LICENSE).