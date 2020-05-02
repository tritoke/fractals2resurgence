/* defines the style of the resulting render
 * -2 -> greyscale for iterations between 100 and 1000
 * -1 -> greyscale for more than 1000 iterations
 * 0 -> use the map file specified by mapfile
 * 1 -> randomly generated colours
 * 2 -> bi flag alternating each pixel
 * 3 -> pan flag alternating each pixel
 * 4 -> trans flag alternating each pixel
 */
#define style 1

//const char mapfile[] = "colourmaps/Skydye05.cmap";
const char mapfile[] = "colourmaps/Lindaa04.cmap";
/* dank:
damien3, Digiorg1, bud(2,4), droz(22,60)
glasses2, headache, Lindaa(02,04,06,09,10,16,17)
lkmtch(05,12),Skydye05
*/

// the x and y lengths of the image in numbers of pixels
// as well as the number of threads to use and the number
// of iterations before declaring a value to be in the set
#define ratio 1
const size_t xlen = 2000;
const size_t ylen = (xlen * ratio);
const size_t iterations = 2000;

// the number of threads to use when rendering the image
#define threads 8

// the bottom left (blx, bly) and top right (trx, try) coordinates
// defining the area of the fractal to render


/* If defined this will create a julia set with c = c_x + i * c_y */
//#define JULIA
#ifdef JULIA  /* render julia set */

/*
 * Good values:
 *  (-0.8, 0.156)
 *  (-0.4, 0.6)
 *  (0.285, 0.01)
 *  (-0.835, -0.2321)
 */

const long double c_x = -0.8;
const long double c_y = 0.156;

#endif


#if (0) // close

#define radius 0.0001
#define centre_x -0.7496866
#define centre_y (radius * ratio) + 0.034334

#else // full set

#define radius 2
#define centre_x 0
#define centre_y 0

#endif

const long double x_off = 0,
                  y_off = 0,
                  blx = centre_x - (radius * 1) + x_off,
                  bly = centre_y - (radius * ratio) + y_off,
                  trx = centre_x + (radius * 1) + x_off,
                  try = centre_y + (radius * ratio) + y_off;

#define fname "out.ff"
