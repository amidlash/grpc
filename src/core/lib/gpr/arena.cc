/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/gpr/arena.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

// TODO(roth): We currently assume that all callers need alignment of 16
// bytes, which may be wrong in some cases.  As part of converting the
// arena API to C++, we should consider replacing gpr_arena_alloc() with a
// template that takes the type of the value being allocated, which
// would allow us to use the alignment actually needed by the caller.
#define ROUND_UP_TO_ALIGNMENT_SIZE(x) \
  (((x) + GPR_MAX_ALIGNMENT - 1u) & ~(GPR_MAX_ALIGNMENT - 1u))

typedef struct zone {
  size_t size_begin;
  size_t size_end;
  gpr_atm next_atm;
} zone;

struct gpr_arena {
  gpr_atm size_so_far;
  zone initial_zone;
};

static void* zalloc_aligned(size_t size) {
  void* ptr = gpr_malloc_aligned(size, GPR_MAX_ALIGNMENT);
  memset(ptr, 0, size);
  return ptr;
}

gpr_arena* gpr_arena_create(size_t initial_size) {
  initial_size = ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  gpr_arena* a = (gpr_arena*)zalloc_aligned(
      ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena)) + initial_size);
  a->initial_zone.size_end = initial_size;
  return a;
}

size_t gpr_arena_destroy(gpr_arena* arena) {
  gpr_atm size = gpr_atm_no_barrier_load(&arena->size_so_far);
  zone* z = (zone*)gpr_atm_no_barrier_load(&arena->initial_zone.next_atm);
  gpr_free_aligned(arena);
  while (z) {
    zone* next_z = (zone*)gpr_atm_no_barrier_load(&z->next_atm);
    gpr_free_aligned(z);
    z = next_z;
  }
  return (size_t)size;
}

void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  size = ROUND_UP_TO_ALIGNMENT_SIZE(size);
  size_t start =
      (size_t)gpr_atm_no_barrier_fetch_add(&arena->size_so_far, size);
  zone* z = &arena->initial_zone;
  while (start > z->size_end) {
    zone* next_z = (zone*)gpr_atm_acq_load(&z->next_atm);
    if (next_z == nullptr) {
      size_t next_z_size = (size_t)gpr_atm_no_barrier_load(&arena->size_so_far);
      next_z = (zone*)zalloc_aligned(ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone)) +
                                     next_z_size);
      next_z->size_begin = z->size_end;
      next_z->size_end = z->size_end + next_z_size;
      if (!gpr_atm_rel_cas(&z->next_atm, (gpr_atm)NULL, (gpr_atm)next_z)) {
        gpr_free_aligned(next_z);
        next_z = (zone*)gpr_atm_acq_load(&z->next_atm);
      }
    }
    z = next_z;
  }
  if (start + size > z->size_end) {
    return gpr_arena_alloc(arena, size);
  }
  GPR_ASSERT(start >= z->size_begin);
  GPR_ASSERT(start + size <= z->size_end);
  char* ptr = (z == &arena->initial_zone)
                  ? (char*)arena + ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena))
                  : (char*)z + ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone));
  return ptr + start - z->size_begin;
}
