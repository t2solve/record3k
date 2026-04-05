# VimbaX – IP-Based Network Camera Connection: API Call Reference

Scanned from all C (`VmbC`) and C++ (`VmbCPP`) examples in `VimbaX_2025-1/api/examples`.

---

## VmbC (C API)

### 1. Startup / Shutdown

| Call | Where |
|---|---|
| `VmbStartup(NULL)` | ActionCommands/Helper.c, ForceIpProg, ConfigIpProg |
| `VmbShutdown()` | All `*Prog.c` mains |

---

### 2. Enumerate / Discover Cameras and Infrastructure

| Call | Feature / Note | Example |
|---|---|---|
| `VmbCamerasList()` (via `ListCameras()`) | Forces TL to update internal list; prerequisite before ForceIP | ForceIp, ConfigIp, ActionCommands |
| `VmbInterfacesList(buf, count, &count, sizeof(VmbInterfaceInfo_t))` | Lists all network interfaces | ForceIp, UnicastDiscover |
| `VmbTransportLayersList(buf, count, &count, sizeof(...))` | Lists all transport layers (used to find AVT GigE TL) | ForceIp |
| `VmbFeatureIntRangeQuery(ifaceHandle, "DeviceSelector", &min, &max)` | Count cameras on a GEV interface | ForceIp, UnicastDiscover |
| `VmbFeatureIntSet(ifaceHandle, "DeviceSelector", idx)` | Select a specific camera slot on interface | ForceIp, UnicastDiscover |
| `VmbFeatureIntGet(ifaceHandle, "GevDeviceMACAddress", &mac)` | Read MAC of currently selected device | ForceIp |
| `VmbFeatureIntGet(ifaceHandle, "GevDeviceIPAddress", &ip)` | Read IP of currently selected device | ForceIp, UnicastDiscover |
| `VmbFeatureStringGet(ifaceHandle, "DeviceID", buf, len, &len)` | Read GenTL camera-ID string | ForceIp, UnicastDiscover |
| `VmbFeatureEnumGet(ifaceHandle, "InterfaceType", &value)` | Verify interface is `"GigEVision"` / `"GEV"` | ActionCommands/Helper |
| `VmbFeatureStringGet(tlHandle, "TLVendorName", buf, size, &n)` | Verify Allied Vision GigE TL | ActionCommands/Helper |
| `VmbFeatureIntGet(tlHandle, "InterfaceCount", &n)` | Count interfaces per transport layer | ForceIp |

---

### 3. Unicast Injection (VimbaX 2024-1+)

> Requires `DeviceIPAddress` writable feature (Allied Vision extension in `VimbaGigETL.cti` r1.5+).
> Set `DefaultDeviceDiscovery=Off` in `<install>/cti/VimbaGigETL.xml` to suppress the automatic broadcast scan.

| Call | Feature / Note | Example |
|---|---|---|
| `VmbFeatureIntGet(ifaceHandle, "GevInterfaceIPAddress", &ip)` | Filter by host NIC IP | UnicastDiscover |
| `VmbFeatureIntSet(ifaceHandle, "DeviceIPAddress", cameraIpHostOrder)` | **AVT extension** – write unicast target before scan | UnicastDiscover |
| `VmbFeatureCommandRun(ifaceHandle, "DeviceUpdateList")` | Sends unicast GVCP DISCOVERY_CMD to that IP | UnicastDiscover |
| `VmbFeatureCommandIsDone(ifaceHandle, "DeviceUpdateList", &done)` | Poll until scan completes | UnicastDiscover |

> **Fallback** (older TL, no writable `DeviceIPAddress`): pass the bare IP string directly to `VmbCameraOpen()` –
> the GigE TL sends an internal unicast discovery automatically.

---

### 4. Camera Info & Access Check

| Call | Note | Example |
|---|---|---|
| `VmbCameraInfoQuery(cameraId, &info, sizeof(info))` | Check `permittedAccess & VmbAccessModeFull` before opening | ConfigIp, ForceIp, ActionCommands |
| `VmbCameraInfoQueryByHandle(handle, &info, sizeof(info))` | Same via an already-open handle | ActionCommands/ImageAcquisition |

---

### 5. Open / Close Camera

| Call | Note | Example |
|---|---|---|
| `VmbCameraOpen(cameraId, VmbAccessModeFull, &handle)` | `cameraId` can be the camera's IP string for direct unicast | All examples |
| `VmbCameraClose(handle)` | | All examples |

