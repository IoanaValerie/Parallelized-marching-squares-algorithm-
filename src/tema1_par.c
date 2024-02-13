// Author: APD team, except where source was noted

#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#define CONTOUR_CONFIG_COUNT    16
#define FILENAME_MAX_SIZE       50
#define STEP                    8
#define SIGMA                   200
#define RESCALE_X               2048
#define RESCALE_Y               2048

#define CLAMP(v, min, max) if(v < min) { v = min; } else if(v > max) { v = max; }

// Store data for threads
typedef struct {
    int P;
    int id;
    ppm_image *image;
    unsigned char **grid;
    ppm_image **contour_map;
    ppm_image *scaled_image;
    pthread_barrier_t *barrier;
    int ok;
} ThreadData;

// Creates a map between the binary configuration (e.g. 0110_2) and the corresponding pixels
// that need to be set on the output image. An array is used for this map since the keys are
// binary numbers in 0-15. Contour images are located in the './contours' directory.
ppm_image **init_contour_map() {
    ppm_image **map = (ppm_image **)malloc(CONTOUR_CONFIG_COUNT * sizeof(ppm_image *));
    if (!map) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        char filename[FILENAME_MAX_SIZE];
        sprintf(filename, "./contours/%d.ppm", i);
        map[i] = read_ppm(filename);
    }

    return map;
}

// Updates a particular section of an image with the corresponding contour pixels.
// Used to create the complete contour image.
void update_image(ppm_image *image, ppm_image *contour, int x, int y) {
    for (int i = 0; i < contour->x; i++) {
        for (int j = 0; j < contour->y; j++) {
            int contour_pixel_index = contour->x * i + j;
            int image_pixel_index = (x + i) * image->y + y + j;

            image->data[image_pixel_index].red = contour->data[contour_pixel_index].red;
            image->data[image_pixel_index].green = contour->data[contour_pixel_index].green;
            image->data[image_pixel_index].blue = contour->data[contour_pixel_index].blue;
        }
    }
}

// Parallelize rescale, sample_grid and march
void *thread_function(void *arg) {
    // Extract data from struct ThreadData
    ThreadData* data = (ThreadData *)arg;
    int P = data->P;
    int thread_id = data->id;
    ppm_image *scaled_image = data->scaled_image;
    unsigned char **grid = data->grid;
    ppm_image **contour_map = data->contour_map;
    ppm_image *image = data->image;
    int ok = data->ok;
    pthread_barrier_t *barrier = data->barrier;

    // Declare the start/end for each parallelization
    int start, end;
    int startp, startq, endp, endq;

    // 1. Rescale the image(parallelized)
    if(ok == 1) {
        // Calculate start/end for each thread 
        start = thread_id * (double)scaled_image->x / P;
	    end = (((thread_id + 1) * (double)scaled_image->x / P) < scaled_image->x) ? ((thread_id + 1) * (double)scaled_image->x / P) : scaled_image->x;

        uint8_t sample[3];

        // Use bicubic interpolation for scaling
        for (int i = start; i < end; i++) {
            for (int j = 0; j < scaled_image->y; j++) {
                float u = (float)i / (float)(scaled_image->x - 1);
                float v = (float)j / (float)(scaled_image->y - 1);
                sample_bicubic(image, u, v, sample);

                scaled_image->data[i * scaled_image->y + j].red = sample[0];
                scaled_image->data[i * scaled_image->y + j].green = sample[1];
                scaled_image->data[i * scaled_image->y + j].blue = sample[2];
            }
        }
        pthread_barrier_wait(barrier);
    }

    int p = scaled_image->x / STEP;
    int q = scaled_image->y / STEP;

    // Calculate start/end for each thread 
    startq = thread_id * (double)q / P;
	endq = (((thread_id + 1) * (double)q / P) < q) ? ((thread_id + 1) * (double)q / P) : q;

    startp = thread_id * (double)p / P;
	endp = (((thread_id + 1) * (double)p / P) < p) ? ((thread_id + 1) * (double)p / P) : p;

    // 2. Sample the grid(parallelized)
    for (int i = 0; i < p; i++) {
        for (int j = startq; j < endq; j++) {
            ppm_pixel curr_pixel = scaled_image->data[i * STEP * scaled_image->y + j * STEP];

            unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

            if (curr_color > SIGMA) {
                grid[i][j] = 0;
            } else {
                grid[i][j] = 1;
            }
        }
        pthread_barrier_wait(barrier);

    }

    // last sample points have no neighbors below / to the right, so we use pixels on the
    // last row / column of the input image for them
    for (int i = startp; i < endp; i++) {
        ppm_pixel curr_pixel = scaled_image->data[i * STEP * scaled_image->y +scaled_image->x - 1];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            grid[i][q] = 0;
        } else {
            grid[i][q] = 1;
        }
    }
    pthread_barrier_wait(barrier);

    for (int j = startq; j < endq; j++) {
        ppm_pixel curr_pixel = scaled_image->data[(scaled_image->x - 1) * scaled_image->y + j * STEP];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            grid[p][j] = 0;
        } else {
            grid[p][j] = 1;
        }
    }
    pthread_barrier_wait(barrier);

    // 3. March the squares(parallelized)
    for (int i = 0; i < p; i++) {
        for (int j = startq; j < endq; j++) {
            unsigned char k = 8 * grid[i][j] + 4 * grid[i][j + 1] + 2 * grid[i + 1][j + 1] + 1 * grid[i + 1][j];
            update_image(scaled_image, contour_map[k], i * STEP, j * STEP);
        }
        pthread_barrier_wait(barrier);
    }

    pthread_exit(NULL);
}

