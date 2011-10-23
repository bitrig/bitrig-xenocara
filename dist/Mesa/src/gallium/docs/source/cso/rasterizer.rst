.. _rasterizer:

Rasterizer
==========

The rasterizer state controls the rendering of points, lines and triangles.
Attributes include polygon culling state, line width, line stipple,
multisample state, scissoring and flat/smooth shading.

Shading
-------

flatshade
^^^^^^^^^

If set, the provoking vertex of each polygon is used to determine the color
of the entire polygon.  If not set, fragment colors will be interpolated
between the vertex colors.

The actual interpolated shading algorithm is obviously
implementation-dependent, but will usually be Gourard for most hardware.

.. note::

    This is separate from the fragment shader input attributes
    CONSTANT, LINEAR and PERSPECTIVE. The flatshade state is needed at
    clipping time to determine how to set the color of new vertices.

    :ref:`Draw` can implement flat shading by copying the provoking vertex
    color to all the other vertices in the primitive.

flatshade_first
^^^^^^^^^^^^^^^

Whether the first vertex should be the provoking vertex, for most primitives.
If not set, the last vertex is the provoking vertex.

There are several important exceptions to the specification of this rule.

* ``PIPE_PRIMITIVE_POLYGON``: The provoking vertex is always the first
  vertex. If the caller wishes to change the provoking vertex, they merely
  need to rotate the vertices themselves.
* ``PIPE_PRIMITIVE_QUAD``, ``PIPE_PRIMITIVE_QUAD_STRIP``: This option has no
  effect; the provoking vertex is always the last vertex.
* ``PIPE_PRIMITIVE_TRIANGLE_FAN``: When set, the provoking vertex is the
  second vertex, not the first. This permits each segment of the fan to have
  a different color.

Polygons
--------

light_twoside
^^^^^^^^^^^^^

If set, there are per-vertex back-facing colors.  The hardware
(perhaps assisted by :ref:`Draw`) should be set up to use this state
along with the front/back information to set the final vertex colors
prior to rasterization.

The frontface vertex shader color output is marked with TGSI semantic
COLOR[0], and backface COLOR[1].

front_ccw
    Indicates whether the window order of front-facing polygons is
    counter-clockwise (TRUE) or clockwise (FALSE).

cull_mode
    Indicates which faces of polygons to cull, either PIPE_FACE_NONE
    (cull no polygons), PIPE_FACE_FRONT (cull front-facing polygons),
    PIPE_FACE_BACK (cull back-facing polygons), or
    PIPE_FACE_FRONT_AND_BACK (cull all polygons).

fill_front
    Indicates how to fill front-facing polygons, either
    PIPE_POLYGON_MODE_FILL, PIPE_POLYGON_MODE_LINE or
    PIPE_POLYGON_MODE_POINT.
fill_back
    Indicates how to fill back-facing polygons, either
    PIPE_POLYGON_MODE_FILL, PIPE_POLYGON_MODE_LINE or
    PIPE_POLYGON_MODE_POINT.

poly_stipple_enable
    Whether polygon stippling is enabled.
poly_smooth
    Controls OpenGL-style polygon smoothing/antialiasing

offset_point
    If set, point-filled polygons will have polygon offset factors applied
offset_line
    If set, line-filled polygons will have polygon offset factors applied
offset_tri
    If set, filled polygons will have polygon offset factors applied

offset_units
    Specifies the polygon offset bias
offset_scale
    Specifies the polygon offset scale



Lines
-----

line_width
    The width of lines.
line_smooth
    Whether lines should be smoothed. Line smoothing is simply anti-aliasing.
line_stipple_enable
    Whether line stippling is enabled.
line_stipple_pattern
    16-bit bitfield of on/off flags, used to pattern the line stipple.
line_stipple_factor
    When drawing a stippled line, each bit in the stipple pattern is
    repeated N times, where N = line_stipple_factor + 1.
line_last_pixel
    Controls whether the last pixel in a line is drawn or not.  OpenGL
    omits the last pixel to avoid double-drawing pixels at the ends of lines
    when drawing connected lines.


Points
------

sprite_coord_enable
^^^^^^^^^^^^^^^^^^^

Controls automatic texture coordinate generation for rendering sprite points.

When bit k in the sprite_coord_enable bitfield is set, then generic
input k to the fragment shader will get an automatically computed
texture coordinate.

The texture coordinate will be of the form (s, t, 0, 1) where s varies
from 0 to 1 from left to right while t varies from 0 to 1 according to
the state of 'sprite_coord_mode' (see below).

If any bit is set, then point_smooth MUST be disabled (there are no
round sprites) and point_quad_rasterization MUST be true (sprites are
always rasterized as quads).  Any mismatch between these states should
be considered a bug in the state-tracker.

This feature is implemented in the :ref:`Draw` module but may also be
implemented natively by GPUs or implemented with a geometry shader.


sprite_coord_mode
^^^^^^^^^^^^^^^^^

Specifies how the value for each shader output should be computed when drawing
point sprites. For PIPE_SPRITE_COORD_LOWER_LEFT, the lower-left vertex will
have coordinates (0,0,0,1). For PIPE_SPRITE_COORD_UPPER_LEFT, the upper-left
vertex will have coordinates (0,0,0,1).
This state is used by :ref:`Draw` to generate texcoords.


point_quad_rasterization
^^^^^^^^^^^^^^^^^^^^^^^^

Determines if points should be rasterized according to quad or point
rasterization rules.

OpenGL actually has quite different rasterization rules for points and
point sprites - hence this indicates if points should be rasterized as
points or according to point sprite (which decomposes them into quads,
basically) rules.

Additionally Direct3D will always use quad rasterization rules for
points, regardless of whether point sprites are enabled or not.

If this state is enabled, point smoothing and antialiasing are
disabled. If it is disabled, point sprite coordinates are not
generated.

.. note::

   Some renderers always internally translate points into quads; this state
   still affects those renderers by overriding other rasterization state.

point_smooth
    Whether points should be smoothed. Point smoothing turns rectangular
    points into circles or ovals.
point_size_per_vertex
    Whether the vertex shader is expected to have a point size output.
    Undefined behaviour is permitted if there is disagreement between
    this flag and the actual bound shader.
point_size
    The size of points, if not specified per-vertex.



Other Members
-------------

scissor
    Whether the scissor test is enabled.

multisample
    Whether :term:`MSAA` is enabled.

gl_rasterization_rules
    Whether the rasterizer should use (0.5, 0.5) pixel centers. When not set,
    the rasterizer will use (0, 0) for pixel centers.

