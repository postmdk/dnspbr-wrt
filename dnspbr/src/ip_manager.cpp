#include "ip_manager.hpp"
#include <arpa/inet.h>
#include <cmath>
#include <syslog.h>
#include "nlohmann/json.hpp"
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
using json = nlohmann::json;

static string get_interface_ip(const std::string& iface_name) {
    struct ifaddrs *ifaddr, *ifa;
    string ip_str = "";

    if (getifaddrs(&ifaddr) == -1) return "";

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET && iface_name == ifa->ifa_name) {
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            char addr_buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sin->sin_addr), addr_buffer, INET_ADDRSTRLEN);
            ip_str = string(addr_buffer);
            break;
        }
    }
    freeifaddrs(ifaddr);
    return ip_str;
}

IPManager::IPManager(const std::string& cidr, const std::string& mark, const std::string& interface_name, bool debug_mode)
        : fw_mark(mark), tunnel_interface(interface_name), debug(debug_mode) {
    // Если имя интерфейса передано, настраиваем автоматику маршрутизации
    if (!tunnel_interface.empty()) {
        string iface_ip = get_interface_ip(tunnel_interface);

        if (!iface_ip.empty()) {
            syslog(LOG_NOTICE, "Auto-detected interface %s IP: %s", tunnel_interface.c_str(), iface_ip.c_str());
        } else {
            syslog(LOG_WARNING, "Interface %s specified, but IPv4 address not found yet!", tunnel_interface.c_str());
        }
        // First, we remove any old rule with the same label, should any remain from previous runs.
        string del_old_rule = "ip rule del fwmark " + fw_mark + " table 100 2>/dev/null";
        system(del_old_rule.c_str());
        // Automatically add a routing policy rule for labels (fwmark -> table 100)
        string rule_cmd = "ip rule add fwmark " + fw_mark + " table 100 2>/dev/null";
        int ret_rule = system(rule_cmd.c_str());
        if (ret_rule != 0 && debug) {
            syslog(LOG_DEBUG, "ip rule add returned non-zero (might already exist)");
        }

        // We route default traffic from table 100 to the specified tunnel interface.
        string route_cmd = "ip route replace default dev " + tunnel_interface + " table 100";
        int ret_route = system(route_cmd.c_str());

        if (ret_route == 0) {
            syslog(LOG_NOTICE, "Successfully bound table 100 default route to device %s", tunnel_interface.c_str());
        } else {
            syslog(LOG_ERR, "Failed to execute route command for interface %s", tunnel_interface.c_str());
        }
    } else {
        if (debug) {
            syslog(LOG_DEBUG, "No tunnel interface specified, running in standard mode.");
        }
    }

//-----------
    // Create an nftables context
    nft = nft_ctx_new(NFT_CTX_DEFAULT);
    if (!nft) {
        syslog(LOG_ERR, "[ CRIT ]: Failed to initialize nftables context");
        exit(EXIT_FAILURE);
    }

    // Enabling JSON support
    nft_ctx_output_set_flags(nft, NFT_CTX_OUTPUT_JSON);

    // Parsing CIDR and initializing nftables
    parse_cidr(cidr);
    setup_base_structure();
}

IPManager::~IPManager() {
    if (nft) {
        nft_ctx_free(nft);
    }
}

void IPManager::parse_cidr(const string& cidr) {
    size_t slash = cidr.find('/');
    if (slash == string::npos) {
        syslog(LOG_ERR, "[ ERR ]: Invalid CIDR format: %s", cidr.c_str());
        return;
    }

    // Saving parts to form nftables rules
    base_ip_str = cidr.substr(0, slash);
    prefix_len = stoi(cidr.substr(slash + 1));

    struct in_addr addr;
    if (inet_pton(AF_INET, base_ip_str.c_str(), &addr) <= 0) {
        syslog(LOG_ERR, "[ ERR ]: Invalid IP in CIDR: %s", base_ip_str.c_str());
        return;
    }

    uint32_t start_ip = ntohl(addr.s_addr);
    uint32_t num_hosts = pow(2, 32 - prefix_len);

    // Filling the pool of free addresses (excluding network and broadcast)
    for (uint32_t i = 1; i < num_hosts - 1; ++i) {
        free_ips.push_back(start_ip + i);
    }

    if (debug) {
        syslog(LOG_INFO, "[ INIT ]: Pool initialized for %s. Total available: %zu",
               cidr.c_str(), free_ips.size());
    }
}

