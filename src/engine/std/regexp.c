#include "engine/std/regexp.h"
#include "core/allocator.h"
#include "engine/basetype/object.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef struct _neo_js_regex_t {
  pcre2_code *code;
  uint8_t flag;
  char *source;
  size_t lastindex;
} *neo_js_regex_t;
uint8_t neo_js_regexp_get_flag(neo_js_context_t ctx, neo_js_variable_t self) {
  neo_js_regex_t regex = neo_js_context_get_opaque(ctx, self, "[[regex]]");
  return regex->flag;
}

static void neo_js_regex_dispose(neo_allocator_t allocator,
                                 neo_js_regex_t self) {
  pcre2_code_free(self->code);
  self->code = NULL;
  neo_allocator_free(allocator, self->source);
}
neo_js_variable_t neo_js_regexp_constructor(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv) {
  const char *rule = "(?:)";
  const char *s_flag = "";
  if (argc > 0) {
    neo_js_variable_t v_rule = neo_js_context_to_string(ctx, argv[0]);
    if (neo_js_variable_get_type(v_rule)->kind == NEO_JS_TYPE_ERROR) {
      return v_rule;
    }
    rule = neo_js_context_to_cstring(ctx, v_rule);
  }
  if (argc > 1) {
    neo_js_variable_t v_flag = neo_js_context_to_string(ctx, argv[1]);
    if (neo_js_variable_get_type(v_flag)->kind == NEO_JS_TYPE_ERROR) {
      return v_flag;
    }
    s_flag = neo_js_context_to_cstring(ctx, v_flag);
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint8_t flag = 0;
  const char *pflag = s_flag;
  while (*pflag != 0) {
    switch (*pflag) {
    case 'g':
      flag |= NEO_REGEXP_FLAG_GLOBAL;
      break;
    case 'i':
      flag |= NEO_REGEXP_FLAG_IGNORECASE;
      break;
    case 'm':
      flag |= NEO_REGEXP_FLAG_MULTILINE;
      break;
    case 's':
      flag |= NEO_REGEXP_FLAG_DOTALL;
      break;
    case 'u':
      flag |= NEO_REGEXP_FLAG_UNICODE;
      break;
    case 'd':
      flag |= NEO_REGEXP_FLAG_HAS_INDICES;
      break;
    case 'y':
      flag |= NEO_REGEXP_FLAG_STICKY;
      break;
    default: {
      size_t len = strlen(s_flag) + 128;
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_SYNTAX, len,
          "Invalid flags supplied to RegExp constructor '%s'", s_flag);
    }
    }
    pflag++;
  }
  uint32_t compile_options = 0;
  int error_code = 0;
  PCRE2_SIZE error_offset = 0;
  if (flag & NEO_REGEXP_FLAG_IGNORECASE) {
    compile_options |= PCRE2_CASELESS;
  }
  if (flag & NEO_REGEXP_FLAG_MULTILINE) {
    compile_options |= PCRE2_MULTILINE;
  }
  if (flag & NEO_REGEXP_FLAG_DOTALL) {
    compile_options |= PCRE2_DOTALL;
  }
  if (flag & NEO_REGEXP_FLAG_UNICODE) {
    compile_options |= PCRE2_UTF | PCRE2_UCP;
  }
  compile_options |= PCRE2_DUPNAMES;
  pcre2_code *code =
      pcre2_compile((PCRE2_SPTR)rule, PCRE2_ZERO_TERMINATED, compile_options,
                    &error_code, &error_offset, NULL);
  if (!code) {
    size_t len = strlen(s_flag) + 128;
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_SYNTAX, len,
        "Invalid regular expression: /%s/%s: Unterminated character class",
        rule, s_flag);
  }
  neo_js_regex_t regex = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_regex_t), neo_js_regex_dispose);
  regex->code = code;
  regex->flag = flag;

  size_t len = strlen(rule) + strlen(s_flag) + 8;
  char *str = neo_allocator_alloc(allocator, sizeof(char) * len, NULL);
  snprintf(str, len, "/%s/%s", rule, s_flag);
  regex->source = str;
  neo_js_context_set_opaque(ctx, self, "[[regex]]", regex);
  neo_js_variable_t prototype = neo_js_object_get_prototype(ctx, self);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "dotAll"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_DOTALL), true,
      false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "global"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_GLOBAL), true,
      false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "hasIndices"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_HAS_INDICES),
      true, false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "ignoreCase"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_IGNORECASE),
      true, false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "multiline"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_MULTILINE),
      true, false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "sticky"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_STICKY), true,
      false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "unicode"),
      neo_js_context_create_boolean(ctx, flag & NEO_REGEXP_FLAG_UNICODE), true,
      false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "source"),
      neo_js_context_create_string(ctx, rule), true, false, false);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "flag"),
      neo_js_context_create_string(ctx, s_flag), true, false, false);

  neo_js_context_def_field(
      ctx, self, neo_js_context_create_string(ctx, "lastIndex"),
      neo_js_context_create_number(ctx, 0), false, false, true);

  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_regexp_to_string(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv) {
  neo_js_regex_t regex = neo_js_context_get_opaque(ctx, self, "[[regex]]");
  return neo_js_context_create_string(ctx, regex->source);
}

