#include "neojs/core/rbtree.h"
#include "neojs/core/allocator.h"
#include "neojs/core/common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct _neo_rbtree_node_t {
  bool color;
  void *key;
  neo_rbtree_node_t left;
  neo_rbtree_node_t right;
  neo_rbtree_node_t parent;
};
struct _neo_rbtree_t {
  neo_rbtree_node_t root;
  bool autofree;
  size_t size;
  size_t keysize;
  neo_compare_fn_t compare;
  neo_allocator_t allocator;
};

static void neo_rbtree_dispose_node(neo_rbtree_t self, neo_rbtree_node_t node,
                                    neo_allocator_t allocator) {
  if (node) {
    neo_rbtree_dispose_node(self, node->left, allocator);
    neo_rbtree_dispose_node(self, node->right, allocator);
    if (self->autofree) {
      neo_allocator_free(allocator, node->key);
    }
    neo_allocator_free(allocator, node);
  }
}

static void neo_rbtree_dispose(neo_allocator_t allocator, neo_rbtree_t self) {
  neo_rbtree_dispose_node(self, self->root, allocator);
}

neo_rbtree_t neo_create_rbtree(neo_allocator_t allocator,
                               neo_rbtree_initialize_t *initialize) {
  neo_rbtree_t tree = neo_allocator_alloc(
      allocator, sizeof(struct _neo_rbtree_t), neo_rbtree_dispose);
  tree->root = NULL;
  tree->autofree = false;
  tree->size = 0;
  tree->compare = NULL;
  if (initialize) {
    tree->autofree = initialize->autofree;
    tree->compare = initialize->compare;
  }
  tree->allocator = allocator;
  return tree;
}

static void neo_rbtree_left_rotate(neo_rbtree_t self, neo_rbtree_node_t x) {
  /*
   *     p               p
   *     |               |
   *     x               y
   *    / \    =>       / \
   *   a   y           x   c
   *      / \         / \
   *     b   c       a   b
   */
  neo_rbtree_node_t y = x->right;
  neo_rbtree_node_t b = y->left;
  neo_rbtree_node_t p = x->parent;

  x->right = b;
  if (b) {
    b->parent = x;
  }

  if (p == NULL) {
    self->root = y;
  } else if (p->left == x) {
    p->left = y;
  } else {
    p->right = y;
  }
  y->parent = p;

  y->left = x;
  x->parent = y;
}

static void neo_rbtree_right_rotate(neo_rbtree_t self, neo_rbtree_node_t y) {
  /*
   *      p            p
   *      |            |
   *      y            x
   *     / \    =>    / \
   *    x   c        a   y
   *   / \              / \
   *  a   b            b   c
   */

  neo_rbtree_node_t x = y->left;
  neo_rbtree_node_t b = x->right;
  neo_rbtree_node_t p = y->parent;

  y->left = b;
  if (b) {
    b->parent = y;
  }

  if (p == NULL) {
    self->root = x;
  } else if (p->left == y) {
    p->left = x;
  } else {
    p->right = x;
  }
  x->parent = p;

  x->right = y;
  y->parent = x;
}
static void neo_rbtree_insert_fixup(neo_rbtree_t self, neo_rbtree_node_t node) {
  neo_rbtree_node_t parent, gparent;
  while ((parent = node->parent) && parent && parent->color) {
    gparent = parent->parent;
    if (parent == gparent->left) {
      neo_rbtree_node_t uncle = gparent->right;
      if (uncle && uncle->color) {
        uncle->color = false;
        parent->color = false;
        gparent->color = true;
        node = gparent;
        continue;
      }
      if (parent->right == node) {
        neo_rbtree_left_rotate(self, parent);
        neo_rbtree_node_t tmp = parent;
        parent = node;
        node = tmp;
      }
      parent->color = false;
      gparent->color = true;
      neo_rbtree_right_rotate(self, gparent);
    } else {
      neo_rbtree_node_t uncle = gparent->left;
      if (uncle && uncle->color) {
        uncle->color = false;
        parent->color = false;
        gparent->color = true;
        node = gparent;
        continue;
      }
      if (parent->left == node) {
        neo_rbtree_right_rotate(self, parent);
        neo_rbtree_node_t tmp = parent;
        parent = node;
        node = tmp;
      }
      parent->color = false;
      gparent->color = true;
      neo_rbtree_left_rotate(self, gparent);
    }
  }
  self->root->color = false;
}

