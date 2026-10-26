#include "neojs/runtime/module.h"

#include <unordered_set>

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

/* M5.2b：解析导出名到「最终模块 + 本地绑定名」。
 * 规则（§16.4.6 解析导出，含 export * 合并与环保护）：
 *   - 直接匹配 export_name：
 *       本地导出（from==nullptr）-> 绑定在本模块（resolved=this）。
 *       具名 re-export（from!=nullptr）-> 递归到源模块解析（绑定在源）。
 *   - 未直接命中：遍历所有 export * from 'm'（export_name 空的转发条目），
 *     在源模块递归解析请求名；第一个命中即返回（§16.4.6 步骤 6：多个
 *     源同时导出同名属链接期 ambiguous error，本阶段先到先得）。
 *   - 环保护：visit 集记录已访问的模块记录，star 环（a->b->a）在此截断，
 *     避免无限递归。
 * 输出：local_name = 最终绑定名，resolved = 绑定所在模块记录。
 * 返回 false = 导出不存在（或 ambiguous，本阶段不区分）。 */
bool JSModuleRecord::findExport(const std::string &export_name,
                                std::string *local_name,
                                JSModuleRecord **resolved) const {
  std::unordered_set<const JSModuleRecord *> visit;
  return findExportRec(export_name, local_name, resolved, visit);
}

bool JSModuleRecord::findExportRec(
    const std::string &export_name, std::string *local_name,
    JSModuleRecord **resolved,
    std::unordered_set<const JSModuleRecord *> &visit) const {
  if (!visit.insert(this).second) return false; /* 环：已访问过本模块 */

  /* 1. 直接命中（含 default：export default 的本地绑定）。 */
  for (const ExportBinding &eb : exports_) {
    if (eb.export_name == export_name) {
      if (eb.from == nullptr) {
        /* 本地导出：绑定在本模块。 */
        if (local_name != nullptr) *local_name = eb.local_name;
        if (resolved != nullptr) *resolved = const_cast<JSModuleRecord *>(this);
      } else {
        /* 具名 re-export：绑定在源模块，递归到源模块解析。 */
        if (!eb.from->findExportRec(export_name, local_name, resolved, visit)) {
          return false; /* 源模块无此导出：解析失败 */
        }
      }
      return true;
    }
  }

  /* 2. export * from 'm'（export_name 空）：转发全部具名导出。
   *    请求 default 时跳过（§16.4.6 步骤 4：export * 不含 default）。
   *    多个源：第一个命中即返回（ambiguous 简化）。 */
  if (!export_name.empty() && export_name != "default") {
    for (const ExportBinding &eb : exports_) {
      if (eb.export_name.empty() && eb.from != nullptr) {
        if (eb.from->findExportRec(export_name, local_name, resolved, visit)) {
          return true;
        }
      }
    }
  }

  return false;
}

void JSModuleRecord::gcMarkRefs(Heap *heap) {
  if (env_ != nullptr) heap->markRoot(env_);
  if (function_ != nullptr) heap->markRoot(function_);
}

}  // namespace neojs
