#ifndef DECIMA_CONSENSUS_H_INCLUDED
#define DECIMA_CONSENSUS_H_INCLUDED

// simple one-library blockchain-based consensus algorithm.
// multithreaded, allows for parallel insertion of blocks, but it's a very lazy multithreading.
// I figured at the block speeds Decima's going to see, it doesn't matter much.
// dw I won't implement a crypto scam I actually believe in ts.
// algorithm:
//  blocks can be inserted 'in a parallel manner' into a queue.
//  a thread can become blockmaster after having inserted into the queue; this means emptying the queue & updating consensus state.
//  while the queue is being emptied, people may still insert. how?

// stdlib includes.
#include "stdint.h"
#include "stdbool.h"

typedef struct consensusS consensusT;                   // consensus context.
typedef uint64_t consensus_workT;                       // amount of work inside block.
#define CONSENSUS_WORK_MAX UINT64_MAX                   // maximum amount of work possible.
typedef uint64_t consensus_timeT;                       // age of block.

#define BLOCK_ID_SIZE 64
typedef char block_idT[BLOCK_ID_SIZE];                  // block ID.

// cons ~ consensusT, if one wanted it statically allocated instead of dynamically.
// expiration ~ after how long consensus algorithm can forget about blocks in memory.
// interval ~ how much time between blocks.
// leeway ~ how much into the future can a block we just now receive be.
consensusT* consensus_init(consensusT* restrict cons, consensus_timeT expiration, const consensus_timeT leeway, const consensus_timeT interval, const consensus_timeT divergence);

// destroys consensusT, frees it if the 'freectx' flag is set. only set it if cons is dynamically allocated.
void consensus_destroy(consensusT* const restrict cons, const bool freectx);

// adds block, updates consensus.
// cons ~ consensus context to add stuff to.
// id ~ id of the block.
// work ~ how much work the block has.
// age ~ how old the block is.
bool consensus_add(consensusT* const restrict cons, const block_idT id, const block_idT prev, const consensus_workT work, const consensus_timeT age);

// returns id of head of blockchain that's trusted, which has a negligible chance of being overturned.
void consensus_get_main(consensusT* const restrict cons, block_idT dst);

#endif

#ifdef DECIMA_CONSENSUS_IMPLEMENTATION

// configurable parameters.
#ifndef DECIMA_CONSENSUS__atomic_refT
# define DECIMA_CONSENSUS__atomic_refT atomic_uint_least8_t
# define DECIMA_CONSENSUS__underlying_refT uint8_t
#endif 

#define VFASTQUEUE_IMPLEMENTATION
#define VFASTRHT_IMPLEMENTATION
#define VFASTRHT_KEY_UNDERLYING_TYPE block_idT
#define VFASTRHT_KEY_LENGTH BLOCK_ID_SIZE

#define VFASTBST_KEY consensus_workT
#define VFASTBST_OBJ block_idT
#define VFASTBST_OBJ_SIZE BLOCK_ID_SIZE

#include "vfastqueue.h"
#include "vfastrht.h"
#include "vfastbst.h"

// stdlib includes.
#include "time.h"                                       // time.
#include "stdio.h"                                      // DEBUG.
#include "stddef.h"                                     // size_t.
#include "stdlib.h"                                     // malloc, free.

// POSIX includes.
#include "sched.h"

typedef enum{
  BLOCK_FLAG_NONE       = 0,
  BLOCK_TIP             = (1<<0)
} block_flagT;

typedef struct blockS{
  struct blockS* prev;
  size_t cnt_fork;                                      // No. of forks ahead of block.

  consensus_timeT age;                                  // age of block, rounded to lowest multiple of 'interval'.
  consensus_workT total_work;                           // work this block & all previous blocks produce.
// even if it were 1bln units of work/block, 1 block every 5 minutes, it'd still take over 100k yrs for this to overflow.
// if it ever becomes a problem, 'housekeeping' (in the form of going back & subtracting the weight of whatever old block gets removed) can be performed every once in a while.
  block_flagT flags;
} blockT;

// block_localT* const restrict toadd = block_local_init(&id, &id_prev, work, age);
typedef struct{
  block_idT id;
  block_idT id_prev;
  consensus_timeT age;
  consensus_workT work;
} block_localT;

static block_localT* block_local_init(const block_idT id, const block_idT id_prev, const consensus_timeT age, const consensus_workT work){
  block_localT* const restrict local = malloc(sizeof(*local));

  memcpy(local->id, id, BLOCK_ID_SIZE);
  memcpy(local->id_prev, id_prev, BLOCK_ID_SIZE);
  local->age = age;
  local->work = work;

  return local;
}

static void block_local_destroy(block_localT* const restrict local){
  free(local);
}

