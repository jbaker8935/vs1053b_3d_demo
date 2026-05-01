# VS1053b Geometry Kernel for Wildbits K2

API and demonstration applications for 3d coprocessor application running on the Wildbits K2 VS1053b chip.

The C API is an reference/example implementation.  Application in other languages can use SCI directly to interface with the plugin.  See geometry_kernel.h and geometry_kernel.c to see how SCI calls are used.

[API Documentation](https://jbaker8935.github.io/vs1053b_3d_demo/)

## RECENT UPDATE

- Added makefile and made code changes to support oscar64

- Simplified edge streaming.  Visible edge vertices output in a packed list so they can be copied directly to the hardware line draw.   To allow edge correlation to an object an optional edge descriptor is provided.  Optionally read for near/far or object based edge coloring as desired.  The edge descriptor has have near/far flag | slot index | object edge index.   For speed the host streams the edge screen coords and outputs directly to hardware.  For object coloring, the host will read the edge descriptor first and then use host data to output the correct color value during line draw. 

- Supports scenes with 32 objects
- Supports up to 512 edges rendered in a single kernel call
- Supports objects with up to 60 Vertices, 90 Edges and 32 Faces.
- Up to 8 objects can be loaded simultaneously in the plugin

- Added example for a scene with 32 objects
- Added example for using the plugin from superbasic

- Reorganized plugin memory
- Updated the API and docs.
- Added doxygen comments to the api

If you have questions I'm usually on the  wildbits computing discord (jbaker8935)
