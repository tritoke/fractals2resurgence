#include <arpa/inet.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <errno.h>
#include "colourmap.h"
#include "config.h"

struct settings {
	uint32_t width;
	uint32_t height;
	long double centre_x;
	long double centre_y;
	long double radius;
	long double ratio;
	long double julia_x;
	long double julia_y;
	enum Fractal fractal_type;
	struct colourmap * colourmap;
};

STAILQ_HEAD(tailq_head, row);

struct thread_arg {
	volatile uint32_t * next_row;
	struct settings * settings;
	pthread_mutex_t * row_lock;
	struct tailq_head * result_queue;
};

struct writer_arg {
	struct settings * settings;
	pthread_mutex_t * row_lock;
	struct tailq_head * result_queue;
	FILE * outfile;
};

struct row {
	uint32_t y;
	Pixel * pixels;
	STAILQ_ENTRY(row) tail;
};

struct ff_header {
	char magic[8];
	uint32_t width;
	uint32_t height;
};

static void * rowrenderer(void *);
static void * writer_thread(void *);
static void colour(uint32_t, uint32_t, Pixel *, struct settings *);

int main(void) {
	/* TODO: command line option parsing */

	/* Create the settings struct set to compile time defaults */
	struct settings settings = {
		.width = xlen,
		.height = xlen * ratio,
		.centre_x = centre_x,
		.centre_y = centre_y,
		.radius = radius,
		.ratio = ratio,
		.julia_x = c_x,
		.julia_y = c_y,
		.fractal_type = fractal_type,
		.colourmap = read_map(mapfile)
	};

	/* open the file and write the header */
	FILE * fp = fopen(fname, "wb");

	struct ff_header header = {
		.magic = "farbfeld",
		.width = htonl(xlen),
		.height = htonl(xlen * ratio)
	};

	fwrite(&header, sizeof(struct ff_header), 1, fp);


	/* setup for starting the threads */
	volatile uint32_t next_row = 0;
	pthread_mutex_t row_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t tids[threads],
	          writer_tid; /* TODO: move to a writer thread */
	struct thread_arg * arg;

	/* setup the queue for the results */
	struct tailq_head head = STAILQ_HEAD_INITIALIZER(head);
	STAILQ_INIT(&head);

	/* Construct the arguments for each thread and start the threads */
	for (uint32_t i = 0; i < threads; i++) {
		arg = calloc(1, sizeof(struct thread_arg));
		
		arg->next_row = &next_row;
		arg->settings = &settings;
		arg->row_lock = &row_mutex;
		arg->result_queue = &head;

		if (pthread_create(&tids[i], NULL, rowrenderer, arg)) {
			fprintf(stderr, "error creating thread %d\n", i);
			exit(EXIT_FAILURE);
		} else {
			fprintf(stderr, "[thread]\t%d\tcreated\n", i);
		}
	}

	/* Start the writer thread */
	struct writer_arg * warg = calloc(1, sizeof(struct writer_arg));
	warg->settings = &settings;
	warg->row_lock = &row_mutex;
	warg->result_queue = &head;
	warg->outfile = fp;

	if (pthread_create(&writer_tid, NULL, writer_thread, warg)) {
		fputs("error creating writer thread\n", stderr);
		exit(EXIT_FAILURE);
	} else {
		fputs("[writer]\t\tcreated\n", stderr);
	}

	for (uint32_t i = 0; i < threads; i++) {
		if (pthread_join(tids[i], NULL)) {
			fprintf(stderr, "failed to join thread %d\n", i);
			exit(EXIT_FAILURE);
		} else {
			fprintf(stderr, "[thread]\t%d\tjoined\n", i);
		}
	}

	if (pthread_join(writer_tid, NULL)) {
		fputs("failed to join writer thread\n", stderr);
		exit(EXIT_FAILURE);
	} else {
		fputs("[writer]\t\tjoined\n", stderr);
	}

	fputs("[main]\t\tfreeing colourmap\n", stderr);
	free_cmap(settings.colourmap);

	fputs("[main]\t\tclosing file\n", stderr);
	fclose(fp);

	return 0;
}