// consensus is pretty easy to implement; just a ll with blocks past their 'expiration date' removed.
struct consensusS{
// id_trusted access is regolated by references arr.
// changing trusted block:
  block_idT id_trusted[2];
  DECIMA_CONSENSUS__atomic_refT references[2];
  atomic_uint_least8_t sel;

  block_idT id_main;                                    // main tip. id_trusted is always a bit behind this as to avoid short reorgs causing one to double-spend (happen often by design).
  blockT* main;

  vfastrhtT* all;                                       // all entries.
  vfastbstT tips;                                       // tips, ordered by total work thx to bst structure.
  vfastqueueT* queue;                                   // to clear queue, one must just atomically swap tail with NULL.

  atomic_flag blockmaster;                              // set by thread that becomes block master.

  consensus_timeT expiration;                           // after how long blocks expire.

// TODO: find formula for interval divergence stuff
  consensus_timeT leeway;                               // how much into the future blocks can be in.
  consensus_timeT interval;                             // interval blocks must be in.
  consensus_timeT divergence;                           // how far off into the future or past blocks can diverge from interval.
};

// returns newly initialised block.
static blockT* block_init(blockT* const restrict prev, const consensus_timeT age, const consensus_workT total_work){
  blockT* const restrict blk = malloc(sizeof(*blk));

  blk->cnt_fork = 0;
  blk->age = age;

  blk->prev = prev;
  blk->total_work = total_work;
// new block is tip always.
  blk->flags = BLOCK_TIP;

  return blk;
}

// goes to bottom of chain, purges old blocks.
// has to be recursive, but if called often enough recursion is very shallow.
static bool block_destroy__purgeold(blockT* restrict blk, const consensus_timeT expiration){
  if(blk->prev && !block_destroy__purgeold(blk->prev, expiration)) return false;
  if(blk->cnt_fork > 1) return false;

  free(blk);
  return true;
}

// destroys chain of blocks (:OOO).
// purgeold ~ block_destroy should attempt to destroy old blocks.
// expiration ~ from what point onwards are blocks considered too 'old', relative to current time.
static void block_destroy(blockT* restrict blk, const bool purgeold, const consensus_timeT expiration){
  while(blk->cnt_fork == 0){
    void* const curr = blk;
    blk = blk->prev;
    if(!blk) break;                     // can happen due to old blocks being purged from RAM.
    free(curr);
    blk->cnt_fork--;
  }

  if(purgeold){
    while(blk->age > purgeold){
      blk = blk->prev;
      if(!blk) return;
    }
    block_destroy__purgeold(blk, expiration);
  }
}

consensusT* consensus_init(consensusT* restrict cons, consensus_timeT expiration, const consensus_timeT leeway, const consensus_timeT interval, const consensus_timeT divergence){
  if(!cons) cons = malloc(sizeof(*cons));

  cons->all = vfastrht_table_init();
  cons->tips = VFASTBST_INIT;
  cons->queue = vfastqueue_init(NULL);

  cons->tips = VFASTBST_INIT;

  cons->leeway = leeway;
  cons->interval = interval;
  cons->expiration = expiration;

  return cons;
}

void consensus_destroy(consensusT* const restrict cons, const bool freectx){
// destroys blockchain memory representation.
  while(cons->tips.node){
    block_idT id_smallest;
    vfastbst_del_smallest(&cons->tips, CONSENSUS_WORK_MAX, &id_smallest);
    blockT* const restrict smallest = vfastrht_search_elem(cons->all, id_smallest);
    block_destroy(smallest, false, 0);        // purging old blocks doesn't really matter.
  }

  vfastrht_table_destroy(cons->all);
  vfastqueue_destroy(cons->queue, true);

  if(freectx) free(cons);
}

static void consensus_switch_main(consensusT* const restrict cons, blockT* const restrict new, const block_idT id_new){
  cons->main = new;
  memcpy(cons->id_main, id_new, sizeof(block_idT));
  
  const uint8_t sel = atomic_fetch_xor(&cons->sel, 1);
  atomic_fetch_add(&cons->references[sel], 1);
  while(atomic_load(&cons->references[sel]) > 1) sched_yield();

  memcpy(cons->id_trusted, id_new, sizeof(block_idT));
  atomic_fetch_sub(&cons->references[sel], 1);
}

