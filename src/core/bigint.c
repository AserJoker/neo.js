#include "core/bigint.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/string.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
struct _neo_bigint_t {
  neo_list_t data;
  bool negative;
  neo_allocator_t allocator;
};

static void neo_bigint_push_data(neo_bigint_t bigint, const chunk_t val) {
  chunk_t *v = neo_allocator_alloc(bigint->allocator, sizeof(chunk_t), NULL);
  *v = val;
  neo_list_push(bigint->data, v);
}
static chunk_t *neo_bigint_get_index(neo_bigint_t bigint, size_t idx) {
  neo_list_node_t it = neo_list_get_first(bigint->data);
  for (size_t i = 0; i < idx; i++) {
    it = neo_list_node_next(it);
  }
  return (chunk_t *)neo_list_node_get(it);
}

static void neo_bigint_dispose(neo_allocator_t allocator, neo_bigint_t bigint) {
  neo_allocator_free(allocator, bigint->data);
}

neo_bigint_t neo_create_bigint(neo_allocator_t allocaotr) {
  neo_bigint_t bigint = neo_allocator_alloc(
      allocaotr, sizeof(struct _neo_bigint_t), neo_bigint_dispose);
  neo_list_initialize_t initialize = {true};
  bigint->data = neo_create_list(allocaotr, &initialize);
  bigint->negative = false;
  bigint->allocator = allocaotr;
  neo_bigint_push_data(bigint, 0);
  return bigint;
}

neo_bigint_t neo_number_to_bigint(neo_allocator_t allocator, int64_t number) {
  neo_bigint_t bigint = neo_create_bigint(allocator);
  neo_list_clear(bigint->data);
  static uint64_t max = (uint64_t)((chunk_t)-1) + 1;
  bigint->negative = number < 0;
  if (number < 0) {
    number = -number;
  }
  while (number > 0) {
    neo_bigint_push_data(bigint, (chunk_t)(number % max));
    number >>= (sizeof(chunk_t) * 8);
  }
  if (!neo_list_get_size(bigint->data)) {
    neo_bigint_push_data(bigint, 0);
  }
  return bigint;
}

neo_bigint_t neo_string_to_bigint(neo_allocator_t allocator, const char *val) {
  neo_bigint_t bigint = neo_create_bigint(allocator);
  const char *chr = val;
  if (*chr == L'+') {
    chr++;
  } else if (*chr == L'-') {
    bigint->negative = true;
    chr++;
  }
  while (*chr) {
    if (*chr < '0' || *chr > '9') {
      neo_allocator_free(allocator, bigint);
      return NULL;
    }
    neo_bigint_t another = neo_number_to_bigint(allocator, 10);
    neo_bigint_t result = neo_bigint_mul(bigint, another);
    neo_allocator_free(allocator, another);
    neo_allocator_free(allocator, bigint);
    bigint = result;
    another = neo_number_to_bigint(allocator, *chr - '0');
    result = neo_bigint_add(bigint, another);
    neo_allocator_free(allocator, another);
    neo_allocator_free(allocator, bigint);
    bigint = result;
    chr++;
  }
  if (neo_list_get_size(bigint->data) > 1) {
    chunk_t last =
        *(chunk_t *)neo_list_node_get(neo_list_get_last(bigint->data));
    while (neo_list_get_size(bigint->data) > 1 && last == 0) {
      neo_list_pop(bigint->data);
      last = *(chunk_t *)neo_list_node_get(neo_list_get_last(bigint->data));
    }
  }
  if (!neo_list_get_size(bigint->data)) {
    neo_bigint_push_data(bigint, 0);
  }
  return bigint;
}

neo_bigint_t neo_bigint_clone(neo_bigint_t self) {
  neo_bigint_t bigint = neo_create_bigint(self->allocator);
  neo_list_clear(bigint->data);
  bigint->negative = self->negative;
  for (neo_list_node_t it = neo_list_get_first(self->data);
       it != neo_list_get_tail(self->data); it = neo_list_node_next(it)) {
    neo_bigint_push_data(bigint, *(chunk_t *)neo_list_node_get(it));
  }
  return bigint;
}

