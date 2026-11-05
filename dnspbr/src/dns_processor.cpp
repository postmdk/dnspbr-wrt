#include "dns_processor.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <syslog.h>

using namespace std;

void process_packet(uint8_t*& out_buf, size_t& out_len, uint8_t* in_buf, size_t in_len, IPManager& manager, bool debug) {
    ldns_pkt *pkt;
    // Converting a raw packet from the network into an ldns structure
    if (ldns_wire2pkt(&pkt, in_buf, in_len) != LDNS_STATUS_OK) return;

    ldns_rr_list *questions = ldns_pkt_question(pkt);
    if (questions && ldns_rr_list_rr_count(questions) > 0) {
        ldns_rr *q = ldns_rr_list_rr(questions, 0);
        ldns_rr_type type = ldns_rr_get_type(q);

	// Block IPv6 (AAAA) and HTTPS (SVCB) to force IPv4 usage
	if (type == LDNS_RR_TYPE_AAAA || type == LDNS_RR_TYPE_HTTPS || type == LDNS_RR_TYPE_SVCB) {

	 if (debug) {
          syslog(LOG_DEBUG, "[ DEBUG ]: Blocking query type: %d", type);
         }
          ldns_pkt_set_qr(pkt, true);
          ldns_pkt_set_rcode(pkt, LDNS_RCODE_NOERROR);

          ldns_rr_list *empty = ldns_rr_list_new();
          ldns_pkt_set_answer(pkt, empty);

          ldns_pkt_set_ancount(pkt, 0);
          ldns_pkt_set_nscount(pkt, 0);
          ldns_pkt_set_arcount(pkt, 0);

          ldns_pkt2wire(&out_buf, pkt, &out_len);
          ldns_pkt_free(pkt);
          return;
        }
        if (type == LDNS_RR_TYPE_A) {
            char* qname = ldns_rdf2str(ldns_rr_owner(q));
            ldns_rr_list *answers = ldns_pkt_answer(pkt);

            if (answers) {
                for (size_t i = 0; i < ldns_rr_list_rr_count(answers); i++) {
                    ldns_rr *rr = ldns_rr_list_rr(answers, i);

                    if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_A) {
                        ldns_rdf *data = ldns_rr_a_address(rr);

                        // Safely extract IP address
                        if (ldns_rdf_size(data) == 4) {
                            uint32_t real_ip;
                            memcpy(&real_ip, ldns_rdf_data(data), 4);
                            real_ip = ntohl(real_ip);

                            // Calling IPManager (via nftables API)
                            uint32_t fake_ip = manager.get_or_create(real_ip);

                            if (fake_ip) {
                                struct in_addr f_addr;
                                f_addr.s_addr = htonl(fake_ip);

                                if (debug) {
                                    syslog(LOG_DEBUG, "[  MAP  ]: %s -> %s (API)", qname, inet_ntoa(f_addr));
                                }

                                // Replacing a real IP with a fake one in the DNS response
                                uint32_t fake_net = htonl(fake_ip);
                                ldns_rdf *new_rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_A, 4, &fake_net);

                                // Let's release the old RDF and install the new one
                                ldns_rdf_deep_free(ldns_rr_set_rdf(rr, new_rdf, 0));
                                // Reduce the TTL so that the mapping is not cached for too long
                                ldns_rr_set_ttl(rr, 60); 
                            }
                        }
                    }
                }
            }
            free(qname);
        }
    }

    // Assemble the modified package back into the "wire" format
    ldns_pkt2wire(&out_buf, pkt, &out_len);
    ldns_pkt_free(pkt);
}
