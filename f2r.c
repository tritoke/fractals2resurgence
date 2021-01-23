#include <arpa/inet.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmap.h"

/* little macro for printing booleans as strings */
#define BOOL2STR(x) ((x) ? "true" : "false")

/* enum representing the different types of fractals available */
enum Fractal {Julia, Mandelbrot};

typedef struct {
	double x;
	double y;
} Point;

#include "defaults.h"

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

struct thread_arg {
	atomic_uint_fast32_t * const next_row;
	Pixel ** rows;
	pthread_mutex_t * const row_mtx;
	const struct settings * const settings;
};

struct writer_arg {
	const struct settings * const settings;
	atomic_uint_fast32_t * const next_row;
	Pixel ** rows;
	pthread_mutex_t * const row_mtx;
	FILE * outfile;
	bool in_order_write;
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
static void die(const char *, ...);

int main(int argc, char * argv[]) {
	 /***********************************
	 *   ____  _____ _____ _   _ ____   *
	 *  / ___|| ____|_   _| | | |  _ \  *
	 *  \___ \|  _|   | | | | | | |_) | *
	 *   ___) | |___  | | | |_| |  __/  *
	 *  |____/|_____| |_|  \___/|_|     *
	 *                                  *
	 ************************************/

	/* set up the user options struct with the defaults from defaults.h */
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
	bool in_order_write = false;
	if (strlen(uo.outfile) == 1 && uo.outfile[0] == '-') {
		/* write to stdout */
		fp = stdout;
		in_order_write = true;
	} else {
		/* open the specified output file */
		fp = fopen(uo.outfile, "w");
		if (fp == NULL) die("Failed to open outfile: \"%s\", exiting.\n", uo.outfile);
	}

	/* if in verbose mode print the render settings to stderr */
	if (uo.verbose) {
		fprintf(stderr, "Render Settings:\n");
		fprintf(stderr, "\tthreads: %d\n", uo.threads);
		fprintf(stderr, "\twidth: %d\n", uo.width);
		fprintf(stderr, "\theight: %d\n", (uint32_t) (uo.width * uo.ratio));
		fprintf(stderr, "\titerations: %ld\n", uo.iterations);
		fprintf(stderr, "\tbottom_left: %f,%f\n", bottom_left.x, bottom_left.y);
		fprintf(stderr, "\ttop_right: %f,%f\n", top_right.x, top_right.y);
		fprintf(stderr, "\tjulia_centre: %f,%f\n", uo.julia_centre.x, uo.julia_centre.y);
		fprintf(stderr, "\tfractal_type: %d\n", uo.fractal_type);
		fprintf(stderr, "\tcolourmap: %s\n", uo.mapfile);
		fprintf(stderr, "\tverbose: %s\n", BOOL2STR(uo.verbose));
		fprintf(stderr, "\tsmooth: %s\n", BOOL2STR(uo.smooth));
	}

	/* setup the actual settings passed to the renderer */
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
	};

	 /********************************
	 * __        _____  ____  _  __  *
	 * \ \      / / _ \|  _ \| |/ /  *
	 *  \ \ /\ / / | | | |_) | ' /   *
	 *   \ V  V /| |_| |  _ <| . \   *
	 *    \_/\_/  \___/|_| \_\_|\_\  *
	 *                               *
	 ********************************/


	const struct ff_header header = {
		.magic = "farbfeld",
		.width = htonl(settings.width),
		.height = htonl(settings.height)
	};

	fwrite(&header, sizeof(struct ff_header), 1, fp);

	/* setup for starting the threads */
	atomic_uint_fast32_t next_row = 0;
	pthread_t tids[uo.threads],
	          writer_tid;

	/* set up the thread arguments */
	Pixel ** rows = calloc(settings.height, sizeof(Pixel *)); /* free'd by writer_thread on exit */
	pthread_mutex_t row_mtx = PTHREAD_MUTEX_INITIALIZER;

	struct thread_arg targ = {
		.next_row = &next_row,
		.settings = &settings,
		.rows = rows,
		.row_mtx = &row_mtx,
	};

	struct writer_arg warg = {
		.settings = &settings,
		.next_row = &next_row,
		.rows = rows,
		.row_mtx = &row_mtx,
		.outfile = fp,
		.in_order_write = in_order_write,
	};

	/* start the renderer threads */
	for (uint32_t i = 0; i < uo.threads; i++) {
		if (pthread_create(&tids[i], NULL, rowrenderer, &targ)) {
			die("error creating thread %d\n", i);
		} else if (settings.verbose) {
			fprintf(stderr, "[thread]\t%d\tcreated\n", i);
		}
	}

	/* Start the writer thread */
	if (pthread_create(&writer_tid, NULL, writer_thread, &warg)) {
		die("Error creating writer thread\n")
	} else if (settings.verbose) {
		fputs("[writer]\t\tcreated\n", stderr);
	}

	/* join render threads */
	for (uint32_t i = 0; i < uo.threads; i++) {
		if (pthread_join(tids[i], NULL)) {
			die("failed to join thread %d\n", i);
		} else if (settings.verbose) {
			fprintf(stderr, "[thread]\t%d\tjoined\n", i);
		}
	}