---

### 6. ForceIP – Temporary IP Override (Lost on Power-Cycle)

Applied on **interface handle** (preferred) or **transport layer handle**.

#### Via Interface Handle

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureIntSet(ifaceHandle, "GevDeviceForceIPAddress", ip)` | Set target IP | ForceIp |
| `VmbFeatureIntSet(ifaceHandle, "GevDeviceForceSubnetMask", mask)` | Set target subnet | ForceIp |
| `VmbFeatureIntSet(ifaceHandle, "GevDeviceForceGateway", gw)` | Set target gateway | ForceIp |
| `VmbFeatureCommandRun(ifaceHandle, "GevDeviceForceIP")` | Execute ForceIP | ForceIp |
| `VmbFeatureCommandIsDone(ifaceHandle, "GevDeviceForceIP", &done)` | Wait for completion | ForceIp |

#### Via Transport Layer Handle

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureIntSet(tlHandle, "GevDeviceForceMACAddress", mac)` | Identify camera by MAC first | ForceIp |
| `VmbFeatureIntSet(tlHandle, "GevDeviceForceIPAddress", ip)` | Set target IP | ForceIp |
| `VmbFeatureIntSet(tlHandle, "GevDeviceForceSubnetMask", mask)` | Set target subnet | ForceIp |
| `VmbFeatureIntSet(tlHandle, "GevDeviceForceGateway", gw)` | Set target gateway | ForceIp |
| `VmbFeatureCommandRun(tlHandle, "GevDeviceForceIP")` | Execute ForceIP | ForceIp |
| `VmbFeatureCommandIsDone(tlHandle, "GevDeviceForceIP", &done)` | Wait for completion | ForceIp |

---

### 7. ConfigIP – Persistent IP Configuration (Survives Power-Cycle)

Applied on an **open camera handle**.

#### SFNC-based (preferred)

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureAccessQuery(camHandle, "GevCurrentIPConfigurationPersistentIP", NULL, &writable)` | Check SFNC feature availability | ConfigIp |
| `VmbFeatureIntSet(camHandle, "GevPersistentIPAddress", ip)` | Write static IP | ConfigIp |
| `VmbFeatureIntSet(camHandle, "GevPersistentSubnetMask", mask)` | Write subnet mask | ConfigIp |
| `VmbFeatureIntSet(camHandle, "GevPersistentDefaultGateway", gw)` | Write gateway | ConfigIp |
| `VmbFeatureBoolSet(camHandle, "GevCurrentIPConfigurationPersistentIP", VmbBoolTrue)` | Activate static IP | ConfigIp |
| `VmbFeatureBoolSet(camHandle, "GevCurrentIPConfigurationDHCP", VmbBoolTrue)` | Activate DHCP | ConfigIp |
| `VmbFeatureAccessQuery(camHandle, "GevCurrentIPConfigurationLLA", NULL, &writable)` | Check LLA SFNC support | ConfigIp |
| `VmbFeatureBoolSet(camHandle, "GevCurrentIPConfigurationLLA", VmbBoolTrue)` | Activate Link-Local (LLA) | ConfigIp |

#### Register-based fallback (no SFNC features)

GEV bootstrap registers used directly:

| Register address | Purpose |
|---|---|
| `0x0014` | Network Interface Configuration (read/write config bits) |
| `0x064C` | Persistent IP Address |
| `0x065C` | Persistent Subnet Mask |
| `0x066C` | Persistent Default Gateway |

| Call | Note | Example |
|---|---|---|
| `VmbMemoryRead(camHandle, address, size, buf, &bytesRead)` | Read GEV bootstrap register | ConfigIp |
| `VmbMemoryWrite(camHandle, address, size, buf, &bytesWritten)` | Write GEV bootstrap register | ConfigIp |

---

### 8. GigE Packet-Size Negotiation (After Open, Before Capture)

Applied on the **stream handle** from `camInfo.streamHandles[0]`.

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureCommandRun(streamHandle, "GVSPAdjustPacketSize")` | Auto-negotiate largest MTU | ActionCommands, UnicastDiscover |
| `VmbFeatureCommandIsDone(streamHandle, "GVSPAdjustPacketSize", &done)` | Poll until done | ActionCommands, UnicastDiscover |
| `VmbFeatureIntGet(streamHandle, "GVSPPacketSize", &size)` | Read negotiated packet size | ActionCommands, UnicastDiscover |