neo_js_variable_t neo_js_regexp_exec(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  neo_js_regex_t regex = neo_js_context_get_opaque(ctx, self, "[[regex]]");
  const char *str = "";
  if (argc) {
    neo_js_variable_t v_str = neo_js_context_to_string(ctx, argv[0]);
    str = neo_js_context_to_cstring(ctx, v_str);
  } else {
    return neo_js_context_create_null(ctx);
  }
  neo_js_number_t last_index = NULL;
  uint32_t match_options = 0;
  if (regex->flag & NEO_REGEXP_FLAG_STICKY) {
    match_options |= PCRE2_ANCHORED;
  }
  if (regex->flag & NEO_REGEXP_FLAG_GLOBAL ||
      regex->flag & NEO_REGEXP_FLAG_STICKY) {
    neo_js_variable_t v_last_index = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "lastIndex"), NULL);
    if (neo_js_variable_get_type(v_last_index)->kind != NEO_JS_TYPE_NUMBER) {
      v_last_index = neo_js_context_to_number(ctx, v_last_index);
      neo_js_context_set_field(ctx, self,
                               neo_js_context_create_string(ctx, "lastIndex"),
                               v_last_index, NULL);
    }
    last_index = neo_js_variable_to_number(v_last_index);

    if (isnan(last_index->number) || isinf(last_index->number) ||
        last_index->number > strlen(str) || last_index->number < 0) {
      last_index->number = 0;
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  neo_js_variable_t result = NULL;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  pcre2_match_data *match_data =
      pcre2_match_data_create_from_pattern(regex->code, NULL);
  int rc = pcre2_match(regex->code, (PCRE2_SPTR)str, PCRE2_ZERO_TERMINATED,
                       last_index->number, match_options, match_data, NULL);
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  if (regex->flag & NEO_REGEXP_FLAG_GLOBAL ||
      regex->flag & NEO_REGEXP_FLAG_STICKY) {
    if (rc > 0) {
      last_index->number = ovector[rc * 2 + 1];
    } else {
      last_index->number = 0;
    }
  }
  if (rc > 0) {
    result = neo_js_context_create_array(ctx);
    size_t start = ovector[0];
    size_t end = ovector[(rc - 1) * 2 + 1];
    size_t len = end - start;
    char *part = neo_allocator_alloc(allocator, (len + 1) * sizeof(char), NULL);
    strncpy(part, str + start, len);
    part[len] = 0;
    neo_js_context_set_field(ctx, result, neo_js_context_create_number(ctx, 0),
                             neo_js_context_create_string(ctx, part), NULL);
    neo_allocator_free(allocator, part);
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "index"),
                             neo_js_context_create_number(ctx, start), NULL);
    neo_js_context_set_field(
        ctx, result, neo_js_context_create_string(ctx, "input"), argv[0], NULL);
    neo_js_variable_t indices = NULL;
    if (regex->flag & NEO_REGEXP_FLAG_HAS_INDICES) {
      indices = neo_js_context_create_array(ctx);
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, "indices"),
                               indices, NULL);
    }
    for (size_t idx = 0; idx < rc; idx++) {
      size_t start = ovector[idx * 2];
      size_t end = ovector[idx * 2 + 1];
      char *part =
          neo_allocator_alloc(allocator, (len + 1) * sizeof(char), NULL);
      strncpy(part, str + start, len);
      part[len] = 0;
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_number(ctx, idx + 1),
                               neo_js_context_create_string(ctx, part), NULL);
      neo_allocator_free(allocator, part);
      if (regex->flag & NEO_REGEXP_FLAG_HAS_INDICES) {
        neo_js_variable_t item = neo_js_context_create_array(ctx);
        neo_js_context_set_field(
            ctx, item, neo_js_context_create_number(ctx, 0),
            neo_js_context_create_number(ctx, start), NULL);
        neo_js_context_set_field(ctx, item,
                                 neo_js_context_create_number(ctx, 1),
                                 neo_js_context_create_number(ctx, end), NULL);
        neo_js_context_set_field(
            ctx, indices, neo_js_context_create_number(ctx, idx), item, NULL);
      }
    }
    size_t ngroups = 0;
    pcre2_pattern_info(regex->code, PCRE2_INFO_NAMECOUNT, &ngroups);
    size_t name_entry_size = 0;
    pcre2_pattern_info(regex->code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
    PCRE2_SPTR name_table = NULL;
    pcre2_pattern_info(regex->code, PCRE2_INFO_NAMETABLE, &name_table);
    if (ngroups) {
      neo_js_variable_t groups = neo_js_context_create_object(ctx, NULL);
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, "groups"),
                               groups, NULL);
      neo_js_variable_t igroups = neo_js_context_create_object(ctx, NULL);
      neo_js_context_set_field(ctx, indices,
                               neo_js_context_create_string(ctx, "groups"),
                               igroups, NULL);
      PCRE2_SPTR tabptr = name_table;
      for (uint32_t i = 0; i < ngroups; i++) {
        int n = tabptr[0];
        size_t start = ovector[2 * n];
        size_t end = ovector[2 * n + 1];
        size_t len = name_entry_size - 2;
        PCRE2_SPTR name = tabptr + 1;
        char *s_name =
            neo_allocator_alloc(allocator, sizeof(char) * (len + 1), NULL);
        size_t idx = 0;
        for (idx = 0; name[idx] != 0; idx++) {
          s_name[idx] = name[idx];
        }
        s_name[idx] = 0;
        len = end - start;
        char *part =
            neo_allocator_alloc(allocator, (len + 1) * sizeof(char), NULL);
        strncpy(part, str + start, len);
        part[len] = 0;
        neo_js_context_set_field(ctx, groups,
                                 neo_js_context_create_string(ctx, s_name),
                                 neo_js_context_create_string(ctx, part), NULL);
        neo_allocator_free(allocator, part);
        neo_js_variable_t item = neo_js_context_create_array(ctx);
        neo_js_context_set_field(
            ctx, item, neo_js_context_create_number(ctx, 0),
            neo_js_context_create_number(ctx, start), NULL);
        neo_js_context_set_field(ctx, item,
                                 neo_js_context_create_number(ctx, 1),
                                 neo_js_context_create_number(ctx, end), NULL);
        neo_js_context_set_field(ctx, igroups,
                                 neo_js_context_create_string(ctx, s_name),
                                 item, NULL);
        neo_allocator_free(allocator, s_name);
        tabptr += name_entry_size;
      }
    } else {
      neo_js_context_set_field(ctx, result,
                               neo_js_context_create_string(ctx, "groups"),
                               neo_js_context_create_undefined(ctx), NULL);
    }
  }
  pcre2_match_data_free(match_data);
  if (result) {
    return result;
  }
  return neo_js_context_create_null(ctx);
}

