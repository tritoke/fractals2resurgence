/**
 * This file defines a set of compile-time defaults these options
 * can all be overridden with runtime flags later on
 */

/* The type of fractal to render */
const enum Fractal fractal_type = Mandelbrot;

/* the number of threads to use when rendering the image */
const uint32_t threads = 24;

/* the colourmap file to use */
const char * const mapfile = "libcmap/colourmaps/Skydye05.cmap";
/** dank:
 * damien3, Digiorg1, bud(2,4), droz(22,60)
 * glasses2, headache, Lindaa(02,04,06,09,10,16,17)
 * lkmtch(05,12),Skydye05
*/

/* ratio between the number of pixels on the X-axis
 * and the number of pixels on the Y-axis */
const double ratio = 1.0;

/* length of the X-axis in pixels */
const uint32_t xlen = 4000;

/* number of iterations to run for before declaring
 * a point part of the set */
const uint64_t iterations = 1000;

/* These options define the viewbox into which we render the mandelbrot set
 * e.g. the area of the "true" plot to be rendered */
const double xlen_real = 4;
const Point image_centre = { 0.0, 0.0 };

/**
 * If rendering a julia set use this point as the value for c.
 *
 * Good values:
 *  (-0.8, 0.156)
 *  (-0.4, 0.6)
 *  (0.285, 0.01)
 *  (-0.835, -0.2321)
 */

const Point julia_centre = {-0.8, 0.156};

/* file to write the final image to */
const char * const outfile = "out.ff";

/* default to non-verbose */
const bool verbose = false;

/* default to non-smooth colouring */
const bool smooth = false;
