# Mesa PS4 window-system patches

Mesa / RADV is MIT-licensed (`LICENSES/Mesa-MIT.txt`). Base: Mesa 26.3.0-devel
with the PS4 (Orbis) window-system port of the upstream Wine console-port trees
listed in `SOURCES.txt`.

| File | What |
|---|---|
| `patches/wsi-async-present.patch` | `src/vulkan/wsi/wsi_orbis.c`, `wsi_common_headless.c` and the new `wsi_orbis_present_queue.h`: the display flip rate chosen by the title (60/30/20 Hz) is kept across re-attach, and the present is handed to one worker thread per swapchain instead of waiting on the submit thread. `ORBIS_WSI_SYNC_PRESENT=1` keeps the synchronous present. The patch includes the earlier flip-rate change. |

The remaining Mesa changes of the shipped `libvulkan.prx` will be added
together with the Wine PS4 runtime source (see `wine/README.md`).