static void neo_rbtree_node_dispose(neo_allocator_t allocator,
                                    neo_rbtree_node_t self) {}
static neo_rbtree_node_t neo_create_rbtree_node(neo_allocator_t allocator) {
  neo_rbtree_node_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_rbtree_node_t), neo_rbtree_node_dispose);
  node->color = false;
  node->key = 0;
  node->left = NULL;
  node->right = NULL;
  node->parent = NULL;
  return node;
}

void neo_rbtree_put(neo_rbtree_t self, void *key) {
  neo_rbtree_node_t y = NULL;
  neo_rbtree_node_t x = self->root;
  while (x != NULL) {
    y = x;
    if (self->compare ? self->compare(key, x->key) < 0 : key < x->key) {
      x = x->left;
    } else if (self->compare ? self->compare(key, x->key) == 0
                             : key == x->key) {
      if (x->key != key && self->autofree) {
        neo_allocator_free(self->allocator, x->key);
      }
      x->key = key;
      return;
    } else {
      x = x->right;
    }
  }
  neo_rbtree_node_t node = neo_create_rbtree_node(self->allocator);
  node->key = key;
  node->parent = y;
  if (y != NULL) {
    if (self->compare ? self->compare(node->key, y->key) < 0
                      : node->key < y->key) {
      y->left = node;
    } else {
      y->right = node;
    }
  } else {
    self->root = node;
  }
  node->color = true;
  neo_rbtree_insert_fixup(self, node);
  self->size++;
}
bool neo_rbtree_has(neo_rbtree_t self, const void *key) {
  neo_rbtree_node_t y = NULL;
  neo_rbtree_node_t x = self->root;
  while (x != NULL) {
    y = x;
    if (self->compare ? self->compare(key, x->key) < 0 : key < x->key) {
      x = x->left;
    } else if (self->compare ? self->compare(key, x->key) == 0
                             : key == x->key) {
      return true;
    } else {
      x = x->right;
    }
  }
  return false;
}

static void neo_rbtree_remove_fixup(neo_rbtree_t self, neo_rbtree_node_t node) {
  neo_rbtree_node_t parent = node->parent;
  neo_rbtree_node_t sibling = NULL;
  if (node == parent->left) {
    sibling = parent->right;
    if (!sibling->color) {
      // parent->right[Black]
      if (sibling->right && sibling->right->color) {
        // RR S[Black] parent->right[Black]->right[Red]
        sibling->right->color = sibling->color;
        sibling->color = parent->color;
        parent->color = false;
        neo_rbtree_left_rotate(self, parent);
      } else if (sibling->left && sibling->left->color) {
        // RL
        // parent->right[Black]->left[Red]
        // parent->right[Black]->right?[Black]
        sibling->left->color = parent->color;
        parent->color = false;
        neo_rbtree_right_rotate(self, sibling);
        neo_rbtree_left_rotate(self, parent);
      } else if ((!sibling->left || !sibling->left->color) &&
                 (!sibling->right || !sibling->right->color)) {
        // parent->right[Black]->left?[Black]
        // parent->right[Black]->right?[Black]
        sibling->color = true;
        if (parent->color) {
          parent->color = false;
        } else if (parent != self->root) {
          neo_rbtree_remove_fixup(self, parent);
        }
      }
    } else {
      bool color = sibling->color;
      sibling->color = parent->color;
      parent->color = color;
      neo_rbtree_left_rotate(self, parent);
      neo_rbtree_remove_fixup(self, node);
    }
  } else {
    sibling = parent->left;
    if (!sibling->color) {
      if (sibling->right && sibling->right->color) { // LR
        sibling->right->color = parent->color;
        parent->color = false;
        neo_rbtree_left_rotate(self, sibling);
        neo_rbtree_right_rotate(self, parent);
      } else if (sibling->left && sibling->left->color) { // LL
        sibling->left->color = sibling->color;
        sibling->color = parent->color;
        parent->color = false;
        neo_rbtree_right_rotate(self, parent);
      } else if ((!sibling->left || !sibling->left->color) &&
                 (!sibling->right || !sibling->right->color)) {
        sibling->color = true;
        if (parent->color) {
          parent->color = false;
        } else if (parent != self->root) {
          neo_rbtree_remove_fixup(self, parent);
        }
      }
    } else {
      bool color = sibling->color;
      sibling->color = parent->color;
      parent->color = color;
      neo_rbtree_right_rotate(self, parent);
      neo_rbtree_remove_fixup(self, node);
    }
  }
}

