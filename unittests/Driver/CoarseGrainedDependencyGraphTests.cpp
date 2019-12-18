#include "swift/Basic/ReferenceDependencyKeys.h"
#include "swift/Driver/CoarseGrainedDependencyGraph.h"
#include "gtest/gtest.h"

using namespace swift;
using LoadResult = CoarseGrainedDependencyGraphImpl::LoadResult;
using namespace reference_dependency_keys;

static LoadResult loadFromString(CoarseGrainedDependencyGraph<uintptr_t> &dg,
                                 uintptr_t node, StringRef key,
                                 StringRef data) {
  return dg.loadFromString(node, key.str() + ": [" + data.str() + "]");
}

static LoadResult loadFromString(CoarseGrainedDependencyGraph<uintptr_t> &dg,
                                 uintptr_t node, StringRef key1,
                                 StringRef data1, StringRef key2,
                                 StringRef data2) {
  return dg.loadFromString(node, key1.str() + ": [" + data1.str() + "]\n" +
                                     key2.str() + ": [" + data2.str() + "]");
}

static LoadResult loadFromString(CoarseGrainedDependencyGraph<uintptr_t> &dg,
                                 uintptr_t node, StringRef key1,
                                 StringRef data1, StringRef key2,
                                 StringRef data2, StringRef key3,
                                 StringRef data3, StringRef key4,
                                 StringRef data4) {
  return dg.loadFromString(node, key1.str() + ": [" + data1.str() + "]\n" +
                                     key2.str() + ": [" + data2.str() + "]\n" +
                                     key3.str() + ": [" + data3.str() + "]\n" +
                                     key4.str() + ": [" + data4.str() + "]\n");
}

TEST(CoarseGrainedDependencyGraph, BasicLoad) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;
  uintptr_t i = 0;

  EXPECT_EQ(loadFromString(graph, i++, dependsTopLevel, "a, b"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, dependsNominal, "c, d"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, providesTopLevel, "e, f"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, providesNominal, "g, h"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, providesDynamicLookup, "i, j"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, dependsDynamicLookup, "k, l"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, providesMember, "[m, mm], [n, nn]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, dependsMember, "[o, oo], [p, pp]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, i++, dependsExternal, "/foo, /bar"),
            LoadResult::UpToDate);

  EXPECT_EQ(loadFromString(graph, i++,
                           providesNominal, "a, b",
                           providesTopLevel, "b, c",
                           dependsNominal, "c, d",
                           dependsTopLevel, "d, a"),
            LoadResult::UpToDate);
}

TEST(CoarseGrainedDependencyGraph, IndependentNodes) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsTopLevel, "a",
                           providesTopLevel, "a0"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsTopLevel, "b",
                           providesTopLevel, "b0"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2,
                           dependsTopLevel, "c",
                           providesTopLevel, "c0"),
      LoadResult::UpToDate);

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  // Mark 0 again -- should be no change.
  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(2).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(1).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, IndependentDepKinds) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsNominal, "a",
                           providesNominal, "b"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsTopLevel, "b",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, IndependentDepKinds2) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsNominal, "a",
                           providesNominal, "b"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsTopLevel, "b",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);

  EXPECT_EQ(0u, graph.markTransitive(1).size());
  EXPECT_FALSE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, IndependentMembers) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesMember, "[a,aa]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsMember, "[a,bb]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsMember, "[a,\"\"]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 3, dependsMember, "[b,aa]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 4, dependsMember, "[b,bb]"),
            LoadResult::UpToDate);

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));
  EXPECT_FALSE(graph.isMarked(3));
  EXPECT_FALSE(graph.isMarked(4));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesTopLevel, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsTopLevel, "x, b, z"),
            LoadResult::UpToDate);
  {
    auto marked = graph.markTransitive(0);
    EXPECT_EQ(1u, marked.size());
    EXPECT_EQ(1u, marked.front());
  }
    EXPECT_TRUE(graph.isMarked(0));
    EXPECT_TRUE(graph.isMarked(1));

    EXPECT_EQ(0u, graph.markTransitive(0).size());
    EXPECT_TRUE(graph.isMarked(0));
    EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependentReverse) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, dependsTopLevel, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, providesTopLevel, "x, b, z"),
            LoadResult::UpToDate);

  {
  auto marked = graph.markTransitive(1);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(0u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent2) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "x, b, z"),
            LoadResult::UpToDate);


  {
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent3) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           providesNominal, "a",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "a"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent4) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsNominal, "a",
                           dependsTopLevel, "a"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive( 0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent5) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           providesNominal, "a",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsNominal, "a",
                           dependsTopLevel, "a"),
            LoadResult::UpToDate);


