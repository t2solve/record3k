#pragma once

// vimbax_unicast_discovery.h — pure C++ unicast GVCP DISCOVERY_CMD probe.
//
// Sends a GigE Vision DISCOVERY_CMD packet (UDP port 3956) directly to a
// camera IP and waits for a DISCOVERY_ACK.  No VimbaX SDK or broadcast
// required — works even on managed networks where multicast/broadcast is
// blocked (VPN, managed switch).
//
// Based on GEV spec §16.2.

#include <string>

namespace lbt {

// Result of a single unicast GVCP probe.
struct GevDiscoveryResult {
    bool        responded  = false;
    std::string fromIp;       // source IP of the DISCOVERY_ACK
    std::string deviceIp;     // IP as reported by the device
    std::string deviceMac;    // MAC as reported by the device (XX:XX:XX:XX:XX:XX)
    std::string subnet;       // subnet mask reported by the device
    std::string gateway;      // gateway reported by the device
};

class VimbaXUnicastDiscovery {
public:
    // Send a GVCP DISCOVERY_CMD to targetIp:3956 and wait up to timeoutSec
    // seconds for a DISCOVERY_ACK.  Returns a GevDiscoveryResult whose
    // 'responded' field is true if the camera replied.
    static GevDiscoveryResult probe(const std::string& targetIp,
                                    double timeoutSec = 2.0);
};

} // namespace lbt