---

### 9. Action Commands – GEV Broadcast / Unicast Trigger to Multiple Cameras

Applied on **interface handle**.

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureIntGet(localDeviceHandle, "GevDeviceIPAddress", &ip)` | Read camera IP for unicast destination | ActionCommands |
| `VmbFeatureIntSet(ifaceHandle, "ActionDeviceKey", key)` | Set device key | ActionCommands |
| `VmbFeatureIntSet(ifaceHandle, "ActionGroupKey", key)` | Set group key | ActionCommands |
| `VmbFeatureIntSet(ifaceHandle, "ActionGroupMask", mask)` | Set group mask | ActionCommands |
| `VmbFeatureIntSet(ifaceHandle, "GevActionDestinationIPAddress", ip)` | `0` = broadcast, camera IP = unicast | ActionCommands |
| `VmbFeatureCommandRun(ifaceHandle, "ActionCommand")` | Fire the GEV action packet | ActionCommands |
| `VmbFeatureCommandIsDone(ifaceHandle, "ActionCommand", &done)` | Poll completion | ActionCommands |

Camera-side setup (on open camera handle) before receiving action commands:

| Call | Feature | Example |
|---|---|---|
| `VmbFeatureEnumSet(camHandle, "TriggerSelector", "FrameStart")` | Select trigger | ActionCommands |
| `VmbFeatureEnumSet(camHandle, "TriggerSource", "Action0")` | Set source to Action0 | ActionCommands |
| `VmbFeatureEnumSet(camHandle, "TriggerMode", "On")` | Enable trigger | ActionCommands |

---

## VmbCPP (C++ API)

| Call | Note | Example |
|---|---|---|
| `VmbSystem::GetInstance()` | Get API singleton | SynchronousGrab |
| `system.Startup()` | Initialize API | SynchronousGrab |
| `system.GetCameras(cameras)` | List all discovered cameras | SynchronousGrab |
| `system.GetCameraByID(cameraId, camera)` | Open by camera-ID **or bare IP string** | SynchronousGrab |
| `camera->Open(VmbAccessModeFull)` | Open camera for full access | SynchronousGrab |
| `camera->GetName(name)` | Read camera name | SynchronousGrab |
| `camera->GetStreams(streams)` | Get stream list for GigE tuning | SynchronousGrab |
| `streams[0]->GetFeatureByName("GVSPAdjustPacketSize", feature)` | Get packet-size feature | SynchronousGrab |
| `feature->RunCommand()` | Negotiate MTU | SynchronousGrab |
| `feature->IsCommandDone(done)` | Poll until negotiation is done | SynchronousGrab |
| `system.Shutdown()` | Cleanup / release resources | SynchronousGrab |

---

## Connection Strategy Summary

| Strategy | Key API calls | Source file |
|---|---|---|
| **Normal broadcast enumeration** | `VmbStartup → VmbCamerasList → VmbCameraOpen` | All examples |
| **Unicast injection** (targeted, no broadcast) | `VmbInterfacesList → DeviceIPAddress (write) → DeviceUpdateList → VmbCameraOpen` | `VmbC/UnicastDiscover/UnicastDiscover.c` |
| **Direct open by IP string** (older TL fallback) | `VmbCameraOpen("192.168.x.x", VmbAccessModeFull, &handle)` | `VmbC/UnicastDiscover/UnicastDiscover.c` |
| **ForceIP via interface handle** | `DeviceSelector + GevDeviceForceIP*` features on interface handle | `VmbC/ForceIp/ForceIp.c` |
| **ForceIP via transport layer handle** | `GevDeviceForceMACAddress + GevDeviceForceIP*` on TL handle | `VmbC/ForceIp/ForceIp.c` |
| **ConfigIP – persistent / DHCP / LLA** | SFNC features or `VmbMemoryRead/Write` on camera registers | `VmbC/ConfigIp/ConfigIp.c` |
| **GigE packet-size negotiation** | `GVSPAdjustPacketSize` + `GVSPPacketSize` on stream handle | All GigE examples |
| **Action Commands** (broadcast or unicast trigger) | `ActionCommand` feature on interface handle | `VmbC/ActionCommands/ActionCommands.c` |
