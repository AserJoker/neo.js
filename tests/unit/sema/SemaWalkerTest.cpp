// tests/unit/sema/SemaWalkerTest.cpp - Walker / AnnotationStore 骨架测试

#include "frontend/Node.h"
#include "frontend/Parser.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"
#include "sema/Walker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using neo::frontend::Node;
using neo::frontend::NodeId;
using neo::frontend::NodeKind;
using neo::frontend::NodeIdScope;
using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceRange;
using neo::frontend::SourceType;
using neo::sema::AnnotationStore;
using neo::sema::IMeta;
using neo::sema::IVisitor;
using neo::sema::SemaContext;
using neo::sema::Walker;

ParseResult parse(std::string_view source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    return parser.parseProgram();
}

std::string kindLabel(NodeKind kind) {
    return std::to_string(static_cast<int>(kind));
}

struct IntMeta final : IMeta {
    explicit IntMeta(int value) : value(value) {}
    int value;
};

struct StringMeta final : IMeta {
    explicit StringMeta(std::string value) : value(std::move(value)) {}
    std::string value;
};

class OrderVisitor final : public IVisitor {
public:
    explicit OrderVisitor(std::vector<std::string>& events) : events_(events) {}

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        events_.push_back("enter:" + kindLabel(node.kind()));
    }

    void leave(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        events_.push_back("leave:" + kindLabel(node.kind()));
    }

private:
    std::vector<std::string>& events_;
};

class TagVisitor final : public IVisitor {
public:
    TagVisitor(std::string tag, std::vector<std::string>& events)
        : tag_(std::move(tag)), events_(events) {}

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& /*node*/) override {
        events_.push_back("enter:" + tag_);
    }

    void leave(Walker& /*walker*/, SemaContext& /*context*/,
               Node& /*node*/) override {
        events_.push_back("leave:" + tag_);
    }

private:
    std::string tag_;
    std::vector<std::string>& events_;
};

class IdVisitor final : public IVisitor {
public:
    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        ids.push_back(node.id());
    }

    std::vector<NodeId> ids;
};

struct CountMeta final : IMeta {
    int count = 0;
};

class CountVisitor final : public IVisitor {
public:
    void enter(Walker& walker, SemaContext& /*context*/, Node& node) override {
        walker.annotations().emplace<CountMeta>(node.id()).count += 1;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// NodeId
// ---------------------------------------------------------------------------

TEST(SemaWalkerTest, NodeIdsAreDenseAndUnique) {
    ParseResult result = parse("var a = 1; function f(x) { return x + a; }");
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.program, nullptr);

    AnnotationStore store;
    SemaContext context;
    Walker walker(store);
    auto visitor = std::make_unique<IdVisitor>();
    IdVisitor* raw = visitor.get();
    walker.addAll(std::move(visitor));

    walker.walk(*result.program, context);

    ASSERT_FALSE(raw->ids.empty());
    std::vector<NodeId> sorted = raw->ids;
    std::sort(sorted.begin(), sorted.end());

    EXPECT_EQ(sorted.front(), 1u);
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        EXPECT_EQ(sorted[i], static_cast<NodeId>(i + 1));
    }
}

TEST(SemaWalkerTest, NodeIdsRestartForEachParse) {
    ParseResult first = parse("var a = 1;");
    ParseResult second = parse("var a = 1;");
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());

    auto collectIds = [](Node& program) {
        AnnotationStore store;
        SemaContext context;
        Walker walker(store);
        auto visitor = std::make_unique<IdVisitor>();
        IdVisitor* raw = visitor.get();
        walker.addAll(std::move(visitor));
        walker.walk(program, context);
        return raw->ids;
    };

    std::vector<NodeId> firstIds = collectIds(*first.program);
    std::vector<NodeId> secondIds = collectIds(*second.program);
    std::sort(firstIds.begin(), firstIds.end());
    std::sort(secondIds.begin(), secondIds.end());

    ASSERT_FALSE(firstIds.empty());
    EXPECT_EQ(firstIds.front(), 1u);
    EXPECT_EQ(firstIds, secondIds);
}

