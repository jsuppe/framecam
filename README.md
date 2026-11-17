# framecam

An OS virtual camera fed by another process.

framecam **creates the camera and owns the buffers**. It registers a Windows
camera device and allocates a [framelink](../framelink) ring; your application
attaches, is handed GPU buffers, renders into them and submits. Your
application needs to know nothing about cameras, Media Foundation or
DirectShow.

```
framecam --mf myapp 1280 720 30
```

```c
// your application, in a different process
flChannel* ch;
flOpenProducer("myapp.scene", &ch);   // FL_NOT_FOUND until framecam is running
for (;;) {
    flBuffer b;
    if (flAcquireBuffer(ch, &b, 100) != FL_OK) continue;
    draw_into(flImageD3D11(b.image)); // or import the shared handle into
    flSubmit(ch, &b, -1);             // D3D12 / Vulkan / CUDA
}
```

Anything that can render into a texture becomes a webcam — a game, a
compositor, a visualiser, a dashboard.

## Two tiers

| | reaches | needs |
|---|---|---|
| DirectShow softcam (default) | Chrome, OBS, Zoom, classic Teams | `regsvr32 softcam.dll` |
| `--mf` MFCreateVirtualCamera | MF/UWP apps *and* DirectShow (Windows bridges it) | `regsvr32 vkrender_vcam_source.dll` |

The Windows **Camera app is UWP** and only ever sees the `--mf` tier — a
DirectShow-only camera will not appear there no matter how healthy it is.

Both registrations are one-time and elevated.

## Persistence needs elevation

Unelevated, `MFCreateVirtualCamera(System, AllUsers)` *creates* but `Start()`
returns `E_ACCESSDENIED`, so framecam falls back to **session scope**: the
camera exists only while the process runs and only for the current user. A
persistent, machine-wide camera visible to other users and to session-0
consumers requires running elevated.

## Verifying it

```
ffmpeg -f dshow -list_devices true -i dummy
ffmpeg -f dshow -i video="vkrender Camera (Windows Virtual Camera)" -t 2 -update 1 -y out.png
```

Frames arriving at 30 fps with real content is the check. Note that the camera
appears to be **single-consumer**: while the Windows Camera app held the device,
this capture returned zero frames and an I/O error, and worked immediately once
the app closed.

## Building

```
cmake -S . -B build          # expects ../framelink, or -DFRAMELINK_DIR=<path>
cmake --build build --config Release
```

framecam is not pinned to a framelink revision. Build it against the revision the
renderer feeding it uses: framelink refuses a peer with a different `FL_VERSION`.
For vkrender that is the pin in vkrender's `.github/workflows/build.yml`
(currently `c6d7db7`, `FL_VERSION` 6). vkrender also builds framecam itself when a
framecam checkout sits beside it, which is how its appliance installer gets it.

## Names you must not change casually

The MF source DLL name, its CLSID, and the shared-memory names in
`src/vcam_shared.h` are unchanged from when this lived inside vkrender. They are
baked into an already-registered COM server; renaming any of them silently
breaks every installed camera until someone re-registers with elevation.

## Status

Windows only. `vcam_linux.cpp` over **v4l2loopback** is the planned next backend;
framelink's Linux dma-buf backend it was waiting for now exists, but no work has
started.

MIT. Vendors [softcam](third_party/softcam) (MIT) and
[VCamSample](third_party/VCamSample) (MIT); both keep their own LICENSE files.