static void * rowrenderer(void * varg) {
	/* colours the y'th row of the image.  */

	struct thread_arg * arg = (struct thread_arg *) varg;
	struct settings * settings = arg->settings;

	uint32_t curr_row;
	struct row * result;

	/* Aqcuire the lock */
	if (pthread_mutex_lock(arg->row_lock) != 0) {
		fputs("[thread]\tfailed to acquire lock, exiting.", stderr);
		exit(EXIT_FAILURE);
	}

	curr_row = (*arg->next_row)++;

	/* release lock */
	if (pthread_mutex_unlock(arg->row_lock) != 0) {
		fputs("[thread]\tfailed to release lock, exiting.", stderr);
		exit(EXIT_FAILURE);
	}

	while (curr_row < settings->height) {
		/* Allocate the space for the current row */
		Pixel * row = malloc(settings->width * sizeof(Pixel));
		if (errno == ENOMEM) {
			printf("errno = %d\n", errno);
		}

		/* Colour each pixel in the row */
		for (uint32_t x = 0; x < settings->width; x++) {
			colour(x, curr_row, &row[x], settings);
		}

		/*
		 * Acquire the lock, get the next row to be rendered
		 * and add the current row to the queue
		 */
		if (pthread_mutex_lock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to acquire lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}

		/* Allocate result struct and add to queue */
		result = calloc(1, sizeof(struct row));
		result->y = curr_row;
		result->pixels = row;

		STAILQ_INSERT_TAIL(arg->result_queue, result, tail);

		/* Get the next row of the image to render */
		curr_row = (*arg->next_row)++;

		/* release lock */
		if (pthread_mutex_unlock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to release lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}
	}

	/* Free the argument struct */
	free(varg);

	return NULL;
}

static void * writer_thread(void * varg) {
	/* Takes rows from the result queue and writes them out */

	struct writer_arg * arg = (struct writer_arg *) varg;
	struct settings * settings = arg->settings;
	struct row * row = NULL;

	uint32_t rows_written = 0;
	ssize_t rowsize = settings->width * sizeof(Pixel);

	while (rows_written < settings->height) {
		/* Aqcuire the lock */
		if (pthread_mutex_lock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to acquire lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}

		/* get next item from queue */
		if (!STAILQ_EMPTY(arg->result_queue)) {
			row = STAILQ_FIRST(arg->result_queue);
			STAILQ_REMOVE_HEAD(arg->result_queue, tail);
		}

		/* release lock */
		if (pthread_mutex_unlock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to release lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}

		/* Write the block to the right offset in the file */
		if (row != NULL) {
			long offset = sizeof(struct ff_header) + rowsize * row->y;
			fseek(arg->outfile, offset, SEEK_SET);
			fwrite(row->pixels, sizeof(Pixel), settings->width, arg->outfile);

			/* Update state */
			rows_written++;

			/* free resources */
			free(row->pixels);
			free(row);
			row = NULL;
		}
	}

	/* Free the argument struct */
	free(varg);

	return NULL;
}

static void colour(uint32_t x, uint32_t y, Pixel * pixel, struct settings * settings) {
	/*
	 * colour the pixel with the values for the coordinate at x+iy
	 *	z = a + bi, c = c + di
	 */

	static const Pixel default_pixel = {
		.red = 0,
		.green = 0,
		.blue = 0,
		.alpha = UINT16_MAX
	};

  long double blx = settings->centre_x - settings->radius,
              bly = settings->centre_y - (settings->radius * settings->ratio),
              trx = settings->centre_x + settings->radius,
              try = settings->centre_y + (settings->radius * settings->ratio),
							c_x = settings->julia_x,
							c_y = settings->julia_y;


	size_t i = 0;
	long double c = blx + (x / (xlen / (trx-blx))),
	            d = try - (y / ((xlen * ratio) / (try-bly))),
	            a = c,
	            b = d,
	            a2 = a * a,
	            b2 = b * b,
	            temp;

	while ((i < iterations) && ((a2 + b2) < 4)) {
		i++;
		temp = a2 - b2 + (settings->fractal_type == Julia ? c_x : c);
		b = ((a + a) * b) + (settings->fractal_type == Julia ? c_y : d);
		a = temp;
		a2 = a * a;
		b2 = b * b;
	}

	if (i == iterations) {
		memcpy(pixel, &default_pixel, sizeof(Pixel));
	} else {
		memcpy(pixel, &settings->colourmap->colours[i % settings->colourmap->size], sizeof(Pixel));
	}
}
