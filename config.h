/**
 * This file defines a set of compile-time defaults these options
 * can all be overridden with runtime flags later on
 */

/* The type of fractal to render */
enum Fractal fractal_type = Julia;

/* the number of threads to use when rendering the image */
uint32_t threads = 24;

/* the colourmap file to use */
char * mapfile = "libcmap/colourmaps/hot.cmap";
/** dank:
 * damien3, Digiorg1, bud(2,4), droz(22,60)
 * glasses2, headache, Lindaa(02,04,06,09,10,16,17)
 * lkmtch(05,12),Skydye05
*/

/* ratio between the number of pixels on the X-axis
 * and the number of pixels on the Y-axis */
long double ratio = 1.0;

/* length of the X-axis in pixels */
uint32_t xlen = 4000;

/* number of iterations to run for before declaring
 * a point part of the set */
uint64_t iterations = 100000;

/* These options define the viewbox into which we render the mandelbrot set
 * e.g. the area of the "true" plot to be rendered */
long double xlen_real = 4;
Point image_centre = {
	.x = 0.0,
	.y = 0.0
};

/**
 * If rendering a julia set use this point as the value for c.
 *
 * Good values:
 *  (-0.8, 0.156)
 *  (-0.4, 0.6)
 *  (0.285, 0.01)
 *  (-0.835, -0.2321)
 */

Point julia_centre = {-0.8, 0.156};

/* file to write the final image to */
char * outfile = "out.ff";
