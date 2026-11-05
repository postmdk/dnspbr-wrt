#pragma once
#include <ldns/ldns.h>
#include "ip_manager.hpp"

/**
 * Processes incoming DNS packets, performs filtering (IPv6/HTTPS blocking)
 * and IP address spoofing via IPManager.
 */
void process_packet(uint8_t*& out_buf, size_t& out_len, uint8_t* in_buf, size_t in_len, IPManager& manager, bool debug_mode);
