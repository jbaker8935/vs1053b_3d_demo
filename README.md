# VS1053b Geometry Kernel for Wildbits K2

API and demonstration applications for 3d coprocessor application running on the Wildbits K2 VS1053b chip.

[API Documentation](https://jbaker8935.github.io/vs1053b_3d_demo/)

## FUTURE PLANS

- Simplified edge streaming.  Visible edge vertices output in a packed list so they can be copied directly to the hardware line draw.   To allow edge correlation to an object a separate edge descriptor list will be provided.  Optionally read for near/far or object based edge coloring as desired.  The edge descriptor will have near/far flag | slot index | object edge index.   For speed the host will stream read the edge screen coords and output directly to hardware using a defined color.  For object coloring, the host will read the edge descriptor first and then use host data to output the correct color value during line draw.  Simpler, smaller, faster host logic, but will require changing plugin logic and memory layout.

- If memory allows will increase object and scene size

If you have questions I'm usually on the  wildbits computing discord (jbaker8935)
