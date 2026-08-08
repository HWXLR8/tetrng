#include <inttypes.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <omp.h>

#define RNG_MULTIPLIER UINT32_C(0x41C64E6D)
#define RNG_INCREMENT UINT32_C(12345)
#define SEED_LIMIT UINT64_C(0xFFFFFFFF)
#define WORK_BLOCK_SIZE UINT64_C(65536)
#define CANCEL_CHECK_INTERVAL UINT64_C(1024)

struct target {
    uint8_t piece;
    uint8_t history_mask;
};

struct search_state {
    alignas(64) _Atomic uint64_t next_seed;
    alignas(64) _Atomic uint64_t best_seed;
};

static inline uint32_t rng_hash(uint32_t state)
{
    return state * RNG_MULTIPLIER + RNG_INCREMENT;
}

static inline uint8_t state_to_piece(uint32_t state)
{
    return (uint8_t)(((state >> 10) & UINT32_C(0x7FFF)) % 7U);
}

static int piece_index(char piece)
{
    switch (piece) {
    case 'I': return 0;
    case 'Z': return 1;
    case 'S': return 2;
    case 'J': return 3;
    case 'L': return 4;
    case 'O': return 5;
    case 'T': return 6;
    default: return -1;
    }
}

static struct target *prepare_targets(const char *goal, size_t *length_out)
{
    static const uint8_t initial_history[3] = {2, 2, 1}; /* SSZ */
    const size_t length = strlen(goal);

    if (length == 0) {
        fprintf(stderr, "goal must contain at least one piece\n");
        exit(EXIT_FAILURE);
    }

    struct target *targets = malloc(length * sizeof(*targets));
    if (targets == NULL) {
        fprintf(stderr, "unable to allocate goal data\n");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < length; ++i) {
        const int piece = piece_index(goal[i]);
        if (piece < 0) {
            fprintf(stderr, "invalid piece '%c' at goal position %zu\n",
                    goal[i], i + 1);
            free(targets);
            exit(EXIT_FAILURE);
        }
        targets[i].piece = (uint8_t)piece;
        targets[i].history_mask = 0;
    }

    for (size_t i = 1; i < length; ++i) {
        uint8_t mask = 0;

        for (size_t position = i - 1; position < i + 3; ++position) {
            const uint8_t piece = position < 3
                                      ? initial_history[position]
                                      : targets[position - 3].piece;
            mask |= (uint8_t)(1U << piece);
        }
        targets[i].history_mask = mask;
    }

    *length_out = length;
    return targets;
}

static inline bool seed_matches(uint32_t seed, const struct target *targets,
                                size_t target_count)
{
    uint32_t state = rng_hash(seed);
    uint8_t piece = state_to_piece(state);

    if (piece != targets[0].piece) {
        return false;
    }

    for (size_t i = 1; i < target_count; ++i) {
        for (unsigned int roll = 0; roll < 5; ++roll) {
            state = rng_hash(state);
            piece = state_to_piece(state);
            if ((targets[i].history_mask & (uint8_t)(1U << piece)) == 0) {
                break;
            }

            /* A rejected roll advances the Python reference a second time. */
            state = rng_hash(state);
            piece = state_to_piece(state);
        }

        if (piece != targets[i].piece) {
            return false;
        }
    }

    return true;
}

static void record_match(_Atomic uint64_t *best_seed, uint64_t seed)
{
    uint64_t best = atomic_load_explicit(best_seed, memory_order_relaxed);

    while (seed < best &&
           !atomic_compare_exchange_weak_explicit(best_seed, &best, seed,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static uint64_t find_first_seed(const struct target *targets,
                                size_t target_count)
{
    struct search_state search;
    atomic_init(&search.next_seed, 0);
    atomic_init(&search.best_seed, SEED_LIMIT);

#pragma omp parallel
    {
        for (;;) {
            const uint64_t begin = atomic_fetch_add_explicit(
                &search.next_seed, WORK_BLOCK_SIZE, memory_order_relaxed);
            const uint64_t known_best = atomic_load_explicit(
                &search.best_seed, memory_order_relaxed);

            if (begin >= SEED_LIMIT || begin >= known_best) {
                break;
            }

            uint64_t end = begin + WORK_BLOCK_SIZE;
            if (end > SEED_LIMIT) {
                end = SEED_LIMIT;
            }

            for (uint64_t seed = begin; seed < end; ++seed) {
                if ((seed & (CANCEL_CHECK_INTERVAL - 1)) == 0 &&
                    seed >= atomic_load_explicit(&search.best_seed,
                                                 memory_order_relaxed)) {
                    break;
                }

                if (seed_matches((uint32_t)seed, targets, target_count)) {
                    record_match(&search.best_seed, seed);
                    break;
                }
            }
        }
    }

    return atomic_load_explicit(&search.best_seed, memory_order_relaxed);
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s GOAL\n", program);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    size_t target_count = 0;
    struct target *targets = prepare_targets(argv[1], &target_count);

    omp_set_dynamic(0);
    omp_set_num_threads(omp_get_num_procs());

    const uint64_t seed = find_first_seed(targets, target_count);
    if (seed < SEED_LIMIT) {
        printf("matching seed: %" PRIu64 "\n", seed);
    } else {
        puts("no seed found");
    }

    free(targets);
    return EXIT_SUCCESS;
}