// Calls `free` method on the utilized resources.
void free_resources(ppm_image *orgimage, ppm_image *image, ppm_image **contour_map, unsigned char **grid, int step_x) {
    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        free(contour_map[i]->data);
        free(contour_map[i]);
    }
    free(contour_map);

    for (int i = 0; i <= image->x / step_x; i++) {
        free(grid[i]);
    }
    free(grid);

    free(image->data);
    free(image);

    if (!(image->x <= RESCALE_X && image->y <= RESCALE_Y)) {
        free(orgimage->data);
        free(orgimage);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ./tema1 <in_file> <out_file> <P>\n");
        return 1;
    }

    ppm_image *image = read_ppm(argv[1]);
    int p = image->x / STEP;
    int q = image->y / STEP;

    // 0. Initialize contour map
    ppm_image **contour_map = init_contour_map();

    // Verify if the image should be rescaled
    ppm_image *scaled_image;
    int ok = 1;

    // We only rescale downwards
    if (image->x <= RESCALE_X && image->y <= RESCALE_Y) {
        scaled_image = image;
        ok = 0;
    } else {
        // Alloc memory for the scaled image
        scaled_image = (ppm_image *)malloc(sizeof(ppm_image));
        if (!scaled_image) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
        scaled_image->x = RESCALE_X;
        scaled_image->y = RESCALE_Y;

        scaled_image->data = (ppm_pixel*)malloc(scaled_image->x * scaled_image->y * sizeof(ppm_pixel));
        if (!scaled_image) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
    }

    // Alloc memory for the grid
    unsigned char **grid = (unsigned char **)malloc((p + 1) * sizeof(unsigned char*));
    if (!grid) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i <= p; i++) {
        grid[i] = (unsigned char *)malloc((q + 1) * sizeof(unsigned char));
        if (!grid[i]) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
    }

    //Initialize data for the threads 
    int P = atoi(argv[3]);
	pthread_t tid[P];
	ThreadData thread_data[P];
    pthread_barrier_t barrier;
	pthread_barrier_init(&barrier, NULL, P);

    // Create threads
	for (int i = 0; i < P; i++) {
        thread_data[i].P = P;
		thread_data[i].id = i;
        thread_data[i].image = image;
        thread_data[i].grid = grid;
        thread_data[i].contour_map = contour_map;
        thread_data[i].scaled_image = scaled_image;
        thread_data[i].barrier = &barrier;
        thread_data[i].ok = ok;
		pthread_create(&tid[i], NULL, thread_function, (void*)&thread_data[i]);
	}

	// Join threads
	for (int i = 0; i < P; i++) {
		pthread_join(tid[i], NULL);
	}

    pthread_barrier_destroy(&barrier);

    // 4. Write output
    write_ppm(scaled_image, argv[2]);

    free_resources(image, scaled_image, contour_map, grid, STEP);

    return 0;
}