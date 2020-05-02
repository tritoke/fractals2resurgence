#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "colourmap.h"

Cmap * read_map(const char * mapfile) {
	/* Initialise the pixel we will read items into */
	size_t currlim = 256;
	Pixel * pixels = calloc(currlim, sizeof(Pixel));

	/* Set up the file IO */
	FILE * fp;
	if ((fp = fopen(mapfile, "r")) == NULL) {
		puts("Failed to open mapfile, exiting.");
		exit(EXIT_FAILURE);
	}

	/* Parse the contents of the file */
	char * buf = NULL;
	size_t n = 0,
				 lineno = 0;
	ssize_t chars_read;
	while ((chars_read = getline(&buf, &n, fp)) != -1) {
		if (currlim == lineno) {
			/* Pixels array is full, double the space we have allocated */
			currlim <<= 2;
			if ((pixels = reallocarray(pixels, currlim, sizeof(Pixel))) == NULL) {
				goto reallocarray_error;
			}
		}

		int items_parsed;
		Pixel * pixel = &pixels[lineno];
		if (buf[0] == '#') { /* HTML hex colour */
			items_parsed = sscanf(buf, "#%2hx%2hx%2hx", &pixel->red, &pixel->green, &pixel->blue);
		} else { /* original map colour format */
			items_parsed = sscanf(buf, "%3hu %3hu %3hu", &pixel->red, &pixel->green, &pixel->blue);
		}

		if (items_parsed != 3) {
			/* parse error */
			fprintf(stderr, "[read_map]\tFailed to parse line %ld of colourmap file %s\n", lineno, mapfile);
			exit(EXIT_FAILURE);
		}

		/* Set pixel as opaque */
		pixels[lineno++].alpha = UINT16_MAX;
	}

	/* Check errno after getline fails */
	if (errno == ENOMEM || errno == EINVAL) {
		fputs("[read_map]\tgetline failed to read line", stderr);
		exit(EXIT_FAILURE);
	}

	/* free the buffer used while reading in the lines */
	free(buf);

	/* trim off the unused space in pixels array */
	if ((pixels = reallocarray(pixels, lineno, sizeof(Pixel))) == NULL) {
reallocarray_error:
		fputs("[read_map]\treallocarray failed to resize the pixels array -- out of memory", stderr);
		exit(EXIT_FAILURE);
	}

	/* Create the colourmap */
	Cmap * cmap = calloc(1, sizeof(Cmap));
	cmap->size = lineno;
	cmap->colours = pixels;
	return cmap;
}

void free_cmap(Cmap * cmap) { 
	free(cmap->colours);
	free(cmap);
}

Cmap * gen_random_map(const size_t size) {
	/* Generate a random colour map */

	/* Allocate the space to store the pixels */
	Pixel * pixels = calloc(size, sizeof(Pixel));

  srand(time(NULL));
  for (size_t i = 0; i < size; i++) {
    pixels[i].red   = rand() % UINT16_MAX;
    pixels[i].green = rand() % UINT16_MAX;
    pixels[i].blue  = rand() % UINT16_MAX;
    pixels[i].alpha = UINT16_MAX;
  }

	Cmap * cmap = calloc(size, sizeof(Cmap));
	cmap->size = size;
	cmap->colours = pixels;
	return cmap;
}

