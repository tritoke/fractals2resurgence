#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include "cmap.h"

/* enum representing the different types of fractals available */
enum Fractal {Julia, Mandelbrot};

typedef struct {
	long double x;
	long double y;
} Point;

#include "config.h"

struct settings {
	uint32_t width;
	uint32_t height;
	uint64_t iterations;
	Point bottom_left;
	Point top_right;
	Point julia_centre;
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

static void parse_options(int, char **, enum Fractal *, uint32_t *, char **, long double *, uint32_t *, uint64_t *, long double *, Point *, Point *, char **);
static void usage(const char *);
static void * rowrenderer(void *);
static void * writer_thread(void *);
static void colour(const uint32_t, const uint32_t, Pixel *, const struct settings *);

int main(int argc, char * argv[]) {
	extern enum Fractal fractal_type;
	extern uint32_t threads;
	extern char * mapfile;
	extern long double ratio;
	extern uint32_t xlen;
	extern uint64_t iterations;
	extern long double xlen_real;
	extern Point image_centre;
	extern Point julia_centre;
	extern char * outfile;
	/* Parse the command line options */
	parse_options(argc, argv, &fractal_type, &threads, &mapfile, &ratio, &xlen, &iterations, &xlen_real, &image_centre, &julia_centre, &outfile);

	/* create the settings struct */
	long double ylen_real = xlen_real * ratio;

	Point bottom_left = {
		.x = image_centre.x - (xlen_real / 2),
		.y = image_centre.y - (ylen_real / 2),
	};

	Point top_right = {
		.x = image_centre.x + (xlen_real / 2),
		.y = image_centre.y + (ylen_real / 2),
	};

	struct settings settings = {
		.width = xlen,
		.height = xlen * ratio,
		.iterations = iterations,
		.bottom_left = bottom_left,
		.top_right = top_right,
		.julia_centre = julia_centre,
		.fractal_type = fractal_type,
		.colourmap = read_map(mapfile),
	};

	/* open the file and write the header */
	FILE * fp;
	if (strlen(outfile) == 1 && outfile[0] == '-') {
		/* write to stdout */
		fp = stdout;
	} else {
		fp = fopen(outfile, "wb");
	}

	struct ff_header header = {
		.magic = "farbfeld",
		.width = htonl(settings.width),
		.height = htonl(settings.height)
	};

	fwrite(&header, sizeof(struct ff_header), 1, fp);


	/* setup for starting the threads */
	volatile uint32_t next_row = 0;
	pthread_mutex_t row_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t tids[threads],
	          writer_tid;
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

static void parse_options(int argc, char ** argv, enum Fractal * fractal_type, uint32_t * threads, char ** mapfile, long double * ratio, uint32_t * width, uint64_t * iterations, long double * xlen_real, Point * image_centre, Point * julia_centre, char ** outfile) {
	struct option long_options[] = {
		/* put the long-only options first */
		{ "image_centre", required_argument, NULL,  0  },
		{ "julia_centre", required_argument, NULL,  0  },

		/* now long and short args */
		{ "fractal_type", required_argument, NULL, 'f' },
		{ "threads",      required_argument, NULL, 't' },
		{ "mapfile",      required_argument, NULL, 'm' },
		{ "ratio",        required_argument, NULL, 'r' },
		{ "width",        required_argument, NULL, 'w' },
		{ "iterations",   required_argument, NULL, 'i' },
		{ "xlen_real",    required_argument, NULL, 'x' },
		{ "outfile",      required_argument, NULL, 'o' },
		{ "help",         no_argument,       NULL, 'h' },
		{ NULL,           0,                 NULL,  0  },
	};

	/* save the program name to pass to usage later */
	char * program_name = argv[0];
	int option_index = 0, c;

  while ((c = getopt_long(argc, argv, "f:t:m:r:w:i:x:o:h", long_options, &option_index)) != -1) {
		switch (c) {
			case 0: /* long option */
				printf("option %s", long_options[option_index].name);
				if (optarg)
					printf(" with arg %s", optarg);
				printf("\n");

				switch (option_index) {
					case 0:
						if (sscanf(optarg, "%Lf,%Lf", &image_centre->x, &image_centre->y) != 2) {
							fprintf(stderr, "Failed to parse image_centre: %s\n", optarg);
						}
						break;
					case 1:
						if (sscanf(optarg, "%Lf,%Lf", &julia_centre->x, &julia_centre->y) != 2) {
							fprintf(stderr, "Failed to parse julia_centre: %s\n", optarg);
						}
						break;
				}
				break;
			case 'f':
				if (strcasecmp("julia", optarg) == 0) {
					/* render julia set */
					*fractal_type = Julia;
				} else if (strcasecmp("mandelbrot", optarg) == 0) {
					/* render mandelbrot set */
					*fractal_type = Mandelbrot;
				} else {
					fprintf(stderr, "Unsupported fractal type: %s\n", optarg);
				}
				break;
			case 't':
				printf("Got option t: %s\n", optarg);
				if (sscanf(optarg, "%u", threads) != 1) {
					fprintf(stderr, "Failed to parse threads: %s\n", optarg);
				}
				break;
			case 'm':
				printf("Got option m: %s\n", optarg);
				*mapfile = optarg;
				break;
			case 'r':
				printf("Got option r: %s\n", optarg);
				if (sscanf(optarg, "%Lf", ratio) != 1) {
					fprintf(stderr, "Failed to parse ratio: %s\n", optarg);
				}
				break;
			case 'w':
				printf("Got option w: %s\n", optarg);
				if (sscanf(optarg, "%u", width) != 1) {
					fprintf(stderr, "Failed to parse width: %s\n", optarg);
				}
				break;
			case 'i':
				printf("Got option i: %s\n", optarg);
				if (sscanf(optarg, "%lu", iterations) != 1) {
					fprintf(stderr, "Failed to parse width: %s\n", optarg);
				}
				break;
			case 'x':
				printf("Got option x: %s\n", optarg);
				if (sscanf(optarg, "%Lf", xlen_real) != 1) {
					fprintf(stderr, "Failed to parse xlen_real: %s\n", optarg);
				}
				break;
			case 'o':
				printf("Got option o: %s\n", optarg);
				*outfile = optarg;
				break;
			case 'h':
				printf("Got option h: %s\n", optarg);
				usage(program_name);
				exit(EXIT_SUCCESS);
		}
	}
}

static void usage(const char * program_name) {
	puts("Usage:");
	printf("  %s [options]\n\n", program_name);

	puts("  -h, --help      show list of command-line options");
	/* TODO: the rest of the help lol */
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

static void colour(const uint32_t x, const uint32_t y, Pixel * pixel, const struct settings * settings) {
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

  long double blx = settings->bottom_left.x,
              bly = settings->bottom_left.y,
              trx = settings->top_right.x,
              try = settings->top_right.y,
              c_x = settings->julia_centre.x,
              c_y = settings->julia_centre.y;


	size_t i = 0;
	long double c = blx + (x / (settings->width / (trx-blx))),
	            d = try - (y / ((settings->width * ratio) / (try-bly))),
	            a = c,
	            b = d,
	            a2 = a * a,
	            b2 = b * b,
	            temp;

	while ((i < settings->iterations) && ((a2 + b2) < 4)) {
		i++;
		temp = a2 - b2 + (settings->fractal_type == Julia ? c_x : c);
		b = ((a + a) * b) + (settings->fractal_type == Julia ? c_y : d);
		a = temp;
		a2 = a * a;
		b2 = b * b;
	}

	if (i == settings->iterations) {
		memcpy(pixel, &default_pixel, sizeof(Pixel));
	} else {
		memcpy(pixel, &settings->colourmap->colours[i % settings->colourmap->size], sizeof(Pixel));
	}
}
