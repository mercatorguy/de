// Copyright 2021 Charlie Shenton
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef DIFFERENTIAL_EVOLUTION_H
#define DIFFERENTIAL_EVOLUTION_H

#if (!defined(DE_ALLOC) && defined(DE_FREE)) || (defined(DE_ALLOC) && !defined(DE_FREE)) 
#error "Must define both or neither of DE_ALLOC and DE_FREE."
#endif

#ifndef DE_ALLOC
#define DE_ALLOC(sz) malloc(sz)
#define DE_FREE(p) free(p)
#endif

#include <stdint.h>

typedef struct de_settings
{
    int dimension_count;  // Number of dimensions in the optimisation problem
    int population_count; // Number of agents in the population
    float *lower_bound;    // Lower bound of the search space (same in all dimensions)
    float *upper_bound;    // Upper bound of the search space (same in all dimensions)
    int random_seed;      // Seed for the optimiser's pseudo random number generator
} de_settings;

typedef struct de_optimiser
{
    int dimension_count;  // Number of dimensions in the optimisation problem
    int population_count; // Number of agents in the population
    float *lower_bound;    // Lower bounds of the search space
    float *upper_bound;    // Upper bounds of the search space
    int best;             // Index of the agent with the lowest fitness

    float *crossover_probs;      // Per-agent crossover probability params (population_count)
    float *differential_weights; // Per-agent weighting params (population_count)
    float *fitnesses;            // Per-agent fitness
    float *candidates;           // Per-agent candidate vectors (population_count * dimension_count)
    uint32_t rng[4];             // PRNG state
} de_optimiser;

// Initialise the optimiser. Returns NULL if any allocation failed.
de_optimiser *de_init(de_settings *settings);

// Ask the optimiser to generate a candidate solution for evaluation
int de_ask(de_optimiser *opt, float *out_candidate);

// Tell the optimiser the fitness of a candidate solution
void de_tell(de_optimiser *opt, int id, const float *candidate, float fitness);

// Query the optimiser for the current best fitness and corresponding candidate solution. You
// may optionall pass in NULL to out_candidate if you just want the minimum fitness.
int de_best(de_optimiser *opt, float *val, float *out_candidate);

// Free the optimiser and its memory pools
void de_deinit(de_optimiser *opt);

#endif // DIFFERENTIAL_EVOLUTION_H

// Implementation

#ifdef DIFFERENTIAL_EVOLUTION_IMPL

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Random number generation, courtesy of https://prng.di.unimi.it/xoshiro128plus.c

