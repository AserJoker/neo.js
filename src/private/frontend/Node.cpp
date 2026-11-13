// frontend/Node.cpp - AST 节点 id 分配
//
// id 在一次 parse 内稠密自增，0 保留为无效值。使用 thread_local 计数，
// 避免多线程解析时互相干扰；NodeIdScope 在 parseProgram 入口重置。

#include "frontend/Node.h"

namespace neo::frontend {

namespace {
thread_local NodeId nextNodeId = 1;
}

NodeId Node::allocateId() {
    return nextNodeId++;
}

void Node::resetIdCounter() {
    nextNodeId = 1;
}

}  // namespace neo::frontend
