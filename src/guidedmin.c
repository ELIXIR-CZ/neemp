/* Copyright 2013-2016 Jana Pazurikova (pazurikova@ics.muni.cz)
 *
 * This file is part of NEEMP.
 *
 * NEEMP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NEEMP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NEEMP. If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "omp.h"
#include "eem.h"
#include "kappa.h"
#include "neemp.h"
#include "parameters.h"
#include "settings.h"
#include "subset.h"
#include "statistics.h"
#include "structures.h"
#include "../externals/lhs/latin_random.h"
#include "guidedmin.h"

extern const struct training_set ts;
extern const struct settings s;
extern const float ionenergies[];
extern const float affinities[];


/* Run guided minimization algorithm to find the best set of parameters for calculation of partial charges. */ 
void run_guided_min(struct subset * const ss) {

	assert(ss != NULL);

	/* Create a set of random points in vector space of kappa_data */            
	if (s.verbosity >= VERBOSE_KAPPA)
		printf("GM Generating %d vectors\n", s.population_size);

	/* Set bounds for each parameters in kappa_data */
	float *bounds = (float *) malloc((ts.atom_types_count * 2 + 1) * 2 * sizeof (float));

	/* Compute bounds, 0 means set them to fixed numbers taken from Tomas's full scan, 1 means try to find them with broad search */
	compute_parameters_bounds(bounds, 0);

	/* Generate population */
	fill_ss(ss, s.population_size); 
	generate_random_population(ss, bounds, s.population_size);
	de_ss = ss;

	/* Evaluate the fitness function for all points */
	if (s.verbosity >= VERBOSE_KAPPA)
		printf("GM Calculating charges and evaluating fitness function for whole set\n");

	int i = 0;
	#pragma omp parallel for num_threads(s.om_threads) default(shared) private(i)
	for (i = 0; i < ss->kappa_data_count; i++) {
		calculate_charges(ss, &ss->data[i]);
		calculate_statistics(ss, &ss->data[i]);
	}

	/* Minimize part of population that has R > 0.3 */
	int minimized_initial = 0;
	if (s.verbosity >= VERBOSE_KAPPA)
		printf("GM minimizing population\n");
	minimized_initial = minimize_part_of_gm_set(ss, s.gm_iterations_beg);
	
	/* If we minimized zero data, inform user and suggest larger set */
	if (minimized_initial == 0) {
		EXIT_ERROR(RUN_ERROR, "No vector in set was worth minimizing. Please choose larger set than %d (option --om-pop-size)\n.", s.population_size);
	}

	/* Find the best kappa_data */
	set_the_best(ss);

	struct kappa_data *so_far_best = (struct kappa_data *) malloc(sizeof(struct kappa_data));
	kd_init(so_far_best);

	/* Copy ss->best into so_far_best */
	kd_copy_parameters(ss->best, so_far_best);
	calculate_charges(ss, so_far_best);
	calculate_statistics(ss, so_far_best);

	/* Minimize the result */
	minimize_locally(so_far_best, s.gm_iterations_end);

	/* Tidying up and printing */
	free(bounds);
	kd_copy_parameters(so_far_best, ss->best);
	calculate_charges(ss, ss->best);
	calculate_statistics(ss, ss->best);
	kd_destroy(so_far_best);
	free(so_far_best);
}

/* Run local minimization on part of population */
int minimize_part_of_gm_set(struct subset* ss, int min_iterations) {

	assert(ss != NULL);

	int quite_good = 0;
	int i = 0;
	/* We minimize all with R2 > 0.2 && R > 0 */
	#pragma omp parallel for num_threads(s.om_threads) shared(ss, quite_good) private(i)
	for (i = 0; i < ss->kappa_data_count; i++) {
		if (ss->data[i].full_stats.R2 > 0.2 && ss->data[i].full_stats.R > 0) {
			#pragma omp critical
			{
				quite_good++;
			}
			struct kappa_data* m = (struct kappa_data *) malloc (sizeof(struct kappa_data));
			kd_init(m);
			m->parent_subset = ss;
			kd_copy_parameters(&ss->data[i], m);
			minimize_locally(m, min_iterations);
			kd_copy_parameters(m, &ss->data[i]);
			kd_destroy(m);
			free(m);

		}
	}
	if (s.verbosity >= VERBOSE_KAPPA) {
		printf("Out of %d in population, we minimized %d\n", ss->kappa_data_count, quite_good);
	}
	return quite_good;
}