char *neo_bigint_to_string(neo_bigint_t bigint, uint32_t radix) {
  size_t max = 128;
  char *result =
      neo_allocator_alloc(bigint->allocator, sizeof(char) * max, NULL);
  result[0] = 0;
  neo_bigint_t tmp = neo_bigint_clone(bigint);
  tmp->negative = false;
  neo_bigint_t another = neo_number_to_bigint(bigint->allocator, 0);
  if (neo_bigint_is_equal(tmp, another)) {
    neo_allocator_free(bigint->allocator, another);
    result[0] = '0';
    result[1] = 0;
  } else {
    neo_bigint_t mod = neo_number_to_bigint(bigint->allocator, radix);
    while (neo_bigint_is_greater(tmp, another)) {
      neo_bigint_t mod_res = neo_bigint_mod(tmp, mod);
      chunk_t val =
          *(chunk_t *)(neo_list_node_get(neo_list_get_first(mod_res->data)));
      char s[2] = {val + '0', 0};
      if (val >= 10) {
        s[0] = val - 10 + 'a';
      }
      result = neo_string_concat(bigint->allocator, result, &max, s);
      neo_allocator_free(bigint->allocator, mod_res);
      mod_res = neo_bigint_div(tmp, mod);
      neo_allocator_free(bigint->allocator, tmp);
      tmp = mod_res;
    }
    neo_allocator_free(bigint->allocator, mod);
    neo_allocator_free(bigint->allocator, another);
  }
  neo_allocator_free(bigint->allocator, tmp);
  if (bigint->negative) {
    result = neo_string_concat(bigint->allocator, result, &max, "-");
  }
  size_t len = strlen(result);
  for (size_t idx = 0; idx < len / 2; idx++) {
    char tmp = result[idx];
    result[idx] = result[len - 1 - idx];
    result[len - 1 - idx] = tmp;
  }
  return result;
}

uint16_t *neo_bigint_to_string16(neo_bigint_t bigint, uint32_t radix) {
  size_t max = 128;
  uint16_t *result =
      neo_allocator_alloc(bigint->allocator, sizeof(uint16_t) * max, NULL);
  result[0] = 0;
  neo_bigint_t tmp = neo_bigint_clone(bigint);
  tmp->negative = false;
  neo_bigint_t another = neo_number_to_bigint(bigint->allocator, 0);
  if (neo_bigint_is_equal(tmp, another)) {
    neo_allocator_free(bigint->allocator, another);
    result[0] = '0';
    result[1] = 0;
  } else {
    neo_bigint_t mod = neo_number_to_bigint(bigint->allocator, radix);
    while (neo_bigint_is_greater(tmp, another)) {
      neo_bigint_t mod_res = neo_bigint_mod(tmp, mod);
      chunk_t val =
          *(chunk_t *)(neo_list_node_get(neo_list_get_first(mod_res->data)));
      uint16_t s[2] = {val + '0', 0};
      if (val >= 10) {
        s[0] = val - 10 + 'a';
      }
      result = neo_string16_concat(bigint->allocator, result, &max, s);
      neo_allocator_free(bigint->allocator, mod_res);
      mod_res = neo_bigint_div(tmp, mod);
      neo_allocator_free(bigint->allocator, tmp);
      tmp = mod_res;
    }
    neo_allocator_free(bigint->allocator, mod);
    neo_allocator_free(bigint->allocator, another);
  }
  neo_allocator_free(bigint->allocator, tmp);
  if (bigint->negative) {
    uint16_t symbol[2] = {'-', 0};
    result = neo_string16_concat(bigint->allocator, result, &max, symbol);
  }
  size_t len = neo_string16_length(result);
  for (size_t idx = 0; idx < len / 2; idx++) {
    char tmp = result[idx];
    result[idx] = result[len - 1 - idx];
    result[len - 1 - idx] = tmp;
  }
  return result;
}

double neo_bigint_to_number(neo_bigint_t bigint) {
  int64_t result = 0;
  uint32_t step = 0;
  if (neo_list_get_size(bigint->data) == 1) {
    result = *(chunk_t *)neo_list_node_get(neo_list_get_first(bigint->data));
  } else {
    for (neo_list_node_t it = neo_list_get_first(bigint->data);
         it != neo_list_get_tail(bigint->data); it = neo_list_node_next(it)) {
      int64_t tmp = *(chunk_t *)neo_list_node_get(it);
      tmp <<= step;
      step += sizeof(chunk_t) * 8;
      if (step > 63) {
        return INFINITY;
      }
      result |= tmp;
    }
  }
  if (bigint->negative) {
    result = -result;
  }
  return result;
}

