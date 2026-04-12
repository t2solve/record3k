// vimbax_unicast_discovery.cpp — implementation of unicast GVCP pre-check.
//
// GVCP DISCOVERY_CMD layout (8 bytes, big-endian, GEV spec §16.2):
//   byte 0   : 0x42  (magic / key character)
//   byte 1   : 0x01  (flags — bit-0 = allow_broadcast_ack)
//   byte 2-3 : 0x0002 (DISCOVERY_CMD)
//   byte 4-5 : 0x0000 (payload length = 0)
//   byte 6-7 : request ID
//
// DISCOVERY_ACK packet offsets (0-based from start of received UDP payload):
//   0-1  : status  (0x0000 = GEV_STATUS_SUCCESS)
//   2-3  : cmd     (0x0003 = DISCOVERY_ACK)
//   4-5  : length
//   6-7  : req_id
//   payload (+8 bytes):
//     +0   : SpecVersion (4 bytes)
//     +4   : DevMode     (4 bytes)
//     +8   : MAC[8]      — 2-byte pad + 6-byte MAC  → absolute offset 18
//     +24  : CurIP[16]   — 12-byte pad + 4-byte IPv4 → absolute offset 44
//     +40  : SubMask[16] — 12-byte pad + 4-byte mask → absolute offset 60
//     +56  : Gateway[16] — 12-byte pad + 4-byte gw   → absolute offset 76

#include <liblbt/vimbax_unicast_discovery.h>

#include <iostream>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

namespace lbt {

GevDiscoveryResult VimbaXUnicastDiscovery::probe(const std::string& targetIp,
                                                  double timeoutSec)
{
    GevDiscoveryResult result;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "    [gev-probe] socket(): " << std::strerror(errno) << "\n";
        return result;
    }

    // Some older cameras reply with a broadcast ACK regardless of the flags field
    int on = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    struct sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = 0;
    local.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
        std::cerr << "    [gev-probe] bind(): " << std::strerror(errno) << "\n";
        ::close(sock);
        return result;
    }

    // Build DISCOVERY_CMD packet
    const uint16_t reqId = 0xAB42;
    uint8_t pkt[8];
    pkt[0] = 0x42;
    pkt[1] = 0x01;
    pkt[2] = 0x00; pkt[3] = 0x02;   // DISCOVERY_CMD
    pkt[4] = 0x00; pkt[5] = 0x00;   // payload length = 0
    pkt[6] = static_cast<uint8_t>(reqId >> 8);
    pkt[7] = static_cast<uint8_t>(reqId & 0xFF);

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(3956);   // GVCP UDP port (GEV spec fixed)
    if (::inet_pton(AF_INET, targetIp.c_str(), &dest.sin_addr) != 1) {
        std::cerr << "    [gev-probe] Invalid IP: " << targetIp << "\n";
        ::close(sock);
        return result;
    }

    if (::sendto(sock, pkt, sizeof(pkt), 0,
                 reinterpret_cast<const struct sockaddr*>(&dest), sizeof(dest)) < 0) {
        std::cerr << "    [gev-probe] sendto(): " << std::strerror(errno) << "\n";
        ::close(sock);
        return result;
    }

    // Compute absolute deadline using CLOCK_MONOTONIC
    struct timespec deadline{};
    ::clock_gettime(CLOCK_MONOTONIC, &deadline);
    const long addNs = static_cast<long>(timeoutSec * 1'000'000'000L);
    deadline.tv_sec  += addNs / 1'000'000'000L;
    deadline.tv_nsec += addNs % 1'000'000'000L;
    if (deadline.tv_nsec >= 1'000'000'000L) {
        deadline.tv_sec  += 1;
        deadline.tv_nsec -= 1'000'000'000L;
    }

    // Loop: receive packets until we get a DISCOVERY_ACK or time out
    while (true) {
        struct timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        const long remNs = (deadline.tv_sec  - now.tv_sec ) * 1'000'000'000L
                         + (deadline.tv_nsec - now.tv_nsec);
        if (remNs <= 0) break;

        struct timeval tv{};
        tv.tv_sec  = remNs / 1'000'000'000L;
        tv.tv_usec = (remNs % 1'000'000'000L) / 1'000L;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        if (::select(sock + 1, &rfds, nullptr, nullptr, &tv) <= 0) break;

        uint8_t buf[512]{};
        struct sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const ssize_t n = ::recvfrom(sock, buf, sizeof(buf), 0,
                                     reinterpret_cast<struct sockaddr*>(&from), &fromLen);
        if (n < 8) continue;

        // bytes 2-3 hold the ACK command code; 0x0003 = DISCOVERY_ACK
        const uint16_t ackCmd = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
        if (ackCmd != 0x0003) continue;   // stray packet (e.g. READREG_ACK) — keep waiting

        result.responded = true;

        char tmp[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &from.sin_addr, tmp, sizeof(tmp));
        result.fromIp = tmp;

        if (n >= 48) {
            ::inet_ntop(AF_INET, &buf[44], tmp, sizeof(tmp));
            result.deviceIp = tmp;
        }
        if (n >= 64) {
            ::inet_ntop(AF_INET, &buf[60], tmp, sizeof(tmp));
            result.subnet = tmp;
        }
        if (n >= 80) {
            ::inet_ntop(AF_INET, &buf[76], tmp, sizeof(tmp));
            result.gateway = tmp;
        }
        if (n >= 24) {
            char macStr[32]{};
            std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                          buf[18], buf[19], buf[20], buf[21], buf[22], buf[23]);
            result.deviceMac = macStr;
        }
        break;
    }

    ::close(sock);
    return result;
}

} // namespace lbt
