# Set a static (persistent) IP on a GigE camera

Uses the `ConfigIp_VmbC` example from the VimbaX SDK. Arguments: camera ID, IP, subnet mask, gateway.

```bash
./ConfigIp_VmbC DEV_000A4708ED3F 172.18.227.210 255.255.0.0 172.18.227.254
```

Restart (power-cycle) the camera afterwards so it comes up with the new IP.

Expected output:

```
/////////////////////////////////////////
/// VmbC API IP Configuration Example ///
/////////////////////////////////////////

Vmb Version Major: 1 Minor: 0 Patch: 7

Opened camera
Setting persistent IP using sfnc features
IP configuration written to camera
```
