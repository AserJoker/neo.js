#ifndef _H_NEO_ENGINE_STD_ARRAY_
#define _H_NEO_ENGINE_STD_ARRAY_
#include "engine/type.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_js_variable_t neo_js_array_from(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_from_async(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv,
                                          neo_js_variable_t value,
                                          size_t stage);

neo_js_variable_t neo_js_array_is_array(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_of(neo_js_context_t ctx, neo_js_variable_t self,
                                  uint32_t argc, neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_species(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_constructor(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_at(neo_js_context_t ctx, neo_js_variable_t self,
                                  uint32_t argc, neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_concat(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_copy_within(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_entries(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_every(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_fill(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_filter(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_find(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_find_index(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_find_last(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_find_last_index(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_flat(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_flat_map(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_for_each(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_includes(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_index_of(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_join(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_keys(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_last_index_of(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_map(neo_js_context_t ctx, neo_js_variable_t self,
                                   uint32_t argc, neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_pop(neo_js_context_t ctx, neo_js_variable_t self,
                                   uint32_t argc, neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_push(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_reduce(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_reduce_right(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            uint32_t argc,
                                            neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_reverse(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_shift(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_slice(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_some(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_sort(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_splice(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_to_local_string(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_to_reversed(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           uint32_t argc,
                                           neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_to_sorted(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_to_spliced(neo_js_context_t ctx,
                                          neo_js_variable_t self, uint32_t argc,
                                          neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_to_string(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_unshift(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_values(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv);

neo_js_variable_t neo_js_array_with(neo_js_context_t ctx,
                                    neo_js_variable_t self, uint32_t argc,
                                    neo_js_variable_t *argv);

#ifdef __cplusplus
}
#endif
#endif