{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  auto marked = graph.markTransitive(0);
  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleDependent6) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesDynamicLookup, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsDynamicLookup, "x, b, z"),
            LoadResult::UpToDate);

  {
    auto marked = graph.markTransitive(0);
    EXPECT_EQ(1u, marked.size());
    EXPECT_EQ(1u, marked.front());
    }
    EXPECT_TRUE(graph.isMarked(0));
    EXPECT_TRUE(graph.isMarked(1));

    EXPECT_EQ(0u, graph.markTransitive(0).size());
    EXPECT_TRUE(graph.isMarked(0));
    EXPECT_TRUE(graph.isMarked(1));
}


TEST(CoarseGrainedDependencyGraph, SimpleDependentMember) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           providesMember, "[a,aa], [b,bb], [c,cc]"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsMember, "[x, xx], [b,bb], [z,zz]"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}


template <typename Range, typename T>
static bool contains(const Range &range, const T &value) {
  return std::find(std::begin(range),std::end(range),value) != std::end(range);
}

TEST(CoarseGrainedDependencyGraph, MultipleDependentsSame) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "x, b, z"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "q, b, s"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(2u, marked.size());
  EXPECT_TRUE(contains(marked, 1));
  EXPECT_TRUE(contains(marked, 2));
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, MultipleDependentsDifferent) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "x, b, z"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "q, r, c"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(2u, marked.size());
  EXPECT_TRUE(contains(marked, 1));
  EXPECT_TRUE(contains(marked, 2));
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, ChainedDependents) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsNominal, "x, b",
                           providesNominal, "z"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "z"),
            LoadResult::UpToDate);


{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(2u, marked.size());
  EXPECT_TRUE(contains(marked, 1));
  EXPECT_TRUE(contains(marked, 2));
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, MarkTwoNodes) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsNominal, "a",
                           providesNominal, "z"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "z"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 10,
                           providesNominal, "y, z",
                           dependsNominal, "q"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 11, dependsNominal, "y"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 12,
                           dependsNominal, "q",
                           providesNominal, "q"),
      LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(2u, marked.size());
  EXPECT_TRUE(contains(marked, 1));
  EXPECT_TRUE(contains(marked, 2));
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
  EXPECT_FALSE(graph.isMarked(10));
  EXPECT_FALSE(graph.isMarked(11));
  EXPECT_FALSE(graph.isMarked(12));

{
  auto marked = graph.markTransitive(10);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(11u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
  EXPECT_TRUE(graph.isMarked(10));
  EXPECT_TRUE(graph.isMarked(11));
  EXPECT_FALSE(graph.isMarked(12));
}

TEST(CoarseGrainedDependencyGraph, MarkOneNodeTwice) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "b"),
            LoadResult::UpToDate);


{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  // Reload 0.
  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "b"),
            LoadResult::UpToDate);

 {
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(2u, marked.front());
}
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, MarkOneNodeTwice2) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "b"),
            LoadResult::UpToDate);

{
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  // Reload 0.
  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a, b"),
            LoadResult::UpToDate);

 {
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(2u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, NotTransitiveOnceMarked) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsNominal, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsNominal, "b"),
            LoadResult::UpToDate);


  EXPECT_EQ(0u, graph.markTransitive(1).size());
  EXPECT_FALSE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  // Reload 1.
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsNominal, "a",
                           providesNominal, "b"),
            LoadResult::UpToDate);

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_FALSE(graph.isMarked(2));

  // Re-mark 1.
  {
  auto marked = graph.markTransitive(1);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(2u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, DependencyLoops) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           providesTopLevel, "a, b, c",
                           dependsTopLevel, "a"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           providesTopLevel, "x",
                           dependsTopLevel,
                           "x, b, z"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 2, dependsTopLevel, "x"),
            LoadResult::UpToDate);

  {
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(2u, marked.size());
  EXPECT_TRUE(contains(marked, 1));
  EXPECT_TRUE(contains(marked, 2));
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
  EXPECT_TRUE(graph.isMarked(2));
}

