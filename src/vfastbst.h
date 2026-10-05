#ifndef VFASTBST_H_INCLUDED
#define VFASTBST_H_INCLUDED

#include <string.h>
#ifndef VFASTBST_KEY
# define VFASTBST_KEY unsigned int
#endif

#ifndef VFASTBST_OBJ
typedef char vfastbst__sample_type[32];
# define VFASTBST_OBJ vfastbst__sample_type
#endif

#ifndef VFASTBST_OBJ_SIZE
# define VFASTBST_OBJ_SIZE 32
#endif

// VFASTBST custom for decima-consensus.
// not parallel anymore (not needed).
// supports deletion of smallest entry as long as key is smaller than some target.

// stdlib includes.
#include "stddef.h"         // for NULL, size_t.
#include "stdint.h"         // for uintptr_t.
#include "stdlib.h"         // for calloc, free.
#include "string.h"         // for strcmp.
#include "stdbool.h"        // boolean data type.

typedef struct vfastbst_nodeS{
  VFASTBST_KEY key;
  VFASTBST_OBJ obj;
  struct vfastbst_nodeS* leaf[2];
} vfastbst_nodeT;

static vfastbst_nodeT* vfastbst_node_init(VFASTBST_KEY key, VFASTBST_OBJ* obj){
  vfastbst_nodeT* const restrict node = calloc(1, sizeof(*node));
  node->key = key;
  memcpy(&node->obj, obj, VFASTBST_OBJ_SIZE);

  return node;
}

static void vfastbst_node_destroy(vfastbst_nodeT* const restrict node){
  free(node);
}

typedef struct{ struct vfastbst_nodeS* node; } vfastbstT;   // all types terminated with T and not _t to be annoyingly POSIX-compliant. fuck you POSIX why'd you do this???
#define VFASTBST_INIT (vfastbstT){ NULL }

static void vfastbst_destroy__r(vfastbst_nodeT* const restrict node){
  if(node->leaf[0]) vfastbst_destroy__r(node->leaf[0]);
  if(node->leaf[1]) vfastbst_destroy__r(node->leaf[1]);
}

static void vfastbst_destroy(vfastbstT* const restrict tree, const bool freectx){
  if(tree->node) vfastbst_destroy__r(tree->node);
  if(freectx) free(tree);
}

static bool vfastbst_search(vfastbstT* const restrict bst, VFASTBST_KEY key, VFASTBST_OBJ* obj){
  if(!bst->node) return false;
  vfastbst_nodeT* restrict node = bst->node;

  do{
    if(node->key == key){ memcpy(obj, node->obj, VFASTBST_OBJ_SIZE); return true; }
    const bool pick = node->key > key;
    node = node->leaf[pick];
  } while(node);
  return false;
}


static bool vfastbst_put(vfastbstT* const restrict bst, VFASTBST_KEY key, VFASTBST_OBJ* obj){
  if(!bst->node){
    bst->node = vfastbst_node_init(key, obj);
    return true;
  }

  vfastbst_nodeT* node = bst->node; 
  while(1){
  // we want to allow multiple elements to share the same key. how?
  // it would be rather odd for two blocks with differing work to not be able to coexist you know!
  // we need a ll rather than this. there usually aren't that many (>20 or so) tips.
  // why not just add it to a lower lvl?
    if(node->key == key){
      if(!node->leaf[0]) node->leaf[0] = vfastbst_node_init(key, obj);
      else if(!node->leaf[1]) node->leaf[1] = vfastbst_node_init(key, obj);
      else{ node = node->leaf[0]; continue; }
      return true;
    }
    const size_t ch_pick = node->key > key;

    if(node->leaf[ch_pick]) node = node->leaf[ch_pick];
    else{
      node->leaf[ch_pick] = vfastbst_node_init(key, obj);
      return true;
    }
  }
}

// deletes block.
// when deleting block, if key == node->key but obj != node->obj, we go to left branch.
// this is due to us inserting elements even when there are conflicts.
static bool vfastbst_del(vfastbstT* const restrict bst, VFASTBST_KEY key, VFASTBST_OBJ object){
  vfastbst_nodeT *restrict node = bst->node, *father = NULL;
  size_t ch_father = 0;

  if(!node) return false;

  while(1){
  // we're encountering our object.
    if(node->key == key){
      if(memcmp(node->obj, object, VFASTBST_OBJ_SIZE) == 0) break;
      father = node; ch_father = 0;
      node = node->leaf[0];
      if(!node) return false;
    }
    const size_t ch_pick = node->key > key;

    if(node->leaf[ch_pick]){
      father = node; ch_father = ch_pick;
      node = node->leaf[ch_pick];
    }
    else return false;
  }

  if(!node->leaf[0] && !node->leaf[1]){
    if(!father) bst->node = NULL;
    else father->leaf[ch_father] = NULL;
  }
  else if(!node->leaf[0] || !node->leaf[1]){
    const size_t ch = node->leaf[1] != NULL;
    father->leaf[ch_father] = node->leaf[ch];
  }
  else{
    vfastbst_nodeT *restrict largest_small = node->leaf[0], *restrict ls_father = NULL;

    while(largest_small->leaf[1]){ ls_father = largest_small; largest_small = largest_small->leaf[0]; }

    if(ls_father){
      ls_father->leaf[1] = largest_small->leaf[0];
      father->leaf[ch_father] = largest_small;

      largest_small->leaf[0] = node->leaf[0];
      largest_small->leaf[1] = node->leaf[1];
    }
    else father->leaf[ch_father] = largest_small;
  }

  vfastbst_node_destroy(node);
  return true;
}

static bool vfastbst_del_smallest(vfastbstT* const restrict bst, VFASTBST_KEY target, VFASTBST_OBJ* obj){
  if(!bst->node) return false;

  vfastbst_nodeT* small = bst->node, *father = NULL;

  while(small->leaf[0]){
    father = small;
    small = small->leaf[0];
  }
  if(small->key > target) return false;
  if(!father) bst->node = small->leaf[1];
  else father->leaf[0] = small->leaf[1];

  memcpy(obj, &small->obj, VFASTBST_OBJ_SIZE);
  vfastbst_node_destroy(small);
  return true;
}

#endif