bool neo_bigint_is_negative(neo_bigint_t bigint) { return bigint->negative; }

neo_bigint_t neo_bigint_abs(neo_bigint_t self) {
  neo_bigint_t res = neo_bigint_clone(self);
  res->negative = false;
  return res;
}

static neo_bigint_t neo_bigint_uadd(neo_bigint_t self, neo_bigint_t another) {
  static uint64_t max = (uint64_t)(((chunk_t)-1) + 1);
  size_t index = 0;
  uint64_t next = 0;
  neo_bigint_t result = neo_create_bigint(self->allocator);
  while (next != 0 || neo_list_get_size(self->data) > index ||
         neo_list_get_size(another->data) > index) {
    uint64_t val = next;
    if (index < neo_list_get_size(self->data)) {
      val += *neo_bigint_get_index(self, index);
    }
    if (index < neo_list_get_size(another->data)) {
      val += *neo_bigint_get_index(another, index);
    }
    if (index >= neo_list_get_size(result->data)) {
      neo_bigint_push_data(result, 0);
    }
    *neo_bigint_get_index(result, index) = (chunk_t)(val % max);
    next = val / max;
    index++;
  }
  return result;
}
static neo_bigint_t neo_bigint_usub(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t left = neo_bigint_abs(self);
  neo_bigint_t right = neo_bigint_abs(another);
  neo_bigint_t a = left;
  neo_bigint_t b = right;
  if (neo_bigint_is_greater(right, left)) {
    a = right;
    b = left;
  }
  int64_t next = 0;
  neo_bigint_t result = neo_create_bigint(self->allocator);
  neo_list_clear(result->data);
  for (size_t index = 0; index < neo_list_get_size(a->data); index++) {
    uint64_t pa = *neo_bigint_get_index(a, index);
    pa += next;
    if (index < neo_list_get_size(b->data)) {
      uint64_t pb = *neo_bigint_get_index(b, index);
      if (pa < pb) {
        pa += 1 << sizeof(chunk_t) * 8;
        next = -1;
      }
      neo_bigint_push_data(result, ((chunk_t)(pa - pb)));
    } else {
      neo_bigint_push_data(result, ((chunk_t)pa));
    }
  }
  if (neo_list_get_size(result->data) > 1) {
    chunk_t last =
        *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    while (neo_list_get_size(result->data) > 1 && last == 0) {
      neo_list_pop(result->data);
      last = *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    }
  }
  neo_allocator_free(self->allocator, a);
  neo_allocator_free(self->allocator, b);
  return result;
}

neo_bigint_t neo_bigint_add(neo_bigint_t self, neo_bigint_t another) {
  if (self->negative == another->negative) {
    neo_bigint_t result = neo_bigint_uadd(self, another);
    result->negative = self->negative;
    return result;
  } else {
    neo_bigint_t ano = neo_bigint_clone(another);
    ano->negative = !ano->negative;
    neo_bigint_t result = neo_bigint_sub(self, ano);
    neo_allocator_free(self->allocator, ano);
    return result;
  }
}

neo_bigint_t neo_bigint_sub(neo_bigint_t self, neo_bigint_t another) {
  if (self->negative == another->negative) {
    neo_bigint_t result = neo_bigint_usub(self, another);
    result->negative = neo_bigint_is_greater(another, self);
    return result;
  } else {
    neo_bigint_t ano = neo_bigint_clone(another);
    ano->negative = !ano->negative;
    neo_bigint_t result = neo_bigint_add(self, ano);
    neo_allocator_free(self->allocator, ano);
    return result;
  }
}

neo_bigint_t neo_bigint_mul(neo_bigint_t self, neo_bigint_t another) {
  static uint64_t max = (uint64_t)((chunk_t)-1) + 1;
  neo_bigint_t result = neo_create_bigint(self->allocator);
  uint64_t rindex = 0;
  for (neo_list_node_t it = neo_list_get_first(another->data);
       it != neo_list_get_tail(another->data); it = neo_list_node_next(it)) {
    chunk_t *part = neo_list_node_get(it);
    uint64_t next = 0;
    for (size_t index = 0; index < neo_list_get_size(self->data); index++) {
      chunk_t *src = neo_bigint_get_index(self, index);
      if (index + rindex >= neo_list_get_size(result->data)) {
        neo_bigint_push_data(result, 0);
      }
      uint64_t val = *part * *src + next;
      *neo_bigint_get_index(result, index + rindex) += (chunk_t)(val % max);
      next = val / max;
    }
    if (next > 0) {
      neo_bigint_push_data(result, (chunk_t)next);
    }
    rindex++;
  }
  if (neo_list_get_size(result->data) > 1) {
    chunk_t last =
        *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    while (neo_list_get_size(result->data) > 1 && last == 0) {
      neo_list_pop(result->data);
      last = *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    }
  }
  result->negative = self->negative != another->negative;
  return result;
}