	/*****************************************************
	*    _____ ___ _   _    _    _     ___ ____  _____   *
	*   |  ___|_ _| \ | |  / \  | |   |_ _/ ___|| ____|  *
	*   | |_   | ||  \| | / _ \ | |    | |\___ \|  _|    *
	*   |  _|  | || |\  |/ ___ \| |___ | | ___) | |___   *
	*   |_|   |___|_| \_/_/   \_\_____|___|____/|_____|  *
	*                                                    *
	*****************************************************/

	/* join writer thread */
	if (pthread_join(writer_tid, NULL)) {
		die("Failed to join writer thread\n");
	} else if (settings.verbose) {
		fputs("[writer]\t\tjoined\n", stderr);
	}

	if (settings.verbose) fputs("[main]\t\tfreeing colourmap\n", stderr);
	free_cmap(settings.colourmap);

	if (settings.verbose) fputs("[main]\t\tclosing file\n", stderr);
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

	const struct thread_arg * const arg = (struct thread_arg *) varg;
	const struct settings * const settings = arg->settings;
	Pixel ** rows = arg->rows;

	uint32_t curr_row;

	/* get the current row to render */
	curr_row = (*arg->next_row)++;

	while (curr_row < settings->height) {
		/* Allocate the space for the current row */
		Pixel * row = malloc(settings->width * sizeof(Pixel));

		/* Colour each pixel in the row */
		for (uint32_t x = 0; x < settings->width; x++) {
			colour(x, curr_row, &row[x], settings);
		}

		/* write the pointer to the row out to be written to disk */
		if (pthread_mutex_lock(arg->row_mtx) != 0) die("Failed to acquire row mutex, exiting");
		
		rows[curr_row] = row;

		if (pthread_mutex_unlock(arg->row_mtx) != 0) die("Failed to release row mutex, exiting");

		/* Get the next row of the image to render */
		curr_row = (*arg->next_row)++;
	}

	return NULL;
}

static void * writer_thread(void * varg) {
	/* Takes rows from the result queue and writes them out */

	const struct writer_arg * arg = (struct writer_arg *) varg;
	const struct settings * settings = arg->settings;

	/* sentinel value for signalling a row has been written */
	Pixel * const sentinel_value = (Pixel *) 0xDEADBEEFBEEFDEAD;

	uint32_t min_unwritten_row = 0;
	Pixel ** rows = arg->rows;

	while (min_unwritten_row < settings->height) {
		uint32_t row_to_write = min_unwritten_row;
		/* search for rows to write */
		Pixel * row = NULL;

		if (pthread_mutex_lock(arg->row_mtx) != 0) die("Failed to acquire row mutex, exiting");

		if (arg->in_order_write) {
			/* if we have to write in-order then we have to wait for the next row... */
			row = rows[row_to_write];
		} else {
			/* if we can write in whatever order we want then we can search for unwritten rows */
			const uint32_t limit = *arg->next_row;
			for (; row_to_write < limit; row_to_write++) {
				row = rows[row_to_write];

				/* we wrote this already */
				if (row == sentinel_value && row_to_write == min_unwritten_row) {
					min_unwritten_row++;
					continue;
				}

				/* there is a row ready here */
				if (row != NULL && row != sentinel_value) break;
			}
		}

		if (pthread_mutex_lock(arg->row_mtx) != 0) die("Failed to release row mutex, exiting");

		/* if there are no rows to write then just continue */
		if (row == NULL) continue;

		/* if writing out of order - seek to the right place in the file first */
		if (!arg->in_order_write)
			fseek(arg->outfile, sizeof(struct ff_header) + row_to_write * (settings->width * sizeof(Pixel)), SEEK_SET);

		/* write out the row */
		fwrite(row, sizeof(Pixel), settings->width, arg->outfile);

		/* free the row */
		free(row);
		
		/* write back sentinel value if we're writing out of order */
		if (pthread_mutex_lock(arg->row_mtx) != 0) die("Failed to acquire row mutex, exiting");

		if (!arg->in_order_write)
			rows[row_to_write] = sentinel_value;

		if (pthread_mutex_lock(arg->row_mtx) != 0) die("Failed to release row mutex, exiting");

		/* Update state */
		if (row_to_write == min_unwritten_row) {
			min_unwritten_row++;
		}
	}

	/* free the space used to store the pointers to the rows */
	free(rows);

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
			i++;
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

		Pixel c1 = settings->colourmap->colours[colour_one];
		Pixel c2 = settings->colourmap->colours[colour_two];

		Pixel colour = {
			.red   = c1.red   * t1 + c2.red   * t2,
			.green = c1.green * t1 + c2.green * t2,
			.blue  = c1.blue  * t1 + c2.blue  * t2,
			.alpha = UINT16_MAX,
		};

		memcpy(pixel, &colour, sizeof(Pixel));
	} else {
		memcpy(pixel, &settings->colourmap->colours[i % settings->colourmap->size], sizeof(Pixel));
	}
}

static void die(const char * fmt, ...) {
	va_list vargs;
	va_start(vargs, fmt);

	vfprintf(stderr, fmt, vargs);

	va_end(vargs);

	exit(EXIT_FAILURE);
}
