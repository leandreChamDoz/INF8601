/*
 i* dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

extern "C"
{
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"

using namespace std;
using namespace tbb;

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
	DragonDraw(draw_data *draw_data)
	{
		this->_draw_data = draw_data;
	}

	DragonDraw(const DragonDraw &d)
	{
		this->_draw_data = d._draw_data;
	}

	void operator()(const blocked_range<uint64_t> &range) const
	{
		for (size_t i = 0; i < NB_TILES; i++)
			dragon_draw_raw(i,
							range.begin(),
							range.end(),
							this->_draw_data->dragon,
							this->_draw_data->dragon_width,
							this->_draw_data->dragon_height,
							this->_draw_data->limits,
							range.begin() * this->_draw_data->nb_thread / this->_draw_data->size);
	}

  private:
	draw_data *_draw_data;
};

class DragonRender
{
  public:
	DragonRender(draw_data *draw_data)
	{
		this->_draw_data = draw_data;
	}

	DragonRender(const DragonRender &d)
	{
		this->_draw_data = d._draw_data;
	}

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
	DragonClear(draw_data *draw_data)
	{
		this->_draw_data = draw_data;
	}

	DragonClear(const DragonClear &d)
	{
		this->_draw_data = d._draw_data;
	}

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
	DragonDraw dragon_draw(&data);
	parallel_for(blocked_range<uint64_t>(0, data.size), dragon_draw);

	/* 4. Effectuer le rendu final */
	DragonRender dragon_render(&data);
	parallel_for(blocked_range<uint64_t>(0, data.image_height), dragon_render);

	free_palette(palette);
	FREE(data.tid);
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
