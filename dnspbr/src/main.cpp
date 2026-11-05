#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <csignal>
#include <sys/stat.h>
#include <getopt.h>
#include <cerrno>

#include "ip_manager.hpp"
#include "dns_processor.hpp"

using namespace std;

#ifndef VERSION
#define VERSION "1.1.3"
#endif

struct DNSJob {
    struct sockaddr_in client_addr;
    vector<uint8_t> data;
};

queue<DNSJob> job_queue;
mutex queue_mutex;
condition_variable queue_cv;
bool keep_running = true;

void worker_thread(int server_sock, struct sockaddr_in upstream_addr, IPManager& ip_manager, bool debug) {
    while (true) {
        DNSJob job;
        {
            unique_lock<mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !job_queue.empty() || !keep_running; });
            if (!keep_running && job_queue.empty()) return;
            job = std::move(job_queue.front());
            job_queue.pop();
        }

        int up_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (up_sock < 0) continue;

        struct timeval tv = {2, 0};
        setsockopt(up_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sendto(up_sock, job.data.data(), job.data.size(), 0, (struct sockaddr*)&upstream_addr, sizeof(upstream_addr));
        
        uint8_t res_buf[4096];
        ssize_t rn = recv(up_sock, res_buf, sizeof(res_buf), 0);
        close(up_sock);

        if (rn > 0) {
            uint8_t* out_pkt = nullptr;
            size_t out_len = 0;
            process_packet(out_pkt, out_len, res_buf, rn, ip_manager, debug);

            if (out_pkt) {
                sendto(server_sock, out_pkt, out_len, 0, (struct sockaddr*)&job.client_addr, sizeof(job.client_addr));
                free(out_pkt);
            } else {
                sendto(server_sock, res_buf, rn, 0, (struct sockaddr*)&job.client_addr, sizeof(job.client_addr));
            }
        }
    }
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        syslog(LOG_NOTICE, "Shutdown signal received...");
        {
            lock_guard<mutex> lock(queue_mutex);
            keep_running = false;
        }
        queue_cv.notify_all();
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    umask(0);
    if (chdir("/") < 0) exit(EXIT_FAILURE);
    for (long x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }
}

void print_usage(char* prog_name) {
    cout << "Usage: " << prog_name << " [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -a, --listen <addr>        Address to listen on (default: 0.0.0.0)" << endl;
    cout << "  -p, --port <port>          Port to listen on (default: 53)" << endl;
    cout << "  -u, --upstream <ip>        Upstream DNS server (default: 9.9.9.10)" << endl;
    cout << "  -P, --upstream_port <port> Upstream DNS port (default: 53)" << endl;
    cout << "  -r, --range <cidr>         Fake IP range (default: 10.64.0.0/15)" << endl;
    cout << "  -m, --mark <mark>          Fwmark for matched traffic (default: 0x40)" << endl;
    cout << "  -i, --interface <name>     Tunnel interface for auto-IP and routing (optional)" << endl;
    cout << "  -w, --workers <num>        Number of worker threads (default: 4)" << endl;
    cout << "  -d, --daemonize            Run as daemon (background)" << endl;
    cout << "  -v, --verbose              Enable verbose debug logging" << endl;
    cout << "  -V, --version              Show version information" << endl;
    cout << "  -h, --help                 Show this help message" << endl;
}

int main(int argc, char** argv) {
    string listen_ip = "0.0.0.0";
    int port = 53;
    string upstream_ip = "9.9.9.10";
    int upstream_port = 53;
    string range = "10.64.0.0/15";
    string fw_mark = "0x40";
    string interface_name = "";
    int num_workers = 2; // default value 2
    bool debug_mode = false;
    bool should_daemonize = false;

    static struct option long_options[] = {
        {"listen",    required_argument, 0, 'a'},
        {"port",      required_argument, 0, 'p'},
        {"upstream",  required_argument, 0, 'u'},
        {"upstream_port", required_argument, 0, 'P'},
        {"range",     required_argument, 0, 'r'},
        {"mark",      required_argument, 0, 'm'},
        {"interface",     required_argument, 0, 'i'},
        {"workers",   required_argument, 0, 'w'},
        {"daemonize", no_argument,       0, 'd'},
        {"verbose",   no_argument,       0, 'v'},
        {"version",   no_argument,       0, 'V'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "a:p:u:P:r:m:i:w:dvhV", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a': listen_ip = optarg; break;
            case 'p': port = stoi(optarg); break;
            case 'u': upstream_ip = optarg; break;
            case 'P': upstream_port = stoi(optarg); break;
            case 'r': range = optarg; break;
            case 'm': fw_mark = optarg; break;
            case 'i': interface_name = optarg; break;
            case 'w': num_workers = stoi(optarg); break;
            case 'd': should_daemonize = true; break;
            case 'v': debug_mode = true; break;
            case 'V': cout << "DNSMap version " << VERSION << endl; return 0;
            case 'h': default: print_usage(argv[0]); return 0;
        }
    }

    if (num_workers < 1) num_workers = 1;

    if (std::getenv("INVOCATION_ID") != nullptr) {
        openlog("dnsmap", LOG_PID, LOG_DAEMON);
    } else if (should_daemonize) {
        daemonize();
        openlog("dnsmap", LOG_PID, LOG_DAEMON);
    } else {
        openlog("dnsmap", LOG_PID | LOG_PERROR, LOG_USER);
    }

    setlogmask(LOG_UPTO(debug_mode ? LOG_DEBUG : LOG_NOTICE));
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    try {
        IPManager ip_manager(range, fw_mark, interface_name, debug_mode);
        int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (server_sock < 0) return 1;

        int optval = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        struct timeval tv_timeout = {1, 0};
        setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv_timeout, sizeof(tv_timeout));

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, listen_ip.c_str(), &addr.sin_addr);

        if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            syslog(LOG_ERR, "Bind failed: %m");
            return 1;
        }

        struct sockaddr_in upstream_addr = {};
        upstream_addr.sin_family = AF_INET;
        upstream_addr.sin_port = htons(upstream_port);
        inet_pton(AF_INET, upstream_ip.c_str(), &upstream_addr.sin_addr);

        vector<thread> workers;
        for (int i = 0; i < num_workers; ++i) {
            workers.emplace_back(worker_thread, server_sock, upstream_addr, std::ref(ip_manager), debug_mode);
        }

        syslog(LOG_NOTICE, "Started DNSMap %s (Workers: %d, Range: %s)", VERSION, num_workers, range.c_str());

        while (keep_running) {
            uint8_t buffer[4096];
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            ssize_t n = recvfrom(server_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }

            {
                lock_guard<mutex> lock(queue_mutex);
                job_queue.push({client_addr, vector<uint8_t>(buffer, buffer + n)});
            }
            queue_cv.notify_one();
        }

        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }

        close(server_sock);
        syslog(LOG_NOTICE, "Stopped gracefully");

    } catch (const exception& e) {
        syslog(LOG_CRIT, "Fatal error: %s", e.what());
        closelog();
        return 1;
    }

    closelog();
    return 0;
}