neo_bigint_t neo_bigint_div(neo_bigint_t self, neo_bigint_t another) {
  if (neo_list_get_size(another->data) == 1 &&
      *(chunk_t *)neo_list_node_get(neo_list_get_first(another->data)) == 0) {
    return NULL;
  }
  if (neo_bigint_is_greater(another, self)) {
    return neo_number_to_bigint(self->allocator, 0);
  }
  if (neo_bigint_is_equal(another, self)) {
    return neo_number_to_bigint(self->allocator, 1);
  }
  size_t len = neo_list_get_size(self->data) - neo_list_get_size(another->data);
  neo_bigint_t next = neo_create_bigint(self->allocator);
  neo_bigint_t result = neo_create_bigint(self->allocator);
  neo_list_t rdata = neo_create_list(self->allocator, NULL);
  for (size_t index = 0; index <= len; index++) {
    neo_bigint_t tmp = neo_create_bigint(self->allocator);
    neo_list_clear(tmp->data);
    neo_list_t data = neo_create_list(self->allocator, NULL);
    for (size_t i = 0; i < neo_list_get_size(another->data); i++) {
      neo_list_push(data,
                    neo_bigint_get_index(self, neo_list_get_size(self->data) -
                                                   1 - index - i));
    }
    for (neo_list_node_t it = neo_list_get_last(data);
         it != neo_list_get_head(data); it = neo_list_node_last(it)) {
      chunk_t *data = neo_list_node_get(it);
      neo_bigint_push_data(tmp, *data);
    }
    neo_bigint_t mul_arg = neo_number_to_bigint(
        self->allocator, (int64_t)pow(2, sizeof(chunk_t) * 8));
    neo_bigint_t mul_res = neo_bigint_mul(next, mul_arg);
    neo_allocator_free(self->allocator, mul_arg);
    neo_bigint_t add_res = neo_bigint_add(tmp, mul_res);
    neo_allocator_free(self->allocator, mul_res);
    neo_allocator_free(self->allocator, tmp);
    tmp = add_res;
    chunk_t *v = neo_allocator_alloc(self->allocator, sizeof(chunk_t), NULL);
    *v = 0;
    while (neo_bigint_is_greater(tmp, another) ||
           neo_bigint_is_equal(tmp, another)) {
      neo_bigint_t res = neo_bigint_sub(tmp, another);
      neo_allocator_free(self->allocator, tmp);
      tmp = res;
      (*v)++;
    }
    neo_allocator_free(self->allocator, next);
    next = tmp;
    neo_list_push(rdata, v);
    neo_allocator_free(self->allocator, data);
  }
  neo_list_clear(result->data);
  while (neo_list_get_size(rdata) > 1 &&
         *(chunk_t *)neo_list_node_get(neo_list_get_first(rdata)) == 0) {
    neo_list_node_t it = neo_list_get_first(rdata);
    chunk_t *val = neo_list_node_get(it);
    neo_allocator_free(self->allocator, val);
    neo_list_erase(rdata, it);
  }
  for (neo_list_node_t it = neo_list_get_last(rdata);
       it != neo_list_get_head(rdata); it = neo_list_node_last(it)) {
    neo_list_push(result->data, neo_list_node_get(it));
  }
  neo_allocator_free(self->allocator, rdata);
  neo_allocator_free(self->allocator, next);
  return result;
}
neo_bigint_t neo_bigint_mod(neo_bigint_t self, neo_bigint_t another) {
  if (neo_list_get_size(another->data) == 1 &&
      *(chunk_t *)neo_list_node_get(neo_list_get_first(another->data)) == 0) {
    return NULL;
  }
  if (neo_bigint_is_greater(another, self)) {
    return neo_bigint_clone(self);
  }
  if (neo_bigint_is_equal(self, another)) {
    return neo_number_to_bigint(self->allocator, 0);
  }
  size_t len = neo_list_get_size(self->data) - neo_list_get_size(another->data);
  neo_bigint_t next = neo_create_bigint(self->allocator);
  for (size_t index = 0; index <= len; index++) {
    neo_bigint_t tmp = neo_create_bigint(self->allocator);
    neo_list_clear(tmp->data);
    neo_list_t data = neo_create_list(self->allocator, NULL);
    for (size_t i = 0; i < neo_list_get_size(another->data); i++) {
      chunk_t *val = neo_bigint_get_index(self, neo_list_get_size(self->data) -
                                                    1 - index - i);
      neo_list_push(data, val);
    }
    for (neo_list_node_t it = neo_list_get_last(data);
         it != neo_list_get_head(data); it = neo_list_node_last(it)) {
      chunk_t *val = neo_list_node_get(it);
      neo_bigint_push_data(tmp, *val);
    }
    neo_bigint_t mul_arg = neo_number_to_bigint(
        self->allocator, (int64_t)pow(2, sizeof(chunk_t) * 8));
    neo_bigint_t mul_res = neo_bigint_mul(next, mul_arg);
    neo_allocator_free(self->allocator, mul_arg);
    neo_bigint_t add_res = neo_bigint_add(tmp, mul_res);
    neo_allocator_free(self->allocator, mul_res);
    neo_allocator_free(self->allocator, tmp);
    tmp = add_res;
    while (neo_bigint_is_greater(tmp, another) ||
           neo_bigint_is_equal(tmp, another)) {
      neo_bigint_t res = neo_bigint_sub(tmp, another);
      neo_allocator_free(self->allocator, tmp);
      tmp = res;
    }
    neo_allocator_free(self->allocator, next);
    next = tmp;
    neo_allocator_free(self->allocator, data);
  }
  next->negative = self->negative;
  return next;
}

