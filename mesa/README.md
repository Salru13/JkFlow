# Mesa PS4 window-system patch

Mesa / RADV is MIT-licensed (`LICENSES/Mesa-MIT.txt`). Base: Mesa 26.3.0-devel
(commit `1a59cc14`) with the Mesa changes of the upstream PS4 port, as
shipped in the SDK bundle `orbis-sdk-v1` (see `wine/UPSTREAM.md`).

| File | What |
|---|---|
| `patches/wsi-async-present.patch` | `src/vulkan/wsi/wsi_orbis.c`, `wsi_common_headless.c` and the new `wsi_orbis_present_queue.h`: the display flip rate chosen by the title (60/30/20 Hz) is kept across re-attach, and the present is handed to one worker thread per swapchain instead of waiting on the submit thread. `ORBIS_WSI_SYNC_PRESENT=1` keeps the synchronous present. The patch includes the earlier flip-rate change. |

What else is in the shipped `libvulkan.prx` and is not published here as a
patch (Mesa is MIT, which does not require source):

- the Mesa changes of the upstream PS4 port (above);
- in its PS4 driver wrapper and DRM backend, the JkFlow 1.00 process name,
  the driver log at error level in the title's logs folder, and the developer
  switch paths renamed;
- the post-link path cleans paths6b and paths7 (`wine/BUILD.md`).