TEST(CoarseGrainedDependencyGraph, MarkIntransitive) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesTopLevel, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsTopLevel, "x, b, z"),
            LoadResult::UpToDate);

  EXPECT_TRUE(graph.markIntransitive(0));
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));

  {
  auto marked = graph.markTransitive(0);
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, MarkIntransitiveTwice) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesTopLevel, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsTopLevel, "x, b, z"),
            LoadResult::UpToDate);

  EXPECT_TRUE(graph.markIntransitive(0));
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));

  EXPECT_FALSE(graph.markIntransitive(0));
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, MarkIntransitiveThenIndirect) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, providesTopLevel, "a, b, c"),
            LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1, dependsTopLevel, "x, b, z"),
            LoadResult::UpToDate);

  EXPECT_TRUE(graph.markIntransitive(1));
  EXPECT_FALSE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markTransitive(0).size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, SimpleExternal) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, dependsExternal, "/foo, /bar"),
            LoadResult::UpToDate);

  EXPECT_TRUE(contains(graph.getExternalDependencies(), "/foo"));
  EXPECT_TRUE(contains(graph.getExternalDependencies(), "/bar"));

  EXPECT_EQ(1u, graph.markExternal("/foo").size());
  EXPECT_TRUE(graph.isMarked(0));

  EXPECT_EQ(0u, graph.markExternal("/foo").size());
  EXPECT_TRUE(graph.isMarked(0));
}

TEST(CoarseGrainedDependencyGraph, SimpleExternal2) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0, dependsExternal, "/foo, /bar"),
            LoadResult::UpToDate);

  EXPECT_EQ(1u, graph.markExternal("/bar").size());
  EXPECT_TRUE(graph.isMarked(0));

  EXPECT_EQ(0u, graph.markExternal("/bar").size());
  EXPECT_TRUE(graph.isMarked(0));
}

TEST(CoarseGrainedDependencyGraph, ChainedExternal) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsExternal, "/foo",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsExternal, "/bar",
                           dependsTopLevel, "a"),
      LoadResult::UpToDate);

  EXPECT_TRUE(contains(graph.getExternalDependencies(), "/foo"));
  EXPECT_TRUE(contains(graph.getExternalDependencies(), "/bar"));

  EXPECT_EQ(2u, graph.markExternal("/foo").size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markExternal("/foo").size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, ChainedExternalReverse) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsExternal, "/foo",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsExternal, "/bar",
                           dependsTopLevel, "a"),
      LoadResult::UpToDate);

  {
  auto marked = graph.markExternal("/bar");
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(1u, marked.front());
  }
  EXPECT_FALSE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

  EXPECT_EQ(0u, graph.markExternal("/bar").size());
  EXPECT_FALSE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));

{
  auto marked = graph.markExternal("/foo");
  EXPECT_EQ(1u, marked.size());
  EXPECT_EQ(0u, marked.front());
}
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_TRUE(graph.isMarked(1));
}

TEST(CoarseGrainedDependencyGraph, ChainedExternalPreMarked) {
  CoarseGrainedDependencyGraph<uintptr_t> graph;

  EXPECT_EQ(loadFromString(graph, 0,
                           dependsExternal, "/foo",
                           providesTopLevel, "a"),
      LoadResult::UpToDate);
  EXPECT_EQ(loadFromString(graph, 1,
                           dependsExternal, "/bar",
                           dependsTopLevel, "a"),
      LoadResult::UpToDate);

  graph.markIntransitive(0);

  EXPECT_EQ(0u, graph.markExternal("/foo").size());
  EXPECT_TRUE(graph.isMarked(0));
  EXPECT_FALSE(graph.isMarked(1));
}