neo_bigint_t neo_bigint_pow(neo_bigint_t self, neo_bigint_t another) {

  neo_bigint_t result = neo_number_to_bigint(self->allocator, 1);
  neo_bigint_t i = neo_number_to_bigint(self->allocator, 0);
  while (neo_bigint_is_greater(another, i)) {
    neo_bigint_t res = neo_bigint_mul(result, self);
    neo_allocator_free(self->allocator, result);
    result = res;
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(i, add_arg);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, i);
    i = add_res;
  }
  neo_allocator_free(self->allocator, i);
  return result;
}

neo_bigint_t neo_bigint_and(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t result = neo_create_bigint(self->allocator);
  neo_list_clear(result->data);
  size_t index = 0;
  neo_bigint_t a = neo_bigint_clone(self);
  neo_bigint_t b = neo_bigint_clone(another);
  neo_bigint_t max = neo_create_bigint(self->allocator);
  neo_list_clear(max->data);
  while (neo_list_get_size(a->data) > index ||
         neo_list_get_size(b->data) > index) {
    neo_bigint_push_data(max, (chunk_t)-1);
    index++;
  }
  if (a->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t add_res_2 = neo_bigint_add(add_res, a);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, a);
    a = add_res_2;
  }
  if (b->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t add_res_2 = neo_bigint_add(add_res, b);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, b);
    b = add_res_2;
  }
  index = 0;
  while (neo_list_get_size(a->data) > index ||
         neo_list_get_size(b->data) > index) {
    chunk_t val1 = 0;
    chunk_t val2 = 0;
    if (index < neo_list_get_size(a->data)) {
      val1 = *neo_bigint_get_index(a, index);
    }
    if (index < neo_list_get_size(b->data)) {
      val2 = *neo_bigint_get_index(b, index);
    }
    neo_bigint_push_data(result, val1 & val2);
    index++;
  }
  if (neo_list_get_size(result->data) > 1) {
    chunk_t last =
        *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    while (neo_list_get_size(result->data) > 1 && last == 0) {
      neo_list_pop(result->data);
      last = *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    }
  }
  if (neo_list_get_size(result->data)) {
    neo_bigint_push_data(result, 0);
  }
  if (self->negative && another->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t sub_res = neo_bigint_sub(add_res, result);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, result);
    result = sub_res;
  }
  result->negative = self->negative && another->negative;
  neo_allocator_free(self->allocator, a);
  neo_allocator_free(self->allocator, b);
  neo_allocator_free(self->allocator, max);
  return result;
}