TEST(SemaWalkerTest, AdoptIdKeepsAnnotations) {
    NodeIdScope scope;
    Program original{SourceRange{}, {}, SourceType::Script};
    Program replacement{SourceRange{}, {}, SourceType::Script};

    AnnotationStore store;
    store.emplace<IntMeta>(original.id(), 7);

    EXPECT_EQ(store.get<IntMeta>(replacement.id()), nullptr);
    replacement.adoptIdFrom(original);

    ASSERT_NE(store.get<IntMeta>(replacement.id()), nullptr);
    EXPECT_EQ(store.get<IntMeta>(replacement.id())->value, 7);
}

// ---------------------------------------------------------------------------
// AnnotationStore
// ---------------------------------------------------------------------------

TEST(SemaWalkerTest, AnnotationStoreStoresByNodeId) {
    NodeIdScope scope;
    Program program{SourceRange{}, {}, SourceType::Script};

    AnnotationStore store;
    EXPECT_TRUE(store.empty());
    EXPECT_EQ(store.get<IntMeta>(program.id()), nullptr);

    IntMeta& meta = store.emplace<IntMeta>(program.id(), 42);
    EXPECT_EQ(meta.value, 42);
    EXPECT_FALSE(store.empty());

    ASSERT_NE(store.get<IntMeta>(program.id()), nullptr);
    EXPECT_EQ(store.get<IntMeta>(program.id())->value, 42);
    EXPECT_EQ(store.get<StringMeta>(program.id()), nullptr);
}

TEST(SemaWalkerTest, AnnotationStoreKeepsFirstValuePerType) {
    NodeIdScope scope;
    Program program{SourceRange{}, {}, SourceType::Script};

    AnnotationStore store;
    store.emplace<IntMeta>(program.id(), 1);
    IntMeta& again = store.emplace<IntMeta>(program.id(), 2);

    EXPECT_EQ(again.value, 1);
}

TEST(SemaWalkerTest, AnnotationStoreSupportsMultipleTypes) {
    NodeIdScope scope;
    Program program{SourceRange{}, {}, SourceType::Script};

    AnnotationStore store;
    store.emplace<IntMeta>(program.id(), 1);
    store.emplace<StringMeta>(program.id(), "x");

    ASSERT_NE(store.get<IntMeta>(program.id()), nullptr);
    ASSERT_NE(store.get<StringMeta>(program.id()), nullptr);
    EXPECT_EQ(store.get<StringMeta>(program.id())->value, "x");
}

TEST(SemaWalkerTest, AnnotationStoreClear) {
    NodeIdScope scope;
    Program program{SourceRange{}, {}, SourceType::Script};

    AnnotationStore store;
    store.emplace<IntMeta>(program.id(), 1);
    store.clear();

    EXPECT_TRUE(store.empty());
    EXPECT_EQ(store.get<IntMeta>(program.id()), nullptr);
}

// ---------------------------------------------------------------------------
// Walker
// ---------------------------------------------------------------------------

TEST(SemaWalkerTest, EnterLeaveOrder) {
    ParseResult result = parse("1;");
    ASSERT_TRUE(result.ok());

    AnnotationStore store;
    SemaContext context;
    Walker walker(store);

    std::vector<std::string> events;
    walker.add(std::make_unique<OrderVisitor>(events),
               {NodeKind::Program, NodeKind::ExpressionStatement});

    walker.walk(*result.program, context);

    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0], "enter:" + kindLabel(NodeKind::Program));
    EXPECT_EQ(events[1], "enter:" + kindLabel(NodeKind::ExpressionStatement));
    EXPECT_EQ(events[2], "leave:" + kindLabel(NodeKind::ExpressionStatement));
    EXPECT_EQ(events[3], "leave:" + kindLabel(NodeKind::Program));
}

