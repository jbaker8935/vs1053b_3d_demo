# VS1053b Geometry Kernel for Wildbits K2

API and demonstration applications for 3d coprocessor application running on the Wildbits K2 VS1053b chip.

[API Documentation](https://jbaker8935.github.io/vs1053b_3d_demo/)

## FUTURE PLANS

- Simplified edge streaming.  Visible edge vertices are output in a packed list so they can be fed directly to the hardware line draw.   To allow edge correlation to an object a separate edge descriptor list will be provided.  It can be optionally read for near/far or object based edge coloring as desired.  The edge descriptor will have near/far flag | slot index | object edge index.   For speed the host will stream read the edge screen coords and output directly to hardware using a defined color.  For object coloring, the host will read the edge descriptor first and then use host data to output the correct color value during line draw.  I think this can be the default output mode for both single object and scene modes.   While this does require duplicate outputs for shared vertices it should be much more efficient when hidden lines are enabled.   Need to rethink the chip memory layout a bit to realize this.

- Increase Object Size limits (vertices & edges)

- Increase Slots and Scene size

If you have questions I'm usually on the  wildbits computing discord (jbaker8935)