static inline uint32_t de__rotl(const uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static uint32_t de__next(uint32_t s[4])
{
    const uint32_t result = s[0] + s[3];
    const uint32_t t = s[1] << 9;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;
    s[3] = de__rotl(s[3], 11);

    return result;
}

static float de__next_float(uint32_t s[4])
{
    // Only the upper 28 bits are high entropy enough
    const uint32_t max_draw = (~(uint32_t)0) >> 4;
    const float divisor = 1.0f / (float)max_draw;
    return (float)(de__next(s) >> 4) * divisor;
}

de_optimiser *de_init(de_settings *settings)
{
    const int dimension_count = settings->dimension_count;
    const int population_count = settings->population_count;
    size_t boundsz = sizeof(float) * dimension_count;
    float *lower_bound = DE_ALLOC(boundsz);
    memcpy(lower_bound,settings->lower_bound,boundsz);
    float *upper_bound = DE_ALLOC(boundsz);
    memcpy(upper_bound,settings->upper_bound,boundsz);
    const int random_seed = settings->random_seed;

    // Allocate the optimiser and its memory pools
    de_optimiser *opt = (de_optimiser *)DE_ALLOC(sizeof(de_optimiser));
    float *crossover_probs = (float *)DE_ALLOC(sizeof(float) * population_count);
    float *differential_weights = (float *)DE_ALLOC(sizeof(float) * population_count);
    float *fitnesses = (float *)DE_ALLOC(sizeof(float) * population_count);
    float *candidates = (float *)DE_ALLOC(sizeof(float) * dimension_count * population_count);

    if (!opt || !crossover_probs || !differential_weights || !fitnesses || !candidates)
    {
        DE_FREE(opt);
        DE_FREE(crossover_probs);
        DE_FREE(differential_weights);
        DE_FREE(fitnesses);
        DE_FREE(candidates);
        return NULL;
    }

    // Seed the xoroshiro128+ state with calls to rand()
    uint32_t rng[4];
    uint16_t *rng_view = (uint16_t *)rng;
    srand(random_seed);
    for (int i = 0; i < 8; i++)
    {
        rng_view[i] = (uint16_t)(rand() % 65535);
    }

    // Initialise crossover to random values in [0, 1]
    for (int i = 0; i < population_count; i++)
    {
        crossover_probs[i] = de__next_float(rng);
    }

    // Initialise differential weights to random values in [0, 2]
    for (int i = 0; i < population_count; i++)
    {
        differential_weights[i] = 2.0f * de__next_float(rng);
    }

    // Initialise fitnesses to infinity
    for (int i = 0; i < population_count; i++)
    {
        fitnesses[i] = INFINITY;
    }

    // Initialise the candidates to random points in the search space
    for (int i = 0; i < population_count; i++)
    {
        for ( int j = 0; j < dimension_count; j++ ) {
            float lb = lower_bound[j];
            float db = (upper_bound[j] - lower_bound[j]);
            candidates[dimension_count*i + j] = lb + de__next_float(rng) * db;
        }
    }

    // Fill out the optimiser struct and return the pointer to it
    *opt = (de_optimiser){
        .dimension_count = dimension_count,
        .population_count = population_count,
        .lower_bound = lower_bound,
        .upper_bound = upper_bound,
        .best = -1,

        .crossover_probs = crossover_probs,
        .differential_weights = differential_weights,
        .fitnesses = fitnesses,
        .candidates = candidates,
        .rng = {rng[0], rng[1], rng[2], rng[3]},
    };

    return opt;
}

int de_ask(de_optimiser *opt, float *out_candidate)
{
    const int neighbour_radius = 8;
    const int population_count = opt->population_count;
    const int dimension_count = opt->dimension_count;

    // Randomly choose an id and three nearby ids
    int x_id = de__next(opt->rng) % population_count;
    int a_id = x_id; 
    while ( x_id == a_id ) {
        a_id = (x_id + de__next(opt->rng) % neighbour_radius) % population_count;
    }
    int b_id = a_id;
    while ( x_id == b_id || a_id == b_id ) {
        b_id = (x_id + de__next(opt->rng) % neighbour_radius) % population_count;
    }
    int c_id = b_id;
    while ( x_id == c_id || a_id == c_id || b_id == c_id ) {
        c_id = (x_id + de__next(opt->rng) % neighbour_radius) % population_count;
    }

    // Get the crossover params for x
    float crossover_prob = opt->crossover_probs[x_id];
    float differential_weight = opt->differential_weights[x_id];

    // Get our candidate pointers
    float *x = &opt->candidates[x_id * dimension_count];
    float *a = &opt->candidates[a_id * dimension_count];
    float *b = &opt->candidates[b_id * dimension_count];
    float *c = &opt->candidates[c_id * dimension_count];

    // Choose a random dimension to change with certainty
    int mut_dim = de__next(opt->rng) % dimension_count;

    // Apply the crossover to the output array
    for (int i = 0; i < dimension_count; i++)
    {
        if (de__next_float(opt->rng) < crossover_prob || i == mut_dim)
        {
            out_candidate[i] = a[i] + differential_weight * (b[i] - c[i]);
        }
        else
        {
            out_candidate[i] = x[i];
        }
        if ( opt->lower_bound[i] > out_candidate[i] ) out_candidate[i] = opt->lower_bound[i];
        if ( opt->upper_bound[i] < out_candidate[i] ) out_candidate[i] = opt->upper_bound[i];
    }

    return x_id;
}

void de_tell(de_optimiser *opt, int id, const float *candidate, float fitness)
{
    const int dimension_count = opt->dimension_count;

    if (fitness < opt->fitnesses[id])
    {
        // Replace this individual with the candidate
        memcpy(&opt->candidates[id * dimension_count], candidate, sizeof(float) * dimension_count);
        opt->fitnesses[id] = fitness;
        if ( -1 == opt->best ) opt->best = id;
        else opt->best = (fitness < opt->fitnesses[opt->best]) ? id : opt->best;
    }
    else
    {
        // Reroll the crossover parameters for this individual
        opt->crossover_probs[id] = de__next_float(opt->rng);
        opt->differential_weights[id] = 2.0f * de__next_float(opt->rng);
    }
}

int de_best(de_optimiser *opt, float *val, float *out_candidate)
{
    const int dimension_count = opt->dimension_count;
    const int candidate_bytes = sizeof(float) * dimension_count;

    if ( -1 != opt->best ) {
        if (out_candidate) memcpy(out_candidate, &opt->candidates[opt->best * dimension_count], candidate_bytes);
        if ( val ) *val = opt->fitnesses[opt->best];
    }
    
    return opt->best;
}

void de_deinit(de_optimiser *opt)
{
    DE_FREE(opt->crossover_probs);
    DE_FREE(opt->differential_weights);
    DE_FREE(opt->fitnesses);
    DE_FREE(opt->candidates);
    DE_FREE(opt);
}

#endif // DIFFERENTIAL_EVOLUTION_IMPL