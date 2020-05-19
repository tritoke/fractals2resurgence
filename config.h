/*
 * This file defines a set of compile-time defaults these options
 * can all be overridden with runtime flags later on
 */

/* the colourmap file to use */
char mapfile[] = "colourmaps/Skydye05.cmap";
/* dank:
damien3, Digiorg1, bud(2,4), droz(22,60)
glasses2, headache, Lindaa(02,04,06,09,10,16,17)
lkmtch(05,12),Skydye05
*/

// the x and y lengths of the image in numbers of pixels
// as well as the number of threads to use and the number
// of iterations before declaring a value to be in the set
long double ratio = 1.0;
uint32_t xlen = 20000;
uint32_t iterations = 1000000;

// the number of threads to use when rendering the image
uint8_t threads = 8;

enum Fractal {Julia, Mandelbrot};
enum Fractal fractal_type = Julia;

/* If defined this will create a julia set with c = c_x + i * c_y
 *
 * Good values:
 *  (-0.8, 0.156)
 *  (-0.4, 0.6)
 *  (0.285, 0.01)
 *  (-0.835, -0.2321)
 */

long double c_x = -0.8;
long double c_y = 0.156;

long double radius = 2.0F,
            centre_x = 0,
            centre_y = 0;

char fname[] = "out.ff";