neo_bigint_t neo_bigint_or(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t result = neo_create_bigint(self->allocator);
  neo_list_clear(result->data);
  size_t index = 0;
  neo_bigint_t a = neo_bigint_clone(self);
  neo_bigint_t b = neo_bigint_clone(another);
  neo_bigint_t max = neo_create_bigint(self->allocator);
  neo_list_clear(max->data);
  while (neo_list_get_size(a->data) > index ||
         neo_list_get_size(b->data) > index) {
    neo_bigint_push_data(max, (chunk_t)-1);
    index++;
  }
  if (a->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t add_res_2 = neo_bigint_add(add_res, a);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, a);
    a = add_res_2;
  }
  if (b->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t add_res_2 = neo_bigint_add(add_res, b);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, b);
    b = add_res_2;
  }
  index = 0;
  while (neo_list_get_size(a->data) > index ||
         neo_list_get_size(b->data) > index) {
    chunk_t val1 = 0;
    chunk_t val2 = 0;
    if (index < neo_list_get_size(a->data)) {
      val1 = *neo_bigint_get_index(a, index);
    }
    if (index < neo_list_get_size(b->data)) {
      val2 = *neo_bigint_get_index(b, index);
    }
    neo_bigint_push_data(result, val1 | val2);
    index++;
  }
  if (neo_list_get_size(result->data) > 1) {
    chunk_t last =
        *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    while (neo_list_get_size(result->data) > 1 && last == 0) {
      neo_list_pop(result->data);
      last = *(chunk_t *)neo_list_node_get(neo_list_get_last(result->data));
    }
  }
  if (neo_list_get_size(result->data)) {
    neo_bigint_push_data(result, 0);
  }
  if (self->negative && another->negative) {
    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(max, add_arg);
    neo_bigint_t sub_res = neo_bigint_sub(add_res, result);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, add_res);
    neo_allocator_free(self->allocator, result);
    result = sub_res;
  }
  result->negative = self->negative && another->negative;
  neo_allocator_free(self->allocator, a);
  neo_allocator_free(self->allocator, b);
  neo_allocator_free(self->allocator, max);
  return result;
}

neo_bigint_t neo_bigint_xor(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t not_1 = neo_bigint_not(another);
  neo_bigint_t not_2 = neo_bigint_not(self);
  neo_bigint_t and_1 = neo_bigint_and(self, not_1);
  neo_bigint_t and_2 = neo_bigint_and(not_2, another);
  neo_bigint_t result = neo_bigint_or(and_1, and_2);
  neo_allocator_free(self->allocator, not_1);
  neo_allocator_free(self->allocator, not_2);
  neo_allocator_free(self->allocator, and_1);
  neo_allocator_free(self->allocator, and_2);
  return result;
}

neo_bigint_t neo_bigint_shr(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t result = neo_bigint_clone(self);
  neo_bigint_t idx = neo_number_to_bigint(self->allocator, 0);
  while (neo_bigint_is_greater(another, idx)) {
    neo_bigint_t div_arg = neo_number_to_bigint(self->allocator, 2);
    neo_bigint_t div_res = neo_bigint_div(result, div_arg);
    neo_allocator_free(self->allocator, div_arg);
    neo_allocator_free(self->allocator, result);
    result = div_res;

    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(idx, add_arg);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, idx);
    idx = add_res;
  }
  neo_allocator_free(self->allocator, idx);
  return result;
}

neo_bigint_t neo_bigint_shl(neo_bigint_t self, neo_bigint_t another) {
  neo_bigint_t result = neo_bigint_clone(self);
  neo_bigint_t idx = neo_number_to_bigint(self->allocator, 0);
  while (neo_bigint_is_greater(another, idx)) {
    neo_bigint_t mul_arg = neo_number_to_bigint(self->allocator, 2);
    neo_bigint_t mul_res = neo_bigint_mul(result, mul_arg);
    neo_allocator_free(self->allocator, mul_arg);
    neo_allocator_free(self->allocator, result);
    result = mul_res;

    neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
    neo_bigint_t add_res = neo_bigint_add(idx, add_arg);
    neo_allocator_free(self->allocator, add_arg);
    neo_allocator_free(self->allocator, idx);
    idx = add_res;
  }
  neo_allocator_free(self->allocator, idx);
  return result;
}

neo_bigint_t neo_bigint_not(neo_bigint_t self) {
  neo_bigint_t add_arg = neo_number_to_bigint(self->allocator, 1);
  neo_bigint_t result = neo_bigint_add(self, add_arg);
  neo_allocator_free(self->allocator, add_arg);
  result->negative = !result->negative;
  return result;
}

