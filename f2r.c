#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <math.h>
#include "cmap.h"

/* enum representing the different types of fractals available */
enum Fractal {Julia, Mandelbrot};

typedef struct {
	double x;
	double y;
} Point;

#include "config.h"

/* the settings used by threads to create the render */
struct settings {
	uint32_t width;
	uint32_t height;
	uint64_t iterations;
	Point bottom_left;
	Point top_right;
	Point julia_centre;
	enum Fractal fractal_type;
	struct colourmap * colourmap;
	bool sorted_queue_insert;
	bool verbose;
	bool smooth;
};

/* exclusively those settings controlled by the user */
struct user_options {
	enum Fractal fractal_type;
	uint32_t threads;
	const char * mapfile;
	double ratio;
	uint32_t width;
	uint64_t iterations;
	double xlen_real;
	Point image_centre;
	Point julia_centre;
	const char * outfile;
	bool verbose;
	bool smooth;
};

STAILQ_HEAD(tailq_head, row);

struct thread_arg {
	uint32_t * next_row;
	const struct settings * settings;
	pthread_mutex_t * row_lock;
	struct tailq_head * result_queue;
};

struct writer_arg {
	const struct settings * settings;
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

static void parse_options(int, char **, struct user_options *);
static void usage(const char *);
static void * rowrenderer(void *);
static void * writer_thread(void *);
static void colour(const uint32_t, const uint32_t, Pixel *, const struct settings *);

int main(int argc, char * argv[]) {

	struct user_options uo = {
		.fractal_type = fractal_type,
		.threads = threads,
		.mapfile = mapfile,
		.ratio = ratio,
		.width = xlen,
		.iterations = iterations,
		.xlen_real = xlen_real,
		.image_centre = image_centre,
		.julia_centre = julia_centre,
		.outfile = outfile,
		.verbose = verbose,
		.smooth = smooth,
	};

	/* Parse the command line options */
	parse_options(argc, argv, &uo);

	/* create the settings struct */
	const double ylen_real = uo.xlen_real * uo.ratio;

	const Point bottom_left = {
		.x = uo.image_centre.x - (uo.xlen_real / 2),
		.y = uo.image_centre.y - (ylen_real / 2),
	};

	const Point top_right = {
		.x = uo.image_centre.x + (uo.xlen_real / 2),
		.y = uo.image_centre.y + (ylen_real / 2),
	};

	/* open the file and write the header */
	FILE * fp;
	bool sorted_queue_insert = false;
	if (strlen(uo.outfile) == 1 && uo.outfile[0] == '-') {
		/* write to stdout */
		fp = stdout;
		sorted_queue_insert = true;
	} else {
		fp = fopen(outfile, "w");
	}

	const struct settings settings = {
		.width = uo.width,
		.height = uo.width * uo.ratio,
		.iterations = uo.iterations,
		.bottom_left = bottom_left,
		.top_right = top_right,
		.julia_centre = uo.julia_centre,
		.fractal_type = uo.fractal_type,
		.colourmap = read_map(uo.mapfile),
		.verbose = uo.verbose,
		.smooth = uo.smooth,
		.sorted_queue_insert = sorted_queue_insert,
	};

	const struct ff_header header = {
		.magic = "farbfeld",
		.width = htonl(settings.width),
		.height = htonl(settings.height)
	};

	fwrite(&header, sizeof(struct ff_header), 1, fp);


	/* setup for starting the threads */
	uint32_t next_row = 0;
	pthread_mutex_t row_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t tids[threads],
	          writer_tid;

	/* setup the queue for the results */
	struct tailq_head head = STAILQ_HEAD_INITIALIZER(head);
	STAILQ_INIT(&head);

	/* set up the thread arguments */
	struct thread_arg targ = {
		.next_row = &next_row,
		.settings = &settings,
		.row_lock = &row_mutex,
		.result_queue = &head,
	};

	struct writer_arg warg = {
		.settings = &settings,
		.row_lock = &row_mutex,
		.result_queue = &head,
		.outfile = fp,
	};

	/* start the renderer threads */
	for (uint32_t i = 0; i < threads; i++) {

		if (pthread_create(&tids[i], NULL, rowrenderer, &targ)) {
			fprintf(stderr, "error creating thread %d\n", i);
			exit(EXIT_FAILURE);
		} else if (settings.verbose) {
			fprintf(stderr, "[thread]\t%d\tcreated\n", i);
		}
	}

	/* Start the writer thread */
	if (pthread_create(&writer_tid, NULL, writer_thread, &warg)) {
		fputs("error creating writer thread\n", stderr);
		exit(EXIT_FAILURE);
	} else if (settings.verbose) {
		fputs("[writer]\t\tcreated\n", stderr);
	}

	/* join render threads */
	for (uint32_t i = 0; i < threads; i++) {
		if (pthread_join(tids[i], NULL)) {
			fprintf(stderr, "failed to join thread %d\n", i);
			exit(EXIT_FAILURE);
		} else if (settings.verbose) {
			fprintf(stderr, "[thread]\t%d\tjoined\n", i);
		}
	}

	/* join writer thread */
	if (pthread_join(writer_tid, NULL)) {
		fputs("failed to join writer thread\n", stderr);
		exit(EXIT_FAILURE);
	} else if (settings.verbose) {
		fputs("[writer]\t\tjoined\n", stderr);
	}

	if (settings.verbose) {
		fputs("[main]\t\tfreeing colourmap\n", stderr);
	}
	free_cmap(settings.colourmap);

	if (settings.verbose) {
		fputs("[main]\t\tclosing file\n", stderr);
	}
	fclose(fp);

	return 0;
}

static void parse_options(int argc, char ** argv, struct user_options * uo) {
	const struct option long_options[] = {
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
		{ "verbose",      no_argument,       NULL, 'v' },
		{ "smooth",       no_argument,       NULL, 's' },
		{ NULL,           0,                 NULL,  0  },
	};

	/* save the program name to pass to usage later */
	const char * program_name = argv[0];
	int option_index = 0, c;

  while ((c = getopt_long(argc, argv, "f:t:m:r:w:i:x:o:hsv", long_options, &option_index)) != -1) {
		switch (c) {
			case 0: /* long option */
				switch (option_index) {
					case 0:
						if (sscanf(optarg, "%lf,%lf", &uo->image_centre.x, &uo->image_centre.y) != 2) {
							fprintf(stderr, "Failed to parse image_centre: %s\n", optarg);
						}
						break;
					case 1:
						if (sscanf(optarg, "%lf,%lf", &uo->julia_centre.x, &uo->julia_centre.y) != 2) {
							fprintf(stderr, "Failed to parse julia_centre: %s\n", optarg);
						}
						break;
				}
				break;
			case 'f':
				if (strcasecmp("julia", optarg) == 0) {
					/* render julia set */
					uo->fractal_type = Julia;
				} else if (strcasecmp("mandelbrot", optarg) == 0) {
					/* render mandelbrot set */
					uo->fractal_type = Mandelbrot;
				} else {
					fprintf(stderr, "Unsupported fractal type: %s\n", optarg);
				}
				break;
			case 't':
				if (sscanf(optarg, "%u", &uo->threads) != 1) {
					fprintf(stderr, "Failed to parse threads: %s\n", optarg);
				}
				break;
			case 'm':
				uo->mapfile = optarg;
				break;
			case 'r':
				if (sscanf(optarg, "%lf", &uo->ratio) != 1) {
					fprintf(stderr, "Failed to parse ratio: %s\n", optarg);
				}
				break;
			case 'w':
				if (sscanf(optarg, "%u", &uo->width) != 1) {
					fprintf(stderr, "Failed to parse width: %s\n", optarg);
				}
				break;
			case 'i':
				if (sscanf(optarg, "%lu", &uo->iterations) != 1) {
					fprintf(stderr, "Failed to parse width: %s\n", optarg);
				}
				break;
			case 'x':
				if (sscanf(optarg, "%lf", &uo->xlen_real) != 1) {
					fprintf(stderr, "Failed to parse xlen_real: %s\n", optarg);
				}
				break;
			case 'o':
				uo->outfile = optarg;
				break;
			case 'h':
				usage(program_name);
				exit(EXIT_SUCCESS);
			case 'v':
				uo->verbose = true;
				break;
			case 's':
				uo->smooth = true;
				break;
		}
	}
}

static void usage(const char * program_name) {
	puts("Usage:");
	printf("  %s [options]\n", program_name);
	puts("");
	puts("  -h, --help           show list of command-line options");
	puts("  -f, --fractal_type   type of fractal to render (julia|mandelbrot). default: mandelbrot");
	puts("  -t, --threads        number of renderer threads to start. default: 24");
	puts("  -m, --mapfile        colourmap file to take colors from. default: Skydye05.cmap");
	puts("  -r, --ratio          ratio between the y and x lengths of the bounding box. default: 1.0");
	puts("  -w, --width          width of the image in pixels. default 4000");
	puts("  -i, --iterations     number of iterations before a point is considered part of the set. default: 1000");
	puts("  -x, --xlen_real      length on the real / x axis of the bounding box. default: 4.0");
	puts("  -o, --outfile        file to save the resulting image to. default: out.ff");
	puts("  -v, --verbose        enables verbose output");
	puts("  -s, --smooth         enables smooth colouring at a performance penalty");
	puts("");
	puts("      --image_centre   centre of the image's bounding box. default: 0.0,0.0");
	puts("                       NOTE: takes 2 doubles x,y with NO SPACE between");
	puts("      --julia_centre   value of C in the calculation of the julia set iterations. default: -0.8,0.156");
	puts("                       NOTE: takes 2 doubles x,y with NO SPACE between");
}

static void * rowrenderer(void * varg) {
	/* colours the y'th row of the image.  */

	const struct thread_arg * arg = (struct thread_arg *) varg;
	const struct settings * settings = arg->settings;

	uint32_t curr_row;
	struct row * result;

	/* Aqcuire the lock */
	if (pthread_mutex_lock(arg->row_lock) != 0) {
		fputs("[thread]\tfailed to acquire lock, exiting.", stderr);
		exit(EXIT_FAILURE);
	}

	/* get the current row to render */
	curr_row = (*arg->next_row)++;

	/* release lock */
	if (pthread_mutex_unlock(arg->row_lock) != 0) {
		fputs("[thread]\tfailed to release lock, exiting.", stderr);
		exit(EXIT_FAILURE);
	}

	while (curr_row < settings->height) {
		/* Allocate the space for the current row */
		Pixel * const row = malloc(settings->width * sizeof(Pixel));

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

		/* sorted queue input when necessary */
		bool queued = false;
		if (settings->sorted_queue_insert) {
			struct row * queue_row;
			struct row * prev = NULL;
			STAILQ_FOREACH(queue_row, arg->result_queue, tail) {
				/* our row goes before this one in the output queue */
				if (queue_row->y > result->y) {
					/* check prev is not NULL, if it is we insert at the head */
					if (prev == NULL) {
						STAILQ_INSERT_HEAD(arg->result_queue, result, tail);
					} else {
						/* place current after prev */
						STAILQ_INSERT_AFTER(arg->result_queue, prev, result, tail);
					}

					/* assign to queued flag and break out of loop */
					queued = true;
					break;
				}
				prev = queue_row;
			}
		}

		/* if we haven't queued anything, put it at the tail */
		if (!queued) {
			/* insert the result into the queue */
			STAILQ_INSERT_TAIL(arg->result_queue, result, tail);
		}

		/* Get the next row of the image to render */
		curr_row = (*arg->next_row)++;

		/* release lock */
		if (pthread_mutex_unlock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to release lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}
	}

	return NULL;
}

static void * writer_thread(void * varg) {
	/* Takes rows from the result queue and writes them out */

	const struct writer_arg * arg = (struct writer_arg *) varg;
	const struct settings * settings = arg->settings;
	struct row * row = NULL;

	uint32_t rows_written = 0;
	const ssize_t rowsize = settings->width * sizeof(Pixel);

	while (rows_written < settings->height) {
		/* Aqcuire the lock */
		if (pthread_mutex_lock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to acquire lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}

		/* get next item from queue */
		bool correct_row = false;
		if (!STAILQ_EMPTY(arg->result_queue)) {
			row = STAILQ_FIRST(arg->result_queue);

			/* it writing to stdout we need to check the right row
			 * is being written before writing it */
			if (settings->sorted_queue_insert) {
				/* this is the correct next row, dequeue it */
				if (row->y == rows_written) {
					STAILQ_REMOVE_HEAD(arg->result_queue, tail);
					correct_row = true;
				}
			} else {
				/* otherwise just dequeue it */
				STAILQ_REMOVE_HEAD(arg->result_queue, tail);
			}
		}

		/* release lock */
		if (pthread_mutex_unlock(arg->row_lock) != 0) {
			fputs("[thread]\tfailed to release lock, exiting.", stderr);
			exit(EXIT_FAILURE);
		}

		/* Write the block to the right offset in the file */
		if (row != NULL && (correct_row || !settings->sorted_queue_insert)) {
			const long offset = sizeof(struct ff_header) + rowsize * row->y;
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

	return NULL;
}

static inline void colour(const uint32_t x, const uint32_t y, Pixel * pixel, const struct settings * settings) {
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

	double blx = settings->bottom_left.x,
	       bly = settings->bottom_left.y,
	       trx = settings->top_right.x,
	       try = settings->top_right.y,
	       c_x = settings->julia_centre.x,
	       c_y = settings->julia_centre.y;


	size_t i = 0;
	double c = blx + (x / (settings->width / (trx-blx))),
	       d = try - (y / ((settings->width * ratio) / (try-bly))),
	       a = c,
	       b = d,
	       a2 = a * a,
	       b2 = b * b,
	       temp;

	double cdot = a2 + b2;
	bool not_in_main_bulb = (
		   (256.0 * cdot * cdot - 96.0 * cdot + 32.0 * a - 3.0 >= 0.0)
	  && (16.0 * (cdot + 2.0 * a + 1.0) - 1.0 >= 0.0)
	);
	if (!not_in_main_bulb && settings->fractal_type == Mandelbrot) { // wow look at that for loop go!!
		i = settings->iterations;
	}
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
	} else if (settings->smooth) {
		/* http://csharphelper.com/blog/2014/07/draw-a-mandelbrot-set-fractal-with-smoothly-shaded-colors-in-c/ */

		/* iterate z 3 more times to get smoother colouring */
		for (int j = 0; j < 3; j++) {
			temp = a2 - b2 + (settings->fractal_type == Julia ? c_x : c);
			b = ((a + a) * b) + (settings->fractal_type == Julia ? c_y : d);
			a = temp;
			a2 = a * a;
			b2 = b * b;
		}

		/* computer float estimate of escape iterations */
		double mu = i + 1.0 - log(log(sqrt(a2 + b2))) / log(2);
		if (mu < 0) {
			mu = 0.0;
		}

		/* interpolate between colours */
		size_t colour_one = (size_t) mu;
		double t2 = mu - colour_one;
		double t1 = 1 - t2;
		colour_one %= settings->colourmap->size;
		size_t colour_two = (colour_one + 1) % settings->colourmap->size;

		Pixel colour = {
			.red = (
				settings->colourmap->colours[colour_one].red * t1
			+ settings->colourmap->colours[colour_two].red * t2
			),
			.green = (
				settings->colourmap->colours[colour_one].green * t1
			+ settings->colourmap->colours[colour_two].green * t2
			),
			.blue = (
				settings->colourmap->colours[colour_one].blue * t1
			+ settings->colourmap->colours[colour_two].blue * t2
			),
			.alpha = UINT16_MAX,
		};

		memcpy(pixel, &colour, sizeof(Pixel));
	} else {
		memcpy(pixel, &settings->colourmap->colours[i % settings->colourmap->size], sizeof(Pixel));
	}
}
