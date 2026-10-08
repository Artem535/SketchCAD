#include "sketchcad/document.h"

#include <gtest/gtest.h>

#include <stdexcept>
using namespace sketchcad;
namespace {
Position point_at(const Document& d, EntityId id) {
  return std::get<SketchPoint>(d.sketch().entity(id).value()).position;
}
// Returns the ID the next creation would receive without changing `d`.
EntityId next_id(const Document& d) {
  Sketch probe = d.sketch();
  return probe.create_point({0, 0}).value();
}
std::optional<EntityId> add_point(Document& d, Position p) {
  std::optional<EntityId> id;
  if (!d.execute("Point", [&](Sketch& s) {
        id = s.create_point(p);
        return id.has_value();
      }))
    return std::nullopt;
  return id;
}
}  // namespace
TEST(Document, FailedBatchLeavesDocumentHistoryAndAllocatorUnchanged) {
  Document d;
  ASSERT_TRUE(add_point(d, {1, 1}));
  const auto before = d.sketch().entities();
  const auto revision = d.revision();
  const EntityId expected_next = next_id(d);
  EXPECT_FALSE(d.execute("Batch", [](Sketch& s) {
    auto a = s.create_point({2, 2});
    auto b = s.create_point({3, 3});
    return a && b && s.create_line(*a, 999).has_value();
  }));
  EXPECT_EQ(d.sketch().entities(), before);
  EXPECT_EQ(d.revision(), revision);
  EXPECT_EQ(d.undo_label(), "Point");
  EXPECT_FALSE(d.can_redo());
  EXPECT_EQ(next_id(d), expected_next);
}
TEST(Document, RejectedCommitStepAddsNoHistory) {
  Document d;
  d.set_commit_step(
      [](Sketch& s) { return s.entities().size() <= 1; });
  ASSERT_TRUE(add_point(d, {0, 0}));
  EXPECT_FALSE(add_point(d, {1, 0}));
  EXPECT_EQ(d.sketch().entities().size(), 1u);
  ASSERT_TRUE(d.undo());
  EXPECT_FALSE(d.can_undo());
}
TEST(Document, CommitStepMayAdjustTheWorkingCopy) {
  Document d;
  d.set_commit_step([](Sketch& s) {
    for (const auto& [id, e] : s.entities())
      if (std::holds_alternative<SketchPoint>(e))
        return s.update_point(id, {10, 20});
    return true;
  });
  auto p = add_point(d, {0, 0});
  ASSERT_TRUE(p);
  EXPECT_EQ(point_at(d, *p), (Position{10, 20}));
}
TEST(Document, ThrowingEditPropagatesAndKeepsDocument) {
  Document d;
  ASSERT_TRUE(add_point(d, {1, 1}));
  const auto before = d.sketch().entities();
  const auto revision = d.revision();
  EXPECT_THROW(d.execute("Throw",
                         [](Sketch& s) -> bool {
                           s.create_point({5, 5});
                           throw std::runtime_error("edit failed");
                         }),
               std::runtime_error);
  EXPECT_EQ(d.sketch().entities(), before);
  EXPECT_EQ(d.undo_label(), "Point");
  EXPECT_EQ(d.revision(), revision);
}
TEST(Document, CreateDeleteEditRoundTripsRestoreIdsAndReferences) {
  Document d;
  std::optional<Polyline> rect;
  ASSERT_TRUE(d.execute("Rectangle", [&](Sketch& s) {
    rect = s.create_rectangle({0, 0}, 50, 30);
    return rect.has_value();
  }));
  const auto created = d.sketch().entities();
  const EntityId line = rect->lines[1];
  ASSERT_TRUE(
      d.execute("Delete line", [&](Sketch& s) { return s.erase(line); }));
  const auto deleted = d.sketch().entities();
  ASSERT_TRUE(d.execute("Move corner", [&](Sketch& s) {
    return s.update_point(rect->points[2], {60, 40});
  }));
  const auto moved = d.sketch().entities();

  EXPECT_EQ(d.undo_label(), "Move corner");
  ASSERT_TRUE(d.undo());
  EXPECT_EQ(d.sketch().entities(), deleted);
  ASSERT_TRUE(d.undo());
  EXPECT_EQ(d.sketch().entities(), created);
  EXPECT_EQ(std::get<SketchLine>(d.sketch().entity(line).value()).start,
            rect->points[1]);
  ASSERT_TRUE(d.undo());
  EXPECT_TRUE(d.sketch().entities().empty());
  EXPECT_FALSE(d.undo());

  EXPECT_EQ(d.redo_label(), "Rectangle");
  ASSERT_TRUE(d.redo());
  EXPECT_EQ(d.sketch().entities(), created);
  ASSERT_TRUE(d.redo());
  ASSERT_TRUE(d.redo());
  EXPECT_EQ(d.sketch().entities(), moved);
  EXPECT_FALSE(d.redo());
}
TEST(Document, NewCommandAfterUndoDropsRedoAndNeverReusesIds) {
  Document d;
  auto first = add_point(d, {1, 1});
  ASSERT_TRUE(first);
  ASSERT_TRUE(d.undo());
  ASSERT_TRUE(d.can_redo());
  auto second = add_point(d, {2, 2});
  ASSERT_TRUE(second);
  EXPECT_NE(*second, *first);
  EXPECT_FALSE(d.can_redo());
  EXPECT_FALSE(d.redo());
  ASSERT_TRUE(d.undo());
  EXPECT_FALSE(d.can_undo());
  EXPECT_NE(next_id(d), *first);
  EXPECT_NE(next_id(d), *second);
}
TEST(Document, NoOpEditAddsNoHistory) {
  Document d;
  auto p = add_point(d, {1, 1});
  ASSERT_TRUE(p);
  const auto revision = d.revision();
  EXPECT_TRUE(d.execute("Same", [&](Sketch& s) {
    return s.update_point(*p, {1, 1});
  }));
  EXPECT_EQ(d.revision(), revision);
  EXPECT_EQ(d.undo_label(), "Point");
}
TEST(Document, GestureStepsBecomeOneUndoStep) {
  Document d;
  auto p = add_point(d, {0, 0});
  ASSERT_TRUE(p);
  ASSERT_TRUE(d.begin_gesture("Drag"));
  EXPECT_TRUE(d.gesture_active());
  for (int i = 1; i <= 5; ++i)
    ASSERT_TRUE(d.gesture_step([&](Sketch& s) {
      return s.update_point(*p, {double(i), double(i)});
    }));
  EXPECT_EQ(point_at(d, *p), (Position{5, 5}));
  ASSERT_TRUE(d.end_gesture());
  EXPECT_FALSE(d.gesture_active());
  EXPECT_EQ(d.undo_label(), "Drag");
  ASSERT_TRUE(d.undo());
  EXPECT_EQ(point_at(d, *p), (Position{0, 0}));
  EXPECT_EQ(d.undo_label(), "Point");
  ASSERT_TRUE(d.redo());
  EXPECT_EQ(point_at(d, *p), (Position{5, 5}));
}
TEST(Document, FailingGestureStepKeepsLastValidLiveState) {
  Document d;
  auto p = add_point(d, {0, 0});
  ASSERT_TRUE(p);
  d.set_commit_step([&](Sketch& s) {
    return std::get<SketchPoint>(s.entity(*p).value()).position.x <= 3;
  });
  ASSERT_TRUE(d.begin_gesture("Drag"));
  ASSERT_TRUE(
      d.gesture_step([&](Sketch& s) { return s.update_point(*p, {2, 0}); }));
  EXPECT_FALSE(
      d.gesture_step([&](Sketch& s) { return s.update_point(*p, {9, 0}); }));
  EXPECT_TRUE(d.gesture_active());
  EXPECT_EQ(point_at(d, *p), (Position{2, 0}));
  ASSERT_TRUE(d.end_gesture());
  EXPECT_EQ(point_at(d, *p), (Position{2, 0}));
}
TEST(Document, CancelledOrUnchangedGestureRecordsNothing) {
  Document d;
  auto p = add_point(d, {0, 0});
  ASSERT_TRUE(p);
  const auto revision = d.revision();
  ASSERT_TRUE(d.begin_gesture("Drag"));
  ASSERT_TRUE(
      d.gesture_step([&](Sketch& s) { return s.update_point(*p, {4, 4}); }));
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.cancel_gesture());
  EXPECT_EQ(point_at(d, *p), (Position{0, 0}));
  EXPECT_EQ(d.revision(), revision);
  ASSERT_TRUE(d.begin_gesture("Tap"));
  ASSERT_TRUE(d.end_gesture());
  EXPECT_EQ(d.revision(), revision);
  EXPECT_EQ(d.undo_label(), "Point");
  EXPECT_FALSE(d.end_gesture());
  EXPECT_FALSE(d.cancel_gesture());
  EXPECT_FALSE(d.gesture_step([](Sketch&) { return true; }));
}
TEST(Document, CommandsAreRejectedDuringGesture) {
  Document d;
  ASSERT_TRUE(add_point(d, {0, 0}));
  ASSERT_TRUE(d.begin_gesture("Drag"));
  EXPECT_FALSE(d.begin_gesture("Again"));
  EXPECT_FALSE(add_point(d, {1, 1}));
  EXPECT_FALSE(d.undo());
  EXPECT_FALSE(d.redo());
  EXPECT_FALSE(d.mark_saved());
  EXPECT_EQ(d.sketch().entities().size(), 1u);
  ASSERT_TRUE(d.cancel_gesture());
  EXPECT_TRUE(d.undo());
}
TEST(Document, DirtyStateFollowsSavedRevision) {
  Document d;
  EXPECT_FALSE(d.dirty());
  ASSERT_TRUE(add_point(d, {0, 0}));
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.mark_saved());
  EXPECT_FALSE(d.dirty());
  ASSERT_TRUE(add_point(d, {1, 1}));
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.undo());
  EXPECT_FALSE(d.dirty());
  ASSERT_TRUE(d.undo());
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.redo());
  EXPECT_FALSE(d.dirty());
}
TEST(Document, SavedRevisionOnDiscardedBranchStaysDirty) {
  Document d;
  ASSERT_TRUE(add_point(d, {0, 0}));
  ASSERT_TRUE(add_point(d, {1, 1}));
  ASSERT_TRUE(d.mark_saved());
  ASSERT_TRUE(d.undo());
  ASSERT_TRUE(add_point(d, {2, 2}));
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.undo());
  EXPECT_TRUE(d.dirty());
  ASSERT_TRUE(d.mark_saved());
  EXPECT_FALSE(d.dirty());
}
TEST(Document, RevisionsAreUniqueAcrossBranches) {
  Document d;
  EXPECT_EQ(d.revision(), 0u);
  ASSERT_TRUE(add_point(d, {0, 0}));
  const auto first = d.revision();
  EXPECT_NE(first, 0u);
  ASSERT_TRUE(d.undo());
  EXPECT_EQ(d.revision(), 0u);
  ASSERT_TRUE(add_point(d, {1, 1}));
  EXPECT_NE(d.revision(), first);
  EXPECT_NE(d.revision(), 0u);
}