neo_bigint_t neo_bigint_neg(neo_bigint_t self) {
  neo_bigint_t result = neo_bigint_clone(self);
  result->negative = !self->negative;
  return result;
}

bool neo_bigint_is_equal(neo_bigint_t self, neo_bigint_t another) {
  if (neo_list_get_size(self->data) != neo_list_get_size(another->data)) {
    return false;
  }
  if (self->negative != another->negative) {
    return false;
  }
  neo_list_node_t it1 = neo_list_get_first(self->data);
  neo_list_node_t it2 = neo_list_get_first(another->data);
  for (size_t idx = 0; idx < neo_list_get_size(self->data); idx++) {
    if (*(chunk_t *)neo_list_node_get(it1) !=
        *(chunk_t *)neo_list_node_get(it2)) {
      return false;
    }
    it1 = neo_list_node_next(it1);
    it2 = neo_list_node_next(it2);
  }
  return true;
}

bool neo_bigint_is_greater(neo_bigint_t self, neo_bigint_t another) {
  if (!self->negative && another->negative) {
    return true;
  } else if (self->negative && !another->negative) {
    return false;
  }
  if (neo_list_get_size(self->data) > neo_list_get_size(another->data)) {
    return !self->negative;
  } else if (neo_list_get_size(self->data) < neo_list_get_size(another->data)) {
    return self->negative;
  }
  size_t size = neo_list_get_size(self->data);
  for (size_t index = 0; index < neo_list_get_size(self->data); index++) {
    chunk_t a = *neo_bigint_get_index(self, size - 1 - index);
    chunk_t b = *neo_bigint_get_index(another, size - 1 - index);
    if (a > b) {
      return !self->negative;
    } else if (a < b) {
      return self->negative;
    }
  }
  return false;
}

bool neo_bigint_is_less(neo_bigint_t self, neo_bigint_t another) {
  if (!self->negative && another->negative) {
    return false;
  } else if (self->negative && !another->negative) {
    return true;
  }
  if (neo_list_get_size(self->data) > neo_list_get_size(another->data)) {
    return self->negative;
  } else if (neo_list_get_size(self->data) < neo_list_get_size(another->data)) {
    return !self->negative;
  }
  size_t size = neo_list_get_size(self->data);
  for (size_t index = 0; index < neo_list_get_size(self->data); index++) {
    chunk_t a = *neo_bigint_get_index(self, size - 1 - index);
    chunk_t b = *neo_bigint_get_index(another, size - 1 - index);
    if (a > b) {
      return self->negative;
    } else if (a < b) {
      return !self->negative;
    }
  }
  return false;
}

bool neo_bigint_is_greater_or_equal(neo_bigint_t self, neo_bigint_t another) {
  if (!self->negative && another->negative) {
    return true;
  } else if (self->negative && !another->negative) {
    return false;
  }
  if (neo_list_get_size(self->data) > neo_list_get_size(another->data)) {
    return !self->negative;
  } else if (neo_list_get_size(self->data) < neo_list_get_size(another->data)) {
    return self->negative;
  }
  size_t size = neo_list_get_size(self->data);
  for (size_t index = 0; index < neo_list_get_size(self->data); index++) {
    chunk_t a = *neo_bigint_get_index(self, size - 1 - index);
    chunk_t b = *neo_bigint_get_index(another, size - 1 - index);
    if (a > b) {
      return !self->negative;
    } else if (a < b) {
      return self->negative;
    }
  }
  return true;
}

bool neo_bigint_is_less_or_equal(neo_bigint_t self, neo_bigint_t another) {
  if (!self->negative && another->negative) {
    return false;
  } else if (self->negative && !another->negative) {
    return true;
  }
  if (neo_list_get_size(self->data) > neo_list_get_size(another->data)) {
    return self->negative;
  } else if (neo_list_get_size(self->data) < neo_list_get_size(another->data)) {
    return !self->negative;
  }
  size_t size = neo_list_get_size(self->data);
  for (size_t index = 0; index < neo_list_get_size(self->data); index++) {
    chunk_t a = *neo_bigint_get_index(self, size - 1 - index);
    chunk_t b = *neo_bigint_get_index(another, size - 1 - index);
    if (a > b) {
      return self->negative;
    } else if (a < b) {
      return !self->negative;
    }
  }
  return true;
}