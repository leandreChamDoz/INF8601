/*
 i* dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

extern "C"
{
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"
#include <atomic>

using namespace std;
using namespace tbb;

std::atomic<int> counter;

#define PRINT_PTHREAD_ERROR(err, msg) \
	do { errno = err; perror(msg); } while(0)

class DragonLimits
{
  public:
	piece_t pieces[NB_TILES];

	DragonLimits()
	{
		for (size_t i = 0; i < NB_TILES; i++)
		{
			piece_init(&pieces[i]);
			pieces[i].orientation = tiles_orientation[i];
		}
	}

	DragonLimits(DragonLimits &d, split)
	{
		for (size_t i = 0; i < NB_TILES; i++)
		{
			piece_init(&pieces[i]);
			pieces[i].orientation = tiles_orientation[i];
		}
	}

	void operator()(const blocked_range<uint64_t> &range)
	{
		for (size_t i = 0; i < NB_TILES; i++)
			piece_limit(range.begin(), range.end(), &pieces[i]);
	}

	void join(DragonLimits &d)
	{
		for (size_t i = 0; i < NB_TILES; i++)
			piece_merge(&pieces[i], d.pieces[i], tiles_orientation[i]);
	}
};

class DragonDraw
{
  public:
	DragonDraw(const DragonDraw &d, TidMap *tidMap)
	{
		this->_draw_data = d._draw_data;
		this->_tidMap = tidMap;
	}
	DragonDraw(draw_data *draw_data, TidMap *tidMap)
	{
		this->_draw_data = draw_data;
		this->_tidMap = tidMap;
	}

	void operator()(const blocked_range<uint64_t> &range) const
	{
		this->_draw_data->id = _tidMap->getIdFromTid(gettid());

		counter++;

		xy_t position;
		xy_t orientation;
		uint64_t n;
		for (size_t k = 0; k < NB_TILES; k++)
		{
			position = compute_position(k, range.begin());
			orientation = compute_orientation(k, range.begin());
			position.x -= this->_draw_data->limits.minimums.x;
			position.y -= this->_draw_data->limits.minimums.y;

			for (n = range.begin() + 1; n <= range.end(); n++)
			{
				int j = (position.x + (position.x + orientation.x)) >> 1;
				int i = (position.y + (position.y + orientation.y)) >> 1;
				int index = i * this->_draw_data->dragon_width + j;

				this->_draw_data->dragon[index] = n * this->_draw_data->nb_thread / this->_draw_data->size;

				position.x += orientation.x;
				position.y += orientation.y;
				if (((n & -n) << 1) & n)
					rotate_left(&orientation);
				else
					rotate_right(&orientation);
			}
		}
	}

  private:
	TidMap *_tidMap;
	draw_data *_draw_data;
};

class DragonRender
{
  public:
	DragonRender(const DragonRender &d) { this->_draw_data = d._draw_data; }
	DragonRender(draw_data *draw_data) { this->_draw_data = draw_data; }

	void operator()(const blocked_range<uint64_t> &range) const
	{
		scale_dragon(range.begin(),
					 range.end(),
					 this->_draw_data->image,
					 this->_draw_data->image_width,
					 this->_draw_data->image_height,
					 this->_draw_data->dragon,
					 this->_draw_data->dragon_width,
					 this->_draw_data->dragon_height,
					 this->_draw_data->palette);
	}

  private:
	draw_data *_draw_data;
};

class DragonClear
{
  public:
	DragonClear(const DragonClear &d) { this->_draw_data = d._draw_data; }
	DragonClear(draw_data *draw_data) { this->_draw_data = draw_data; }

	void operator()(const blocked_range<uint64_t> &range) const
	{
		init_canvas(range.begin(), range.end(), this->_draw_data->dragon, -1);
	}

  private:
	draw_data *_draw_data;
};

int dragon_draw_tbb(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	struct draw_data data;
	limits_t limits;
	char *dragon = NULL;
	int dragon_width;
	int dragon_height;
	int dragon_surface;
	int scale_x;
	int scale_y;
	int scale;
	int deltaJ;
	int deltaI;
	struct palette *palette = init_palette(nb_thread);
	if (palette == NULL)
		return -1;

	/* 1. Calculer les limites du dragon */
	dragon_limits_tbb(&limits, size, nb_thread);
	task_scheduler_init init(nb_thread);

	dragon_width = limits.maximums.x - limits.minimums.x;
	dragon_height = limits.maximums.y - limits.minimums.y;
	dragon_surface = dragon_width * dragon_height;
	scale_x = dragon_width / width + 1;
	scale_y = dragon_height / height + 1;
	scale = (scale_x > scale_y ? scale_x : scale_y);
	deltaJ = (scale * width - dragon_width) / 2;
	deltaI = (scale * height - dragon_height) / 2;

	dragon = (char *)malloc(dragon_surface);
	if (dragon == NULL)
	{
		free_palette(palette);
		return -1;
	}

	data.nb_thread = nb_thread;
	data.dragon = dragon;
	data.image = image;
	data.size = size;
	data.image_height = height;
	data.image_width = width;
	data.dragon_width = dragon_width;
	data.dragon_height = dragon_height;
	data.limits = limits;
	data.scale = scale;
	data.deltaI = deltaI;
	data.deltaJ = deltaJ;
	data.palette = palette;
	data.tid = (int *)calloc(nb_thread, sizeof(int));

	/* 2. Initialiser la surface : DragonClear */
	DragonClear dragon_clear(&data);
	parallel_for(blocked_range<uint64_t>(0, dragon_surface), dragon_clear);

	/* 3. Dessiner le dragon : DragonDraw */

	counter = 0;
	TidMap *tidMap = new TidMap(nb_thread);

	DragonDraw dragon_draw(&data, tidMap);
	parallel_for(blocked_range<uint64_t>(0, data.size), dragon_draw);

	/* 4. Effectuer le rendu final */
	DragonRender dragon_render(&data);
	parallel_for(blocked_range<uint64_t>(0, data.image_height), dragon_render);

	//Décommenter pour la partie 3
	//cout << "Total intervals:\t" << counter << endl;

	free_palette(palette);
	FREE(data.tid);
	
	//Décommenter pour la partie 3
	//tidMap->dump();
	
	FREE(tidMap);
	*canvas = dragon;
	return 0;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_tbb(limits_t *limits, uint64_t size, int nb_thread)
{
	DragonLimits lim;

	/* 1. Calculer les limites */
	task_scheduler_init task(nb_thread);
	parallel_reduce(blocked_range<uint64_t>(0, size), lim);

	/* La limite globale est calculée à partir des limites
	 * de chaque dragon.
	 */
	merge_limits(&lim.pieces[0].limits, &lim.pieces[1].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[2].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[3].limits);

	*limits = lim.pieces[0].limits;
	return 0;
}
