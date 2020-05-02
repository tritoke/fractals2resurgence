#include <stdint.h>

/* Definition of a pixel value in farbfeld image spec */
typedef struct {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
	uint16_t alpha;
} Pixel;

typedef struct {
	size_t size;
	Pixel * colours;
} Cmap;

Cmap * read_map(const char *);
void free_cmap(Cmap *);
Cmap * gen_random_map(size_t size);
