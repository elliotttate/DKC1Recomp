#ifndef DKC1_TERRAIN_H
#define DKC1_TERRAIN_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Pure presentation models, with map access supplied by the caller. No
 * collision, guest writes, or retained history. The adjacency rectangle must
 * belong to one calibrated source and must not wrap into another map. */
typedef enum Dkc1TerrainFill {
  kDkc1TerrainUnknown,
  kDkc1TerrainEmpty,
  kDkc1TerrainPartial,
  kDkc1TerrainFull,
} Dkc1TerrainFill;

typedef bool (*Dkc1TerrainRead)(void *, uint32_t, uint32_t, uint16_t *);
typedef Dkc1TerrainFill (*Dkc1TerrainClassify)(void *, uint32_t, uint32_t);

static inline bool Dkc1TerrainWallRow(Dkc1TerrainClassify fill, void *ctx,
                                     uint32_t target, uint32_t source,
                                     uint32_t behind, uint32_t y) {
  return fill(ctx, target, y) == kDkc1TerrainEmpty &&
         fill(ctx, source, y) == kDkc1TerrainFull &&
         fill(ctx, behind, y) == kDkc1TerrainFull;
}

static inline bool Dkc1TerrainWallSource(
    Dkc1TerrainClassify fill, void *ctx, bool east, uint32_t target,
    uint32_t edge, uint32_t y, uint32_t *source) {
  if (!fill || !source || target > 63u || edge > 63u || y > 511u ||
      (east ? target <= edge : target >= edge) ||
      fill(ctx, target, y) != kDkc1TerrainEmpty)
    return false;
  uint32_t candidate = target;
  while (candidate != edge) {
    candidate = east ? candidate - 1u : candidate + 1u;
    const Dkc1TerrainFill kind = fill(ctx, candidate, y);
    if (kind == kDkc1TerrainEmpty)
      continue;
    if (kind != kDkc1TerrainFull || (east ? candidate == 0 : candidate == 63))
      return false;
    const uint32_t behind = east ? candidate - 1u : candidate + 1u;
    if (!Dkc1TerrainWallRow(fill, ctx, target, candidate, behind, y))
      return false;
    if ((y > 0 && Dkc1TerrainWallRow(
            fill, ctx, target, candidate, behind, y - 1u)) ||
        (y < 511u && Dkc1TerrainWallRow(
            fill, ctx, target, candidate, behind, y + 1u))) {
      *source = candidate;
      return true;
    }
    return false;
  }
  return false;
}

/* A short opening in a wall is not unused side art. Accept void from the
 * visible top, or a run of at least four empty rows sealed from the viewport
 * on every row. Search is bounded; unknown rows veto the proof. */
static inline bool Dkc1TerrainVoidRow(Dkc1TerrainClassify fill, void *ctx,
                                     bool east, uint32_t target,
                                     uint32_t edge, uint32_t y) {
  if (fill(ctx, target, y) != kDkc1TerrainEmpty)
    return false;
  uint32_t x = target;
  for (;;) {
    const Dkc1TerrainFill kind = fill(ctx, x, y);
    if (kind == kDkc1TerrainUnknown)
      return false;
    if (kind != kDkc1TerrainEmpty)
      return true;
    if (x == edge)
      return false;
    x = east ? x - 1u : x + 1u;
  }
}

static inline bool Dkc1TerrainSealedVoid(
    Dkc1TerrainClassify fill, void *ctx, bool east, uint32_t target,
    uint32_t edge, uint32_t y, uint32_t visible_top) {
  if (!fill || target > 63 || edge > 63 || y > 511 || visible_top > 511 ||
      (east ? target <= edge : target >= edge))
    return false;
  bool top_empty = visible_top <= y;
  for (uint32_t row = visible_top; top_empty && row <= y; row++)
    top_empty = fill(ctx, target, row) == kDkc1TerrainEmpty;
  if (top_empty)
    return true;
  if (!Dkc1TerrainVoidRow(fill, ctx, east, target, edge, y))
    return false;
  unsigned rows = 1;
  for (unsigned d = 1; d <= 16 && d <= y; d++) {
    if (!Dkc1TerrainVoidRow(fill, ctx, east, target, edge, y - d))
      break;
    rows++;
  }
  for (unsigned d = 1; d <= 16 && y + d <= 511; d++) {
    if (!Dkc1TerrainVoidRow(fill, ctx, east, target, edge, y + d))
      break;
    rows++;
  }
  return rows >= 4;
}

enum { kDkc1TerrainMaxPairs = 512 };
typedef struct Dkc1TerrainPair {
  uint16_t west, east, count;
} Dkc1TerrainPair;
typedef struct Dkc1TerrainAdjacency {
  Dkc1TerrainPair pair[kDkc1TerrainMaxPairs];
  unsigned count;
  bool valid;
} Dkc1TerrainAdjacency;

static inline bool Dkc1TerrainBuildAdjacency(
    Dkc1TerrainAdjacency *out, Dkc1TerrainRead read,
    Dkc1TerrainClassify fill, void *ctx, uint32_t left, uint32_t right,
    uint32_t top, uint32_t bottom) {
  if (!out)
    return false;
  memset(out, 0, sizeof *out);
  if (!read || !fill || left >= right || right > 63 || top > bottom ||
      bottom > 511 || bottom - top >= 8)
    return false;
  for (uint32_t y = top; y <= bottom; y++) {
    for (uint32_t x = left; x < right; x++) {
      uint16_t a, b;
      if (!read(ctx, x, y, &a) || !read(ctx, x + 1u, y, &b))
        return false;
      /* Only populated wall pairs may supply continuation. Cell orientation
       * is part of the key: flipped art cannot borrow an unflipped neighbor. */
      if (fill(ctx, x, y) != kDkc1TerrainFull ||
          fill(ctx, x + 1u, y) != kDkc1TerrainFull)
        continue;
      a &= 0xc7ffu;
      b &= 0xc7ffu;
      unsigned i = 0;
      while (i < out->count &&
             (out->pair[i].west != a || out->pair[i].east != b))
        i++;
      if (i == out->count) {
        if (out->count == kDkc1TerrainMaxPairs)
          return false;
        out->pair[i].west = a;
        out->pair[i].east = b;
        out->count++;
      }
      out->pair[i].count++;
    }
  }
  out->valid = true;
  return true;
}

static inline bool Dkc1TerrainSuccessor(const Dkc1TerrainAdjacency *map,
                                       uint16_t source, bool east,
                                       uint16_t *next) {
  if (!map || !map->valid || !next)
    return false;
  unsigned best = 0;
  uint16_t chosen = 0;
  bool tied = false;
  for (unsigned i = 0; i < map->count; i++) {
    const Dkc1TerrainPair *p = &map->pair[i];
    if ((east ? p->west : p->east) != (source & 0xc7ffu))
      continue;
    const uint16_t candidate = east ? p->east : p->west;
    if (p->count > best) {
      best = p->count;
      chosen = candidate;
      tied = false;
    } else if (p->count == best && chosen != candidate) {
      tied = true;
    }
  }
  if (best == 0 || tied)
    return false;
  *next = chosen;
  return true;
}

static inline bool Dkc1TerrainContinue(const Dkc1TerrainAdjacency *map,
                                      uint16_t source, bool east,
                                      unsigned steps, uint16_t *result) {
  if (!result || steps == 0 || steps > 64)
    return false;
  uint16_t current = source;
  for (unsigned i = 0; i < steps; i++)
    if (!Dkc1TerrainSuccessor(map, current, east, &current))
      return false;
  *result = current;
  return true;
}

#endif
