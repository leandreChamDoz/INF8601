/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "dragon.h"
#include "color.h"
#include "dragon_pthread.h"

#define PRINT_PTHREAD_ERROR(err, msg) \
	do { errno = err; perror(msg); } while(0)

pthread_mutex_t mutex_stdout;

void printf_threadsafe(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

/**
 * Does the work on a part of the dragon.
 * Returns 0 on success.
 */
void* dragon_draw_worker(void *data)
{
	struct draw_data* worker_data = (struct draw_data*) data;

	/* 1. Initialiser la surface */
	int stepZone = worker_data->dragon_height * worker_data->dragon_width / worker_data->nb_thread;
	int startZone = worker_data->id * stepZone;
	int endZone;

	if (worker_data->id == (worker_data->nb_thread - 1))
		endZone = worker_data->dragon_height * worker_data->dragon_width;
	else
		endZone = stepZone * (worker_data->id + 1);

	init_canvas(startZone, endZone, worker_data->dragon, -1);

	pthread_barrier_wait(worker_data->barrier);

	/* 2. Dessiner les dragons dans les 4 directions
	 *
	 * Il est attendu que chaque threads dessine une partie
	 * de chaque dragon.
	 * */
	int stepDepth = worker_data->size / worker_data->nb_thread;
	int start = stepDepth * worker_data->id;
	int end;

	if (worker_data->id == (worker_data->nb_thread - 1))
		end = worker_data->size;
	else
		end = stepDepth * (worker_data->id + 1);

	//Décommenter pour la partie 3
	//printf_threadsafe("THREAD #%d (Range : %d - %d, Real TID : %d)\n", worker_data->id, start, end, gettid());

	for(int i = 0; i < NB_TILES; i++) {
		dragon_draw_raw(i, start, end, worker_data->dragon, 
						worker_data->dragon_width, worker_data->dragon_height, worker_data->limits, worker_data->id);
	}

	pthread_barrier_wait(worker_data->barrier);

	/* 3. Effectuer le rendu final */
	int stepImage = worker_data->image_height / worker_data->nb_thread;
	int startImage = worker_data->id * stepImage;
	int endImage;

	if (worker_data->id == (worker_data->nb_thread - 1))
		endImage = worker_data->image_height;
	else
		endImage = stepImage * (worker_data->id + 1);
	
	scale_dragon(startImage, endImage, worker_data->image, 
				 worker_data->image_width, worker_data->image_height, worker_data->dragon,
				 worker_data->dragon_width, worker_data->dragon_height, worker_data->palette);

	return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	limits_t lim;
	struct draw_data info;
	char *dragon = NULL;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;

	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	/* 1. Initialiser barrier. */

	if (pthread_barrier_init(&barrier, NULL, nb_thread)) {
		printf("erreur lors de la création de la barrier");
		goto err;
	}

	if (dragon_limits_pthread(&lim, size, nb_thread) < 0)
		goto err;

	info.dragon_width = lim.maximums.x - lim.minimums.x;
	info.dragon_height = lim.maximums.y - lim.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height)) == NULL) {
		printf("malloc error dragon\n");
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = lim;
	info.barrier = &barrier;
	info.palette = palette;

	for (int i = 0; i < nb_thread; i++) {
		data[i] = info;
		data[i].id = i;
	}

	//Décommenter pour la partie 3
	//printf("-----PThread Stats Start-----\n");

	/* 2. Lancement du calcul parallèle principal avec dragon_draw_worker */
	for(int i = 0; i < nb_thread; i++) {
		if(pthread_create(&threads[i], NULL, dragon_draw_worker, (void*) &data[i])) {
			printf("erreur lors de la creation des threads\n");
			goto err;
		}
	}

	/* 3. Attendre la fin du traitement */
	for(int i = 0; i < nb_thread; i++) {
		if(pthread_join(threads[i], NULL)) {
			printf("erreur lors de l'attente des threads\n");
			goto err;
		}
	}

	//Décommenter pour la partie 3
	//printf("-----PThread Stats End-----\n");

	/* 4. Destruction des variables. */
	if(pthread_barrier_destroy(&barrier)) {
		printf("erreur lors de la destruction de la barrier");
		goto err;
	}

done:
	FREE(data);
	FREE(threads);
	free_palette(palette);
	*canvas = dragon;
	return ret;

err:
	FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data)
{
	int i;
	struct limit_data *lim = (struct limit_data *) data;
	int start = lim->start;
	int end = lim->end;

	for (i = 0; i < NB_TILES; i++) {
		piece_limit(start, end, &lim->pieces[i]);
	}

	return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread)
{
	int ret = 0;
	int i;
	pthread_t *threads = NULL;
	struct limit_data *thread_data = NULL;
	piece_t masters[NB_TILES];

	for (i = 0; i < NB_TILES; i++) {
		/**
		 * La pièce master représente les limites d'un dragon complet.
		 * Notez bien que chaque dragon à une orientation différente.
		 */
		piece_init(&masters[i]);
		masters[i].orientation = tiles_orientation[i];
	}

	/* 1. Allouer de l'espace pour threads et threads_data. */
	threads = malloc(sizeof(pthread_t) * nb_thread);
	thread_data = malloc(sizeof(struct limit_data) * nb_thread);

	for (int i = 0; i < nb_thread; i++) {
		for (int j = 0; j < NB_TILES; j++) {
			piece_init(&thread_data[i].pieces[j]);
			thread_data[i].pieces[j].orientation = tiles_orientation[j];
			thread_data[i].id = i;
		}
	}

	if (nb_thread >= size)
		nb_thread = size;

	int step = size / nb_thread;

	/* 2. Lancement du calcul en parallèle avec dragon_limit_worker. */
	for (int i = 0; i < (nb_thread - 1); i++) {
		thread_data[i].start = i * step;
		thread_data[i].end = (i + 1) * step;

		if(pthread_create(&threads[i], NULL, dragon_limit_worker, (void*) &thread_data[i])) {
			printf("erreur lors de la creation des threads\n");
			goto err;
		}
	}

	thread_data[nb_thread - 1].start = (nb_thread - 1) * step;
	thread_data[nb_thread - 1].end = size;

	if(pthread_create(&threads[nb_thread - 1], NULL, dragon_limit_worker, (void*) &thread_data[nb_thread - 1])) {
		printf("erreur lors de la creation des threads\n");
		goto err;
	}

	/* 3. Attendre la fin du traitement. */
	for (int i = 0 ; i < nb_thread; i++) {
		if(pthread_join(threads[i], NULL)) {
			printf("erreur lors de l'attente des threads\n");
			goto err;
		}
	}

	/* 4. Fusion des pièces.
	 *
	 * La fonction piece_merge est disponible afin d'accomplir ceci.
	 * Notez bien que les pièces ayant la même orientation initiale
	 * doivent être fusionnées ensemble.
	 * */

	for (int i = 0 ; i < nb_thread; i++) {
		for (int j = 0 ; j < NB_TILES; j++) {
			piece_merge(&masters[j], thread_data[i].pieces[j], tiles_orientation[j]);
		}
	}

	/* La limite globale est calculée à partir des limites
	 * de chaque dragon calculées à l'étape 4.
	 */

	merge_limits(&masters[0].limits, &masters[1].limits);
	merge_limits(&masters[0].limits, &masters[2].limits);
	merge_limits(&masters[0].limits, &masters[3].limits);

done:
	FREE(threads);
	FREE(thread_data);
	*limits = masters[0].limits;
	return ret;
err:
	ret = -1;
	goto done;
}
