#include <arpa/inet.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "colourmap.h"
#include "config.h"

typedef struct {
	uint32_t initial_y;
	uint32_t width;
	uint32_t height;
} RowArg;

/*
 * TODO: move to this rowarg
typedef struct {
	uint32_t * next_row;
	pthread_mutex_t * row_lock;
	uint32_t width;
	uint32_t height;
} RowArg;*/

/* https://www.unix.com/programming/173333-how-sleep-wake-thread.html */

static Cmap * cmap;
static Pixel * pixels;
static const Pixel default_pixel = {
	.red = 0,
	.green = 0,
	.blue = 0,
	.alpha = UINT16_MAX
};

static volatile uint32_t current_y = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

void colour(uint32_t x, uint32_t y, Pixel * pixel);
static void * rowrenderer(void * varg);

int main(void) {
  /*
   *  initial check on the ratio between the size of the
   *  area specified by the top right and bottom left coordinates
   *  and the ratio between the x and y lengths of the image to be written
   */
  long double image_ratio = (long double) xlen / (long double) ylen;
  long double area_ratio  = (trx - blx) / (try - bly);
  printf("image ratio: %.30Lf\narea ratio:  %.30Lf\n", image_ratio, area_ratio);

  /* fill the colour pallette */
  cmap = read_map(mapfile);

  pixels = calloc(xlen * ylen, sizeof(Pixel));
  pthread_t tids[threads];

  if (style == 1) {
		free_cmap(cmap);
		cmap = gen_random_map(10);
	}

  // acquire lock
  if (pthread_mutex_lock(&mtx) != 0) {
    // failed to acquire lock => exit
    exit(EXIT_FAILURE);
  }

  /* open the file and write the magic value / metadata */
  FILE * fp;
  fp = fopen(fname, "wb");

  fwrite("farbfeld", sizeof(char), 8, fp);
  uint32_t geom[2] = {htonl(xlen), htonl(ylen)};
  fwrite(geom, sizeof(uint32_t), 2, fp);
  for (uint32_t i = 0; i < threads; i++, current_y++) {
		RowArg * arg = calloc(1, sizeof(RowArg));
		arg->initial_y = i;
		arg->width = xlen;
		arg->height = ylen;
    if (pthread_create(&tids[i], NULL, rowrenderer, arg)) {
      printf("error creating thread %d\n", i);
      return 1;
    } else printf("[thread]\t%d\tcreated\n", i);
  } 

  // release lock
  if (pthread_mutex_unlock(&mtx) != 0) {
    // failed to release the lock => exit
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < threads; i++) {
    if (pthread_join(tids[i], NULL)) {
      printf("failed to join thread %d\n", i);
      return 2;
    } else printf("[thread]\t%d\tjoined\n", i);
  }

  puts("[main]\t\twriting pixels");

  fwrite(pixels, sizeof(Pixel), xlen * ylen, fp);

  puts("[main]\t\tclosing file");
  fclose(fp);

  puts("[main]\t\tfreeing memory");
  free(pixels);

  return 0;
}

static void * rowrenderer(void * varg) {
  /*
   * colours the y'th row of the image.
   */

	RowArg * arg = (RowArg *) varg;
	uint32_t y = arg->initial_y,
	         width = arg->width,
	         height = arg->height;

	/* Free the argument struct */
	free(varg);

  while (y < height) {
		/* Get reference of the current row in the image */
    Pixel * row = &pixels[y * width];

		/* Colour each pixel in the row */
    for (uint32_t x = 0; x < width; x++) {
      colour(x,y,&row[x]);
		}

    /* Acquire the lock and get the next row that needs colouring */
    if (pthread_mutex_lock(&mtx) == 0) {
      /*
			 * lock acquired
       * get the next row to work on and then increment the global y
			 */
      y = current_y++;
    } else {
      /* failed to acquire lock => exit from thread */
      return NULL;
    }

    /* release lock */
    if (pthread_mutex_unlock(&mtx) != 0) {
      /* failed to release the lock exit the program */
			fputs("[thread]\tfailed to release thread mutex, exiting.", stderr);
      exit(EXIT_FAILURE);
    }
  }
  return NULL;
}

void colour(uint32_t x, uint32_t y, Pixel * pixel) {
  /*
   * colour the pixel with the values for the coordinate at x+iy
   *  z = a + bi, c = c + di
   */

  size_t i = 0;
  long double c = blx + (x / (xlen / (trx-blx))),
              d = try - (y / (ylen / (try-bly))),
              a = c,
              b = d,
              a2 = a * a,
              b2 = b * b,
              temp;

  while ((i < iterations) && ((a2 + b2) < 4)) {
    i++;
#ifdef JULIA
    /* a^2 - b^2 + c_x
     * 2ab + c_y */
    temp = a2 - b2 + c_x;
    b = ((a + a) * b) + c_y;
#else
    /* a^2 - b^2 + c
     * 2ab + d */
    temp = a2 - b2 + c;
    b = ((a + a) * b) + d;
#endif
    a = temp;
    a2 = a * a;
    b2 = b * b;
  } 

  if (i == iterations) {
    memcpy(pixel, &default_pixel, sizeof(Pixel));
  } else {
    memcpy(pixel, &cmap->colours[i % cmap->size], sizeof(Pixel));
  }
}