neo_js_variable_t neo_js_regexp_test(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  neo_js_regex_t regex = neo_js_context_get_opaque(ctx, self, "[[regex]]");
  const char *str = "";
  if (argc) {
    neo_js_variable_t v_str = neo_js_context_to_string(ctx, argv[0]);
    str = neo_js_context_to_cstring(ctx, v_str);
  } else {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_number_t last_index = NULL;
  uint32_t match_options = 0;
  if (regex->flag & NEO_REGEXP_FLAG_STICKY) {
    match_options |= PCRE2_ANCHORED;
  }
  if (regex->flag & NEO_REGEXP_FLAG_GLOBAL ||
      regex->flag & NEO_REGEXP_FLAG_STICKY) {
    neo_js_variable_t v_last_index = neo_js_context_get_field(
        ctx, self, neo_js_context_create_string(ctx, "lastIndex"), NULL);
    if (neo_js_variable_get_type(v_last_index)->kind != NEO_JS_TYPE_NUMBER) {
      v_last_index = neo_js_context_to_number(ctx, v_last_index);
      neo_js_context_set_field(ctx, self,
                               neo_js_context_create_string(ctx, "lastIndex"),
                               v_last_index, NULL);
    }
    last_index = neo_js_variable_to_number(v_last_index);

    if (isnan(last_index->number) || isinf(last_index->number) ||
        last_index->number > strlen(str) || last_index->number < 0) {
      last_index->number = 0;
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  pcre2_match_data *match_data =
      pcre2_match_data_create_from_pattern(regex->code, NULL);
  int rc = pcre2_match(regex->code, (PCRE2_SPTR)str, PCRE2_ZERO_TERMINATED,
                       last_index->number, match_options, match_data, NULL);

  if (regex->flag & NEO_REGEXP_FLAG_GLOBAL ||
      regex->flag & NEO_REGEXP_FLAG_STICKY) {
    if (rc > 0) {
      PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
      last_index->number = ovector[(rc - 1) * 2 + 1];
    } else {
      last_index->number = 0;
    }
  }
  pcre2_match_data_free(match_data);
  return neo_js_context_create_boolean(ctx, rc > 0);
}

void neo_js_context_init_std_regexp(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).regexp_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "toString"),
      neo_js_context_create_cfunction(ctx, "toString", neo_js_regexp_to_string),
      true, false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "exec"),
      neo_js_context_create_cfunction(ctx, "exec", neo_js_regexp_exec), true,
      false, true);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "test"),
      neo_js_context_create_cfunction(ctx, "test", neo_js_regexp_test), true,
      false, true);
}