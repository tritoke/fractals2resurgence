#include <arpa/inet.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "colourmap.h"

static Cmap * cmap;
static uint16_t * pixels;

static volatile uint32_t current_y = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

void colour(uint32_t x, uint32_t y, uint16_t * pixel) {
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
    // a^2 - b^2 + c
    temp = a2 - b2 + c_x;
    // 2ab + d
    b = ((a + a) * b) + c_y;
    a = temp;

    a2 = a * a;
    b2 = b * b;
#else
    // a^2 - b^2 + c
    temp = a2 - b2 + c;
    // 2ab + d
    b = ((a + a) * b) + d;
    a = temp;

    a2 = a * a;
    b2 = b * b;
#endif
  } 

  if (i == iterations) {
    const uint16_t default_pixel[4] = {0,0,0,UINT16_MAX};
    memcpy(pixel, default_pixel, 4*sizeof(uint16_t));
  } else {
    switch (style) {
      case 1:
        // load the colours from the specified map file
        memcpy(pixel, &cmap->colours[i % cmap->size], sizeof(Pixel));
        break;
      case 2: { // trippy pattern in the colours of the bi flag
          //#EC1C89
          const uint16_t pink[4] = {0xEC, 0x1C, 0x89, UINT16_MAX};
          //#9E49C2
          const uint16_t purple[4] = {0x9E, 0x49, 0xC2, UINT16_MAX};
          //#3F48CC
          const uint16_t blue[4] = {0x3F, 0x48, 0xCC, UINT16_MAX};
          const uint16_t * clrs[3] = {pink, purple, blue};
          memcpy(pixel, clrs[i%3], 4*sizeof(uint16_t));
          break;
        }
      case 3: { // trippy pattern in the colours of the pan flag
          //#FF1B8D
					const uint16_t pink[4] = {0xFF, 0x1B, 0x8D, UINT16_MAX};
					//#FFDA00
          const uint16_t yellow[4] = {0xFF, 0xDA, 0x00, UINT16_MAX};
					///#1BB3FF
          const uint16_t blue[4] = {0x1B, 0xB3, 0xFF, UINT16_MAX};
          const uint16_t * clrs[3] = {pink, yellow, blue};
          memcpy(pixel, clrs[i%3], 4*sizeof(uint16_t));
          break;
        }
      case 4: { // trippy pattern in the colours of the trans flag
          //#55CDFC
          const uint16_t pink[4] = {0x55, 0xCD, 0xFC, UINT16_MAX};
          //#FFFFFF
          const uint16_t white[4] = {0xFF, 0xFF, 0xFF, UINT16_MAX};
          //#F7A8B8
          const uint16_t blue[4] = {0xF7, 0xA8, 0xB8, UINT16_MAX};
          const uint16_t * clrs[5] = {blue, pink, white, pink, blue};
          memcpy(pixel, clrs[i%5], 4*sizeof(uint16_t));
          break;
        }
      case -1: {
        //good for between 100 and 1000 iterations
				i = i & ((uint32_t) ~0); // crop i to uint32_t
        uint16_t scaled_pixel[4] = {htons(i<<8), htons(i<<8), htons(i<<8), UINT16_MAX};
        memcpy(pixel, scaled_pixel, 4*sizeof(uint16_t));
        break;
      }
      case -2: {
        //good for more than 1000 iterations
				i = i & ((uint32_t) ~0); // crop i to uint32_t
        uint16_t scaled_pixel[4] = {htons(i<<7), htons(i<<7), htons(i<<7), UINT16_MAX};
        memcpy(pixel, scaled_pixel, 4*sizeof(uint16_t));
        break;
      }
			case 0:
      default: 
      // the default colour scheme is good for less than 100 iterations
      {
				i = i & ((uint32_t) ~0); // crop i to uint32_t
        uint16_t scaled_pixel[4] = {htons(i<<9), htons(i<<9), htons(i<<9), UINT16_MAX};
        memcpy(pixel, scaled_pixel, 4*sizeof(uint16_t));
      }
    }
  }
}

static void * rowrenderer(void * varg) {
  /*
   * colours the y'th row of the image.
   */

  uint32_t y = ((uint64_t) varg) & ((uint32_t) ~0);
  while (y < ylen) {
    uint16_t * row = pixels + (y * xlen << 2);

    // x << 2 == 4 * x
    // this is the x'th pixel in the row
    for (uint32_t x = 0; x < xlen; x++) {
      //printf("[thread]\tx:%d\ty:%d\n", x, y);
      colour(x,y,&row[x<<2]);
		}

    // acquire lock
    if (pthread_mutex_lock(&mtx) == 0) {
      // lock acquired
      // get the next row to work on and then increment the global y
      y = current_y++;
    } else {
      // failed to acquire lock => exit from thread.
      return NULL;
    }

    // release lock
    if (pthread_mutex_unlock(&mtx) != 0) {
      // failed to release the lock exit the program.
			fprintf(stderr, "[thread]\tfailed to release thread mutex, exiting.\n");
      exit(EXIT_FAILURE);
    }
  }
  return NULL;
}

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

  pixels = malloc(sizeof(uint16_t) * xlen * ylen * 4);
  printf("no. uint16_ts = %zu\n", sizeof(uint16_t) * xlen * ylen * 4);
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
    if (pthread_create(&tids[i], NULL, rowrenderer, (void *) (uint64_t) i)) {
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

  fwrite(pixels, sizeof(uint16_t), xlen * ylen * 4, fp);

  puts("[main]\t\tclosing file");
  fclose(fp);

  puts("[main]\t\tfreeing memory");
  free(pixels);

  return 0;
}