TEST(SemaWalkerTest, VisitorsRunInRegistrationOrder) {
    ParseResult result = parse("1;");
    ASSERT_TRUE(result.ok());

    AnnotationStore store;
    SemaContext context;
    Walker walker(store);

    std::vector<std::string> events;
    walker.add(std::make_unique<TagVisitor>("A", events), NodeKind::Program);
    walker.add(std::make_unique<TagVisitor>("B", events), NodeKind::Program);

    walker.walk(*result.program, context);

    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0], "enter:A");
    EXPECT_EQ(events[1], "enter:B");
    EXPECT_EQ(events[2], "leave:A");
    EXPECT_EQ(events[3], "leave:B");
}

TEST(SemaWalkerTest, VisitorCountReflectsRegistration) {
    AnnotationStore store;
    Walker walker(store);

    std::vector<std::string> events;
    EXPECT_EQ(walker.visitorCount(NodeKind::Program), 0u);
    walker.add(std::make_unique<TagVisitor>("A", events),
               {NodeKind::Program, NodeKind::Identifier});
    EXPECT_EQ(walker.visitorCount(NodeKind::Program), 1u);
    EXPECT_EQ(walker.visitorCount(NodeKind::Identifier), 1u);
    EXPECT_EQ(walker.visitorCount(NodeKind::BinaryExpression), 0u);
}

TEST(SemaWalkerTest, VisitorCanAnnotateNodes) {
    ParseResult result = parse("var a = 1; var b = 2;");
    ASSERT_TRUE(result.ok());

    AnnotationStore store;
    SemaContext context;
    Walker walker(store);
    walker.addAll(std::make_unique<CountVisitor>());

    walker.walk(*result.program, context);

    const CountMeta* meta = store.get<CountMeta>(result.program->id());
    ASSERT_NE(meta, nullptr);
    EXPECT_GT(meta->count, 0);
}

TEST(SemaWalkerTest, WalkerHandlesAllNodeKinds) {
    // 覆盖所有节点形状；任何 walkChildren 漏掉的 NodeKind 会触发断言。
    const std::string_view source = R"JS(
        "use strict";
        ;
        var a = 1, b = [1, , 2], c = { x: 1, get y() { return 2; }, ...d };
        var e = function () { return this; };
        var ce = class {};
        var u = !a + -b + ~a + +b + void 0 + typeof e;
        var upd = ++a + b--;
        var log = (a && b || c) ?? d;
        var cond = a ? b : c;
        var seq = (a, b, c);
        var arrow = (x = 1, ...y) => x + y;
        var re = /ab+c/gi;
        var bi = 1n;
        var bo = true;
        var nu = null;
        var nw = new C(1);
        var t = `a${b}c`;
        var tagged = String.raw`x${y}`;
        var chain = obj?.a?.[b]?.(c);
        var { x, y: z = 1, ...r } = c;
        var [m, , n] = b;
        if (a) { } else { }
        while (a) { break; }
        do { continue; } while (b);
        for (var k in c) { }
        for (const v of b) { }
        for (;;) { break; }
        label: for (let i = 0; i < 10; i++) { if (i) continue label; }
        switch (a) { case 1: break; default: }
        try { with (c) { debugger; } } catch (err) { } finally { }
        function f(p = 1, ...rest) { return p; }
        async function g() { for await (const x of xs) { await x; } }
        function* h() { yield 1; yield* xs; }
        class C extends B {
            static s = 1;
            #p = 2;
            static { }
            constructor() { super(); }
            get x() { return this.#p; }
        }
        new.target;
        import.meta;
        import("m", { with: { type: "json" } });
        export * from "m";
        export { a as b } from "m";
        export default 1;
    )JS";

    ParseResult result = parse(source);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.program, nullptr);

    AnnotationStore store;
    SemaContext context;
    Walker walker(store);
    walker.addAll(std::make_unique<CountVisitor>());

    walker.walk(*result.program, context);

    const CountMeta* meta = store.get<CountMeta>(result.program->id());
    ASSERT_NE(meta, nullptr);
    EXPECT_GT(meta->count, 0);
}
