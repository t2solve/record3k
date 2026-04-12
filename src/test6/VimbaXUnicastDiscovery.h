#pragma once
// Forwarding header — the canonical implementation lives in liblbt.
#include <liblbt/vimbax_unicast_discovery.h>

// Expose lbt types in global scope for backwards compatibility with test6 code.
using lbt::GevDiscoveryResult;
using lbt::VimbaXUnicastDiscovery;
