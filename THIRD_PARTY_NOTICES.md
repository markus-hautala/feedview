# Third-party notices

FeedView is built with the following components.

| Component | Used for | Licence |
|---|---|---|
| NDI® runtime (`Processing.NDI.Lib.x64.dll`, `libndi.dylib`, `libndi.so.6`) | Receiving NDI video/audio, bundled in release downloads | NDI SDK License Agreement – <https://ndi.link/ndisdk_license> (licence texts shipped as `NDI-*` files next to the runtime) |
| NDI SDK headers (`third_party/ndi/include`) | Compile-time API definitions | MIT (notice in each header), © Vizrt NDI AB |
| [SDL 3](https://github.com/libsdl-org/SDL) | Window, rendering, audio, input | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | On-screen controls | MIT |
| Roboto Medium font (shipped with Dear ImGui) | UI text | Apache 2.0 |

NDI® is a registered trademark of Vizrt NDI AB. FeedView is an independent application
that is compatible with NDI; it is not a product of, affiliated with, or endorsed by
Vizrt NDI AB. More about NDI: <https://ndi.video>.