void neo_rbtree_remove(neo_rbtree_t self, const void *key) {
  neo_rbtree_node_t node = self->root;
  while (node) {
    if (self->compare ? self->compare(key, node->key) == 0 : key == node->key) {
      break;
    }
    if (self->compare ? self->compare(key, node->key) < 0 : key < node->key) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  if (!node) {
    return;
  }
  if (node->left && node->right) {
    neo_rbtree_node_t replace = node->right;
    while (replace->left) {
      replace = replace->left;
    }
    neo_rbtree_node_t tmp = node;
    node->key = replace->key;
    replace->key = tmp->key;
    node = replace;
  }
  neo_rbtree_node_t parent = node->parent;
  neo_rbtree_node_t child = NULL;
  if (node->left) {
    child = node->left;
  } else if (node->right) {
    child = node->right;
  }
  if (child) {
    if (!parent) {
      self->root = child;
    } else if (node == parent->left) {
      parent->left = child;
    } else {
      parent->right = child;
    }
    child->parent = parent;
    child->color = false;
  } else if (!parent) {
    self->root = NULL;
  } else {
    if (node->color) {
      if (parent->left == node) {
        parent->left = NULL;
      } else {
        parent->right = NULL;
      }
    } else {
      neo_rbtree_remove_fixup(self, node);
      if (node == parent->left) {
        parent->left = NULL;
      } else if (node == parent->right) {
        parent->right = NULL;
      }
    }
  }
  if (self->autofree) {
    neo_allocator_free(self->allocator, node->key);
  }
  neo_allocator_free(self->allocator, node);
  self->size--;
}
size_t neo_rbtree_size(neo_rbtree_t self) { return self->size; }
neo_rbtree_node_t neo_rbtree_get_first(neo_rbtree_t self) {
  if (self->root) {
    neo_rbtree_node_t node = self->root;
    while (node->left) {
      node = node->left;
    }
    return node;
  }
  return NULL;
}
neo_rbtree_node_t neo_rbtree_get_last(neo_rbtree_t self) {
  if (self->root) {
    neo_rbtree_node_t node = self->root;
    while (node->right) {
      node = node->right;
    }
    return node;
  }
  return NULL;
}
neo_rbtree_node_t neo_rbtree_node_next(neo_rbtree_node_t self) {
  if (self->right) {
    neo_rbtree_node_t node = self->right;
    while (node->left) {
      node = node->left;
    }
    return node;
  }
  if (self->parent) {
    if (self == self->parent->left) {
      return self->parent;
    }
    while (self->parent && self == self->parent->right) {
      self = self->parent;
    }
    return self->parent;
  }
  return NULL;
}
neo_rbtree_node_t neo_rbtree_node_last(neo_rbtree_node_t self) {
  if (self->left) {
    neo_rbtree_node_t node = self->left;
    while (node->right) {
      node = node->right;
    }
    return node;
  }
  if (self->parent) {
    if (self == self->parent->right) {
      return self->parent;
    }
    while (self->parent && self == self->parent->left) {
      self = self->parent;
    }
    return self->parent;
  }
  return NULL;
}
void *neo_rbtree_node_get(neo_rbtree_node_t self) { return self->key; }