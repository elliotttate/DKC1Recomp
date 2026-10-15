#ifndef DKC1_COOP_TRANSACTION_H
#define DKC1_COOP_TRANSACTION_H
#include <stddef.h>
/* Coordinate participant lifetime only. WRAM/device commit policy remains
 * owned by each caller; failed acquisition unwinds only opened participants. */
typedef struct Dkc1CoopTransactionParticipant {
  int (*begin)(void);
  void (*end)(int commit);
} Dkc1CoopTransactionParticipant;
typedef struct Dkc1CoopTransaction {
  const Dkc1CoopTransactionParticipant *participants;
  size_t opened;
} Dkc1CoopTransaction;
static inline void Dkc1CoopTransactionEnd(Dkc1CoopTransaction *t, int commit) {
  while (t->opened) t->participants[--t->opened].end(commit);
}
static inline int Dkc1CoopTransactionBegin(Dkc1CoopTransaction *t,
    const Dkc1CoopTransactionParticipant *participants, size_t count) {
  t->participants=participants;
  t->opened=0;
  while (t->opened<count) {
    if (!participants[t->opened].begin()) {
      Dkc1CoopTransactionEnd(t,0);
      return 0;
    }
    ++t->opened;
  }
  return 1;
}
#endif
