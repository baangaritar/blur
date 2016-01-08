#include <ctype.h>
#include <libgen.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <sys/resource.h>  // rusage

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "blur.h"
#include "args.h"
#include "helpers.h"


int main(int argc, char **argv)
{
    /* Command-line argument default values */
    char *filenameIn = NULL;
    char *filenameOut = NULL;
    char *filenameOutDyn = NULL;
    int x = 0,
        y = 0,
        size = 10,
        iterations = 1,
        kernelSize = 5,
        interactive = false;

    if (!parseArgs(argc, argv, &filenameIn, &filenameOut,
                   &x, &y, &size, &iterations, &kernelSize,
                   &interactive))
    {
        return 1;
    }

    if (size < kernelSize)
    {
        printf("Size too small.\n");
        return 1;
    }

    /* Generate a filename */
    if (filenameOut == NULL)
    {
        /* No path given, append suffix to input */
        const char *base = basename(filenameIn);
        const char *suffix = "_out.png";
        filenameOutDyn = (char *) calloc(strlen(base)
                         + sizeof(suffix)
                         + 1, sizeof(char));
        strncpy(filenameOutDyn, base, strlen(base));
        const char *dot = strrchr(filenameOutDyn, '.');
        filenameOutDyn[strlen(filenameOutDyn) - strlen(dot)] = '\0';
        strcat(filenameOutDyn, suffix);
    }
    else
    {
        /* Path given, copy value into new array (so we can free it) */
        filenameOutDyn = (char *) calloc(
            strlen(filenameOut) + 1, sizeof(char));
        strncpy(filenameOutDyn, filenameOut, strlen(filenameOut));
    }


    /* Load an image into memory, and set aside memory for result */
    int width, height, comp;
    uint8_t *pixelsIn = stbi_load(filenameIn, &width, &height, &comp, 0);
    uint8_t *pixelsOut = (uint8_t *) malloc(
        height * width * comp * sizeof(uint8_t *));
    memcpy(pixelsOut, pixelsIn, height * width * comp * sizeof(uint8_t));

    if (pixelsIn == NULL)
    {
        printf("Could not load %s.\n", filenameIn);
        free(pixelsOut);
        return 1;
    }

    if (pixelsOut == NULL)
    {
        printf("Could not allocate enough memory.\n");
        free(pixelsIn);
        return 1;
    }

    /* Clamp x and y to available space */
    x = x > width ? width : x;
    y = y > height ? height : y;

    double *kernel = (double *) malloc(kernelSize * kernelSize * sizeof(double));
    computeKernel(kernel, kernelSize, true /* normalised */);

    struct rusage before, after;
    double time_convolve = 0.0;

    /* Extract an area from source image for convolution */
    int areaSize = size * size * comp * sizeof(uint8_t);
    uint8_t *areaIn = (uint8_t *) malloc(areaSize);
    uint8_t *areaOut = (uint8_t *) malloc(areaSize);

    /* Generate mask with which to multiply the effect */
    double *mask = (double *) malloc(kernelSize
                                     * kernelSize  
                                     * sizeof(double));
    computeRamp(mask, kernelSize);

    if (areaIn == NULL)
    {
        printf("Could not allocate enough memory.\n");
        free(pixelsIn);
        free(pixelsOut);
        return 1;
    }

    if (areaOut == NULL)
    {
        printf("Could not allocate enough memory.\n");
        free(areaIn);
        free(pixelsIn);
        free(pixelsOut);
        return 1;
    }

    if (mask == NULL)
    {
        printf("Could not allocate enough memory.\n");
        free(areaIn);
        free(areaOut);
        free(pixelsIn);
        free(pixelsOut);
        return 1;
    }

    if (extract(pixelsIn, areaIn, x, y, width, height, size, size, comp) == 0)
    {
        /* Convole */
        getrusage(RUSAGE_SELF, &before); // Debugging
        convolve(size,       // width 
                 size,       // height
                 x,          // minX
                 y,          // minY
                 width,      // maxWidth
                 height,     // maxHeight
                 comp,       // components
                 areaIn,     // in
                 areaOut,    // out
                 kernel,     // kernel
                 kernelSize, // kernelSize
                 mask        // mask
        );
        getrusage(RUSAGE_SELF, &after);  // Debugging
        time_convolve = calculate(&before, &after);

        /* Integrate extracted area back into source image */
        if (integrate(areaOut, pixelsOut, x, y, size,
                      size, width, height, comp) == 0)
        {
            if (stbi_write_png(filenameOutDyn, width,
                               height, comp, pixelsOut, 0) == 0)
            {
                printf("Could not write \"%s\"\n", filenameOutDyn);
            }
            else
            {
                printf("Wrote: %s (%ix%ix%i) " \
                                 "(x=%i, y=%i, size=%i) " \
                                 "to %s " \
                                 "in %.3fs\n",
                    filenameIn, height, width, x, y, size, comp,
                    filenameOut, time_convolve);
            }
        }
        else
        {
            printf("Could not integrate.\n");
        }
    }
    else
    {
        printf("Could not extract.\n");
    }

    free(pixelsIn);
    free(pixelsOut);
    free(areaIn);
    free(areaOut);
    free(filenameOutDyn);

    return 0;
}
