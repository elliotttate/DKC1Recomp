#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Dkc1RewindHistory {
  uint8_t *storage;
  size_t snapshot_size;
  size_t capacity;
  size_t count;
  size_t write_index;
} Dkc1RewindHistory;

bool Dkc1RewindHistoryInit(Dkc1RewindHistory *history,
                           size_t snapshot_size, size_t capacity);
void Dkc1RewindHistoryDestroy(Dkc1RewindHistory *history);
bool Dkc1RewindHistoryPush(Dkc1RewindHistory *history,
                           const void *snapshot);
bool Dkc1RewindHistoryPop(Dkc1RewindHistory *history, void *snapshot);