// the in-memory representation has to be updated every once in a while, lest it spiral out of control.
// each block takes up 32B of RAM; over 24hrs, 288 blocks get published.
// say uncle rate is 3; 4*288 are published. that is 36KiB of memory getting leaked each day.
// that might not sound like much, but I want CDEC (or a version of it) to run on very tiny embedded systems with very high uptimes.
// WE NEED TO PRUNE OLD BLOCKS!
static void consensus__blockmaster(consensusT* const restrict cons, const consensus_timeT currtime){
// has to check somewhere if master block has been purged or if its total work is lower than the max amt by some threshold.
  consensus_workT maxwork = 0;                  // we then index ids through tip bst by this, if necessary. 
// processes queue of updates to be perfomed.
  vfastqueueT localq;
// if no updates have been performed to the chain yet, the in-memory representation is as updated as it can be.
  if(!vfastqueue_move(cons->queue, &localq)) return;
  vfastqueue_objT* obj;
  while((obj = vfastqueue_pop(&localq))){
    block_localT* local = obj->elem;

    if(vfastrht_search(cons->all, local->id)) goto LOCAL_SKIP;          // goto considered HARMLESS :DDD.
    blockT* prev = vfastrht_search_elem(cons->all, local->id_prev);
    if(!prev || prev->age > local->age) goto LOCAL_SKIP;

    const consensus_workT blkwork = prev->total_work + local->work;
    if(blkwork > maxwork) maxwork = blkwork;
    blockT* block = block_init(prev, local->age, blkwork);
    if(prev->flags & BLOCK_TIP)
      vfastbst_del(&cons->tips, prev->total_work, local->id_prev);
    vfastbst_put(&cons->tips, block->total_work, &local->id);

    if(!cons->main || prev == cons->main) consensus_switch_main(cons, block, local->id);
LOCAL_SKIP:
    block_local_destroy(local);
    vfastqueue_obj_destroy(obj);
  }
// checks to see if main fork is too weak.
  if(cons->main && cons->main->total_work < maxwork - 1000 /* TODO: decide what to make this based on network conditions */){
    block_idT id_maxwork; vfastbst_search(&cons->tips, maxwork, &id_maxwork);
    blockT* const restrict max = vfastrht_search_elem(cons->all, id_maxwork);

    consensus_switch_main(cons, max, id_maxwork);
  }
// prunes weak forks.
  block_idT id_weak;
  while(vfastbst_del_smallest(&cons->tips, 1000 /* TODO: decide what to make this based on network conditions */, &id_weak)){
    blockT* const restrict weak = vfastrht_del_elem(cons->all, id_weak);
  // 1/8 chance it purges old blocks, can be made lower by increasing the power of two.
  // IDK what's optimal, keep as-is for now & reduce only if it proves to be too much time for too little gain.
    block_destroy(weak, (id_weak[0] & 7) == 0, currtime - cons->expiration);
  }
}

// adds block, updates consensus.
// the following is necessary whenever one's adding a block:
//  removal of those blocks that are too old, unless there's a conflict;
//  addition of block to wherever it belongs; <- how do we know where it belongs? through an rht, perhaps?
//  deciding which fork is the main one.
// the main fork is purely identified by cons->first. adding new blocks, if done to cons->first, updates cons->first to the new one.
bool consensus_add(consensusT* const restrict cons, const block_idT id, const block_idT id_prev, const consensus_workT work, const consensus_timeT age){
// local representation handled by the blockmaster has to be created.
// were it any other way, parallelism conflicts could occur.

// TODO: make these all powers of 2 & make these shifts & masks.
  const consensus_timeT currtime = time(NULL);
  const consensus_timeT center = (age/cons->interval + (age%cons->interval != 0))*cons->interval;
  if(                                                                       // reasons a block is outright rejected:
    age - cons->leeway > currtime ||                                        //   block is too far off into the future.
    center - cons->divergence > age || center + cons->divergence < age      //   block diverges too much from standard epoch time.
  ) return false;

  block_localT* const restrict toadd = block_local_init(id, id_prev, age, work);
  vfastqueue_push(cons->queue, toadd);

  if(!atomic_flag_test_and_set(&cons->blockmaster)){
    consensus__blockmaster(cons, currtime);
    atomic_flag_clear(&cons->blockmaster);
  }
  return true;
}

// gets main/latest block produced by consensus.
// returns true on success, false otherwise (no newest block).
void consensus_get_main(consensusT* const restrict cons, block_idT dst){
  uint8_t sel;
  do{
    sel = atomic_load(&cons->sel);
    DECIMA_CONSENSUS__underlying_refT ref = atomic_load(&cons->references[sel]);
    if(ref & 1) continue;
    ref = atomic_fetch_add(&cons->references[sel], 2);
    if(ref & 1){
      atomic_fetch_sub(&cons->references[sel], 2);
      continue;
    }
    else break;
  } while(1);

  memcpy(dst, cons->id_trusted[sel], sizeof(block_idT));

  atomic_fetch_sub(&cons->references[sel], 2);
}

#endif
