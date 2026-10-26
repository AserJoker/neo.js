#include "neojs/runtime/module.h"

#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/heap.h"

namespace neojs {

/* 查找导出绑定的本地名（本地导出，不含 re-export）。未找到返回 nullptr。 */
const std::string *JSModuleRecord::findLocalExport(
    const std::string &export_name) const {
  for (const ExportBinding &eb : exports_) {
    if (eb.export_name == export_name && eb.from == nullptr) {
      return &eb.local_name;
    }
  }
  return nullptr;
}

/* 查找导出绑定：本地导出返回 local_name（from=nullptr）；re-export 返回
 * from 源模块（由调用方继续在源模块查找，M5.2b 细化转发链）。 */
bool JSModuleRecord::findExport(const std::string &export_name,
                                std::string *local_name,
                                JSModuleRecord **from) const {
  JSModuleRecord *star_from = nullptr;
  for (const ExportBinding &eb : exports_) {
    if (eb.export_name == export_name) {
      if (eb.from == nullptr) {
        if (local_name != nullptr) *local_name = eb.local_name;
        if (from != nullptr) *from = nullptr;
      } else {
        if (local_name != nullptr) *local_name = eb.local_name;
        if (from != nullptr) *from = eb.from;
      }
      return true;
    }
    /* export * from 'm'：记录首个 * 源（请求名非空且非 default 时转发）。 */
    if (eb.export_name.empty() && eb.from != nullptr &&
        star_from == nullptr && !export_name.empty() &&
        export_name != "default") {
      star_from = eb.from;
    }
  }
  if (star_from != nullptr) {
    /* 转发：源模块继续解析（源模块也可能有 *）。递归由调用方转发链完成。 */
    if (local_name != nullptr) *local_name = export_name;
    if (from != nullptr) *from = star_from;
    return true;
  }
  return false;
}

void JSModuleRecord::gcMarkRefs(Heap *heap) {
  if (env_ != nullptr) heap->markRoot(env_);
  if (function_ != nullptr) heap->markRoot(function_);
}

}  // namespace neojs