bool IPManager::nft_run(const string& json_cmd) {
    int res = nft_run_cmd_from_buffer(nft, json_cmd.c_str());
    return (res == 0);
}

void IPManager::setup_base_structure() {
    // First, synchronize with the kernel (pull up old mappings)
    sync_with_kernel();

    json::array_t commands;

    // 1. Add a table (if there is one, nothing will happen)
    commands.push_back({{"add", {{"table", {{"family", "ip"}, {"name", "dnsmap"}}}}}});

    // 2. Adding a map
    commands.push_back({{"add", {{"map", {
            {"family", "ip"}, {"table", "dnsmap"}, {"name", "fake_to_real"},
            {"type", "ipv4_addr"}, {"map", "ipv4_addr"}
    }}}}});

    // 3. Adding a chain
    commands.push_back({{"add", {{"chain", {
            {"family", "ip"}, {"table", "dnsmap"}, {"name", "PREROUTING"},
            {"type", "nat"}, {"hook", "prerouting"}, {"prio", -100}
    }}}}});

    // Clearing the chain before adding a rule to it
    commands.push_back({{"flush", {{"chain", {
            {"family", "ip"}, {"table", "dnsmap"}, {"name", "PREROUTING"}
    }}}}});
    // ------------------------------------------------

    // 4. Forming a DNAT rule
    json rule_match = {
            {"match", {
                    {"left", {{"payload", {{"protocol", "ip"}, {"field", "daddr"}}}}},
                    {"op", "=="},
                    {"right", {{"prefix", {{"addr", base_ip_str}, {"len", prefix_len}}}}}
            }}
    };

    json rule_action = {
            {"dnat", {
                    {"addr", {{"map", {
                            {"key", {{"payload", {{"protocol", "ip"}, {"field", "daddr"}}}}},
                            {"data", "@fake_to_real"}
                    }}}}
            }}
    };

    commands.push_back({{"add", {{"rule", {
            {"family", "ip"}, {"table", "dnsmap"}, {"chain", "PREROUTING"},
            {"expr", json::array({rule_match, rule_action})}
    }}}}});

    // Send everything in one batch
    json final_json = {{"nftables", commands}};
    if (!nft_run(final_json.dump())) {
        syslog(LOG_ERR, "[ ERR ]: Failed to apply base dnsmap structure");
        return;
    }

    // 5.We create an independent table for early tagging, following the main initialization.
    string mark_table_cmd = "add table ip mangle_dnsmap";
    nft_run_cmd_from_buffer(nft, mark_table_cmd.c_str());

    string mark_chain_cmd = "add chain ip mangle_dnsmap prerouting { type filter hook prerouting priority mangle; policy accept; }";
    nft_run_cmd_from_buffer(nft, mark_chain_cmd.c_str());
    
    nft_run_cmd_from_buffer(nft, "flush chain ip mangle_dnsmap prerouting");

    string mark_rule_cmd = "add rule ip mangle_dnsmap prerouting ip daddr " + base_ip_str + "/" + to_string(prefix_len) + 
                           " meta l4proto { tcp, udp } meta mark set " + fw_mark;
    
    if (nft_run_cmd_from_buffer(nft, mark_rule_cmd.c_str()) != 0) {
        syslog(LOG_ERR, "[ ERR ]: Failed to add early marking rule into mangle_dnsmap");
    } else if (debug) {
        syslog(LOG_INFO, "[ OK  ]: Early marking rule successfully added.");
    }
}

