/*
 * sinoscope_openmp.c
 *
 *  Created on: 2011-10-14
 *      Author: francis
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "sinoscope.h"
#include "color.h"
#include "util.h"

int sinoscope_image_openmp(sinoscope_t *ptr)
{
    if (ptr == NULL)
        return -1;

    sinoscope_t sino = *ptr;
    int x, y, index, taylor;
    struct rgb c;
    float val, px, py;
    unsigned char *buffer = sino.buf;
    float dx = sino.dx;
    float dy = sino.dy;
    int width = sino.width;
    int height = sino.height;
    int t_limit = sino.taylor;
    float phase0 = sino.phase0;
    float phase1 = sino.phase1;
    float t = sino.time;
    float interval = sino.interval;
    float interval_inv = sino.interval_inv;

    for (x = 1; x < width - 1; x++)
        #pragma omp parallel for private(px, py, c, val, taylor, index)
        for (y = 1; y < height - 1; y++)
        {
            px = dx * y - 2 * M_PI;
            py = dy * x - 2 * M_PI;
            val = 0.0f;

            for (taylor = 1; taylor <= t_limit; taylor += 2)
                val += sin(px * taylor * phase1 + t) / taylor + cos(py * taylor * phase0) / taylor;

            val = (atan(1.0 * val) - atan(-1.0 * val)) / (M_PI);
            val = (val + 1) * 100;
            value_color(&c, val, interval, interval_inv);
            index = (y * 3) + (x * 3) * width;
            buffer[index + 0] = c.r;
            buffer[index + 1] = c.g;
            buffer[index + 2] = c.b;
        }
    return 0;
}
