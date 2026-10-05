// TODO: add this l8r.
#include "vfastqueue.h"
#define DECIMA_CONSENSUS_IMPLEMENTATION
#include "decima-consensus.h"

// example usage.

int main(){
    consensusT* const restrict cons = consensus_init(NULL, 1000, 10, 100, 10);
    block_idT id; memset(id, 0, sizeof(id));
    block_idT id_prev; memset(id, 1, sizeof(id_prev));

    consensus_add(cons, id, id_prev, 10, 100);

    consensus_destroy(cons, true);
    return 0;
}