uint32_t IPManager::get_or_create(uint32_t real_ip) {
    std::lock_guard<std::mutex> lock(mtx);
    if (real_to_fake.count(real_ip)) {
        return real_to_fake[real_ip];
    }

    if (free_ips.empty()) {
        syslog(LOG_ERR, "[ ALERT ]: IP Pool is exhausted!");
        return 0;
    }

    uint32_t fake_ip = free_ips.front();
    free_ips.pop_front();
    real_to_fake[real_ip] = fake_ip;

    // Preparing strings for nftables
    char f_s[INET_ADDRSTRLEN], r_s[INET_ADDRSTRLEN];
    struct in_addr f_addr = { .s_addr = htonl(fake_ip) };
    struct in_addr r_addr = { .s_addr = htonl(real_ip) };
    inet_ntop(AF_INET, &f_addr, f_s, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &r_addr, r_s, INET_ADDRSTRLEN);

    // Adding an element to a map via JSON
    json add_element = {
            {"nftables", json::array({
                                             {{"add", {{"element", {
                                                     {"family", "ip"}, {"table", "dnsmap"}, {"name", "fake_to_real"},
                                                     {"elem", json::array({ json::array({f_s, r_s}) })}
                                             }}}}}
                                     })}
    };

    if (nft_run(add_element.dump()) && debug) {
        syslog(LOG_INFO, "[  NEW  ]: Mapping created: %s -> %s", f_s, r_s);
    }

    return fake_ip;
}

void IPManager::sync_with_kernel() {
    // Hiding library errors in an internal buffer
    nft_ctx_buffer_error(nft);

    // Hiding the standard output (list result)
    nft_ctx_buffer_output(nft);
    // Use "list map", this is the most stable way to get elements
    json list_cmd = {
            {"nftables", json::array({
                                             {{"list", {
                                                     {"map", {
                                                             {"family", "ip"},
                                                             {"table", "dnsmap"},
                                                             {"name", "fake_to_real"}
                                                     }}
                                             }}}
                                     })}
    };

    nft_ctx_buffer_output(nft);

    if (nft_run_cmd_from_buffer(nft, list_cmd.dump().c_str()) != 0) {
        // If the table doesn't exist yet (first run), just exit
        nft_ctx_get_error_buffer(nft);
        nft_ctx_get_output_buffer(nft);
        nft_ctx_unbuffer_output(nft);
        return;
    }

    const char* output = nft_ctx_get_output_buffer(nft);
    if (!output || strlen(output) == 0) {
        nft_ctx_unbuffer_output(nft);
        return;
    }

    try {
        auto j_out = json::parse(output);
        if (j_out.contains("nftables")) {
            for (auto& item : j_out["nftables"]) {
                // Looking for the "map" object that contains "elem"
                if (item.contains("map") && item["map"].contains("elem")) {
                    for (auto& elem : item["map"]["elem"]) {
                        // Format elem в nftables: [key, value]
                        // But sometimes these are objects, so we take them carefully:
                        string f_s, r_s;

                        if (elem.is_array() && elem.size() >= 2) {
                            f_s = elem[0].get<string>();
                            r_s = elem[1].get<string>();
                        } else {
                            continue;
                        }

                        uint32_t f_ip = ntohl(inet_addr(f_s.c_str()));
                        uint32_t r_ip = ntohl(inet_addr(r_s.c_str()));

                        // Synchronizing the internal map
                        real_to_fake[r_ip] = f_ip;

                        // Remove IP addresses from the free list (use remove+erase)
                        free_ips.erase(
                                std::remove(free_ips.begin(), free_ips.end(), f_ip),
                                free_ips.end()
                        );
                    }
                }
            }
        }
        if (!real_to_fake.empty()) {
            syslog(LOG_INFO, "[ SYNC ]: Recovered %zu mappings from kernel", real_to_fake.size());
        }
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "[ ERR ]: Sync parse error: %s", e.what());
    }

    nft_ctx_unbuffer_output(nft);
}
