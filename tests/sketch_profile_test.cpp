#include "sketchcad/sketch_profile.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "sketchcad/sketch_edit.h"
#include "sketchcad/solver.h"

namespace sketchcad {
namespace {

constexpr double kPi = std::numbers::pi;

TEST(MergeVertices, ExplicitlyCollapsesSideBetweenAdjacentChamfers) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 50, 20);
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kVertical, rect.lines[3]));
  ASSERT_TRUE(chamfer(s, rect.points[3], 10.51));
  ASSERT_TRUE(chamfer(s, rect.points[0], 9.48992));
  const Sketch before = s;
  EXPECT_EQ(merge_nearby_vertices(s, .5).status,
            MergeStatus::kCollapsedEntity);
  EXPECT_EQ(s, before);
  ASSERT_EQ(merge_nearby_vertices(s, .5, true).status, MergeStatus::kMerged);
  EXPECT_FALSE(s.entity(rect.lines[3]));
  for (int i = 0; i < 3; ++i) EXPECT_TRUE(s.entity(rect.lines[i]));
  EXPECT_EQ(s.constraints().size(), before.constraints().size() - 1);
  EXPECT_EQ(check_constraints(s).status, SolveStatus::kSolved);
  const auto profile = analyze_profiles(s);
  ASSERT_TRUE(profile.ready_for_extrude());
  ASSERT_EQ(profile.faces.size(), 1u);
  EXPECT_EQ(profile.faces[0].outer.edges.size(), 5u);
  std::size_t reference_count = 0;
  for (const auto& [id, c] : s.constraints()) reference_count += c.reference;
  EXPECT_EQ(reference_count, 2u);
}

TEST(MergeVertices, CollapseRejectsUnsupportedConstraintsAndEntireLoop) {
  for (bool dimensioned : {false, true}) {
    Sketch s;
    const auto rect = *s.create_rectangle({0, 0}, .3, .2);
    if (dimensioned) {
      ASSERT_TRUE(s.add_dimension(ConstraintKind::kLength, rect.lines[0], 0, .3));
    }
    const Sketch before = s;
    EXPECT_NE(merge_nearby_vertices(s, .5, true).status, MergeStatus::kMerged);
    EXPECT_EQ(s, before);
  }
}

TEST(MergeVertices, CollapseCannotEraseAnEntireHole) {
  Sketch s;
  ASSERT_TRUE(s.create_rectangle({0, 0}, 50, 20));
  ASSERT_TRUE(s.create_rectangle({10, 10}, .3, .2));
  const auto profile = analyze_profiles(s);
  ASSERT_TRUE(profile.ready_for_extrude());
  ASSERT_EQ(profile.faces.size(), 1u);
  ASSERT_EQ(profile.faces[0].holes.size(), 1u);
  const Sketch before = s;
  EXPECT_EQ(merge_nearby_vertices(s, .5, true).status, MergeStatus::kConflict);
  EXPECT_EQ(s, before);
}

TEST(MergeVertices, CollapsePreservesConflictingFixedTargetsAndDrivingEdgeLength) {
  for (bool fixed : {false, true}) {
    Sketch s;
    const auto rect = *s.create_rectangle({0, 0}, 50, 20);
    ASSERT_TRUE(chamfer(s, rect.points[3], 10.51));
    ASSERT_TRUE(chamfer(s, rect.points[0], 9.48992));
    const auto edge = std::get<SketchLine>(*s.entity(rect.lines[3]));
    if (fixed) {
      ASSERT_TRUE(s.add_constraint(ConstraintKind::kFix, edge.start));
      ASSERT_TRUE(s.add_constraint(ConstraintKind::kFix, edge.end));
    } else {
      ASSERT_TRUE(s.add_dimension(ConstraintKind::kLength, edge.id, 0, .00008));
    }
    const Sketch before = s;
    EXPECT_EQ(merge_nearby_vertices(s, .5, true).status, MergeStatus::kConflict);
    EXPECT_EQ(s, before);
  }
}

TEST(Profiles, CornerNumericInputRejectsSubToleranceRetainedLeg) {
  for (bool rounded : {false, true}) {
    Sketch s;
    const auto rect = *s.create_rectangle({12, 32}, 42, 22);
    constexpr double radius = 12.370427255361118;
    ASSERT_TRUE(fillet(s, rect.points[3], radius));
    ASSERT_TRUE(fillet(s, rect.points[1], radius));
    const Sketch before = s;
    const double size = 22 - radius - 4e-7;
    if (rounded) {
      EXPECT_EQ(fillet_status(s, rect.points[0], size), FilletStatus::kTooLong);
      EXPECT_FALSE(fillet_preview(s, rect.points[0], size));
      EXPECT_FALSE(fillet(s, rect.points[0], size));
    } else {
      EXPECT_EQ(chamfer_status(s, rect.points[0], size), ChamferStatus::kTooLong);
      EXPECT_FALSE(chamfer_preview(s, rect.points[0], size));
      EXPECT_FALSE(chamfer(s, rect.points[0], size));
    }
    EXPECT_EQ(s, before);
  }
}

TEST(MergeVertices,
     WeldsUnclosedPolylineWithinToleranceAndPreservesConstraints) {
  Sketch s;
  const auto p0 = *s.create_point({0, 0});
  const auto p1 = *s.create_point({60, 0});
  const auto p2 = *s.create_point({60, 40});
  const auto p3 = *s.create_point({0, 40});
  // Slight 0.25 mm gap at the start/end corner and top-right corner.
  const auto p2b = *s.create_point({60.15, 39.9});
  const auto p0b = *s.create_point({0.2, -0.1});

  const auto bottom = *s.create_line(p0, p1);
  const auto right = *s.create_line(p1, p2);
  const auto top = *s.create_line(p2b, p3);
  const auto left = *s.create_line(p3, p0b);

  ASSERT_TRUE(s.add_constraint(ConstraintKind::kFix, p0));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kHorizontal, bottom));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kVertical, right));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kHorizontal, top));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kVertical, left));
  ASSERT_TRUE(s.add_dimension(ConstraintKind::kLength, bottom, 0, 60));
  ASSERT_TRUE(s.add_dimension(ConstraintKind::kLength, right, 0, 40));

  const auto before_open = analyze_profiles(s);
  EXPECT_EQ(before_open.open_endpoints.size(), 4u);
  EXPECT_FALSE(before_open.ready_for_extrude());

  const auto merged = merge_nearby_vertices(s, 0.5);
  EXPECT_EQ(merged.status, MergeStatus::kMerged);
  EXPECT_EQ(merged.merged_points, 2u);
  EXPECT_EQ(check_constraints(s).status, SolveStatus::kSolved);
  EXPECT_TRUE(Sketch::from_snapshot(s.snapshot()).has_value());

  const auto after = analyze_profiles(s);
  ASSERT_TRUE(after.ready_for_extrude());
  ASSERT_EQ(after.faces.size(), 1u);
  EXPECT_NEAR(after.faces[0].area, 2400.0, 1e-5);
}

TEST(MergeVertices, RejectsInvalidToleranceWithoutMutatingSketch) {
  Sketch s;
  const auto a = *s.create_point({0, 0});
  const auto b = *s.create_point({0.1, 0});
  const auto c = *s.create_point({10, 0});
  ASSERT_TRUE(s.create_line(a, c));
  ASSERT_TRUE(s.create_line(b, c));
  const Sketch original = s;

  for (double bad : {0.0, -0.5, std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity(), 2e6}) {
    const auto res = merge_nearby_vertices(s, bad);
    EXPECT_EQ(res.status, MergeStatus::kInvalidTolerance);
    EXPECT_EQ(res.merged_points, 0u);
    EXPECT_EQ(s, original);
  }
}

TEST(MergeVertices, RejectsCollapsedLineSegmentWhenToleranceExceedsLength) {
  Sketch s;
  const auto a = *s.create_point({0, 0});
  const auto b = *s.create_point({0.3, 0});
  const auto c = *s.create_point({10, 10});
  ASSERT_TRUE(s.create_line(a, b));
  ASSERT_TRUE(s.create_line(b, c));
  const Sketch original = s;

  const auto res = merge_nearby_vertices(s, 0.5);
  EXPECT_EQ(res.status, MergeStatus::kCollapsedEntity);
  EXPECT_EQ(s, original);
}

TEST(MergeVertices, RejectsConflictingFixedVertices) {
  Sketch s;
  const auto a = *s.create_point({0, 0});
  const auto b = *s.create_point({0.2, 0});
  const auto c = *s.create_point({20, 0});
  const auto d = *s.create_point({20, 20});
  ASSERT_TRUE(s.create_line(a, c));
  ASSERT_TRUE(s.create_line(b, d));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kFix, a));
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kFix, b));
  const Sketch original = s;

  const auto res = merge_nearby_vertices(s, 0.5);
  EXPECT_EQ(res.status, MergeStatus::kConflict);
  EXPECT_EQ(s, original);
}

TEST(MergeVertices, IgnoresConstructionVirtualSharpsAndArcCenters) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 40, 30);
  ASSERT_TRUE(fillet(s, rect.points[0], 0.4));
  const Sketch closed_filleted = s;

  // Tolerance 0.5 exceeds the 0.4 mm fillet radius and virtual-sharp setback,
  // yet neither the construction virtual sharp nor the arc center may merge.
  const auto noop = merge_nearby_vertices(s, 0.5);
  EXPECT_EQ(noop.status, MergeStatus::kNothingToMerge);
  EXPECT_EQ(s, closed_filleted);

  // Add a second triangle contour with a 0.2 mm gap; merge welds only that gap.
  const auto t0 = *s.create_point({60, 0});
  const auto t1 = *s.create_point({80, 0});
  const auto t2 = *s.create_point({70, 20});
  const auto t0b = *s.create_point({60.15, 0.1});
  ASSERT_TRUE(s.create_line(t0, t1));
  ASSERT_TRUE(s.create_line(t1, t2));
  ASSERT_TRUE(s.create_line(t2, t0b));

  const auto merged = merge_nearby_vertices(s, 0.5);
  EXPECT_EQ(merged.status, MergeStatus::kMerged);
  EXPECT_EQ(merged.merged_points, 1u);
  EXPECT_TRUE(s.entity(rect.points[0]).has_value());
  EXPECT_TRUE(std::get<SketchPoint>(*s.entity(rect.points[0])).construction);
  EXPECT_EQ(analyze_profiles(s).faces.size(), 2u);
}

TEST(Profiles, ExtractsClockwiseAndCounterClockwiseRectanglesAsCCWOuter) {
  Sketch s;
  // Clockwise 50x30 rectangle.
  const std::array<Position, 4> cw{{{0, 0}, {0, 30}, {50, 30}, {50, 0}}};
  ASSERT_TRUE(s.create_polyline(cw, true));
  // Construction helper diagonal must be ignored.
  const auto h0 = *s.create_point({-10, -10}, true);
  const auto h1 = *s.create_point({60, 40}, true);
  ASSERT_TRUE(s.create_line(h0, h1, true));

  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  EXPECT_GT(prof.faces[0].outer.signed_area, 0.0);
  EXPECT_NEAR(prof.faces[0].area, 1500.0, 1e-6);
  EXPECT_EQ(prof.faces[0].outer.edges.size(), 4u);
  EXPECT_TRUE(prof.faces[0].holes.empty());
}

TEST(Profiles, ExtractsFilletedAndChamferedPlateWithExactAnalyticalArea) {
  Sketch s;
  const auto rect = *s.create_rectangle({0, 0}, 80, 50);
  // Fillet two corners with R=6, chamfer two corners with d=5.
  ASSERT_TRUE(fillet(s, rect.points[0], 6.0));
  ASSERT_TRUE(fillet(s, rect.points[1], 6.0));
  ASSERT_TRUE(chamfer(s, rect.points[2], 5.0));
  ASSERT_TRUE(chamfer(s, rect.points[3], 5.0));
  ASSERT_EQ(solve(s).status, SolveStatus::kSolved);

  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  // Analytical area: 80*50 - 2 * (1 - pi/4) * 6^2 - 2 * (0.5 * 5 * 5)
  const double expected =
      80.0 * 50.0 - 2.0 * (1.0 - kPi / 4.0) * 36.0 - 2.0 * 12.5;
  EXPECT_NEAR(prof.faces[0].area, expected, 1e-5);
  EXPECT_EQ(prof.faces[0].outer.edges.size(), 8u);
}

TEST(Profiles, ExtractsOuterPlateWithCircularAndOblongSlotHoles) {
  Sketch s;
  ASSERT_TRUE(s.create_rectangle({0, 0}, 100, 60));

  // Circular hole R=8 at (25, 30).
  const auto c_hole = *s.create_point({25, 30});
  ASSERT_TRUE(s.create_circle(c_hole, 8.0));

  // Oblong slot hole: two straight segments y=25 and y=35 from x=55 to x=75,
  // capped by two semicircular arcs R=5 at (55, 30) and (75, 30).
  const auto s_bl = *s.create_point({55, 25});
  const auto s_br = *s.create_point({75, 25});
  const auto s_tr = *s.create_point({75, 35});
  const auto s_tl = *s.create_point({55, 35});
  ASSERT_TRUE(s.create_line(s_bl, s_br));
  ASSERT_TRUE(s.create_line(s_tr, s_tl));
  const auto cap_r = *s.create_point({75, 30});
  const auto cap_l = *s.create_point({55, 30});
  ASSERT_TRUE(s.create_arc(cap_r, 5.0, -kPi / 2.0, kPi));
  ASSERT_TRUE(s.create_arc(cap_l, 5.0, kPi / 2.0, kPi));

  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  ASSERT_EQ(prof.faces[0].holes.size(), 2u);
  for (const auto& hole : prof.faces[0].holes) {
    EXPECT_LT(hole.signed_area, 0.0);
  }
  const double circle_area = kPi * 64.0;
  const double slot_area = 20.0 * 10.0 + kPi * 25.0;
  EXPECT_NEAR(prof.faces[0].area, 6000.0 - circle_area - slot_area, 1e-5);
}

TEST(Profiles, ExtractsIslandInsideHoleAsSeparateOuterFace) {
  Sketch s;
  const auto center = *s.create_point({0, 0});
  ASSERT_TRUE(s.create_circle(center, 30.0));  // depth 0: outer
  ASSERT_TRUE(s.create_circle(center, 20.0));  // depth 1: hole
  ASSERT_TRUE(s.create_circle(center, 8.0));   // depth 2: inner island outer

  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 2u);

  double total_area = 0.0;
  for (const auto& f : prof.faces) total_area += f.area;
  const double expected = kPi * (900.0 - 400.0 + 64.0);
  EXPECT_NEAR(total_area, expected, 1e-5);
}

TEST(Profiles, ReportsInvalidIntersectionsOnCrossingAndBranchingCurves) {
  // Self-intersecting bowtie polygon.
  Sketch bowtie;
  const std::array<Position, 4> pts{{{0, 0}, {40, 40}, {40, 0}, {0, 40}}};
  ASSERT_TRUE(bowtie.create_polyline(pts, true));
  const auto res1 = analyze_profiles(bowtie);
  EXPECT_FALSE(res1.ready_for_extrude());
  EXPECT_TRUE(res1.faces.empty());
  EXPECT_FALSE(res1.invalid_intersections.empty());

  // Two adjacent squares sharing a middle vertical edge (degree-3 vertices).
  Sketch branched;
  const auto p0 = *branched.create_point({0, 0});
  const auto p1 = *branched.create_point({20, 0});
  const auto p2 = *branched.create_point({40, 0});
  const auto p3 = *branched.create_point({40, 20});
  const auto p4 = *branched.create_point({20, 20});
  const auto p5 = *branched.create_point({0, 20});
  ASSERT_TRUE(branched.create_line(p0, p1));
  ASSERT_TRUE(branched.create_line(p1, p2));
  ASSERT_TRUE(branched.create_line(p2, p3));
  ASSERT_TRUE(branched.create_line(p3, p4));
  ASSERT_TRUE(branched.create_line(p4, p5));
  ASSERT_TRUE(branched.create_line(p5, p0));
  ASSERT_TRUE(branched.create_line(p1, p4));
  const auto res2 = analyze_profiles(branched);
  EXPECT_FALSE(res2.ready_for_extrude());
  EXPECT_FALSE(res2.invalid_intersections.empty());
}

TEST(Profiles, KeepsAreaAtLargeTranslation) {
  Sketch s;
  ASSERT_TRUE(s.create_rectangle({1e9, 1e9}, 20, 10));
  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  EXPECT_TRUE(std::isfinite(prof.faces[0].area));
  EXPECT_NEAR(prof.faces[0].area, 200.0, 1e-6);
}

TEST(Profiles, ReportsDegenerateCurveBesideValidFace) {
  Sketch s;
  ASSERT_TRUE(s.create_rectangle({0, 0}, 20, 10));
  const auto a = *s.create_point({50, 50});
  const auto b = *s.create_point({50, 50});
  ASSERT_TRUE(s.create_line(a, b));
  const auto prof = analyze_profiles(s);
  EXPECT_FALSE(prof.ready_for_extrude());
  EXPECT_FALSE(prof.invalid_intersections.empty());
  EXPECT_EQ(prof.faces.size(), 1u);
}

TEST(Profiles, DetectsNarrowAngularOverlapBetweenArcSamples) {
  for (double overlap : {0.001, 1e-10}) {
    for (double sign : {-1.0, 1.0}) {
      Sketch s;
      const auto center = *s.create_point({0, 0});
      ASSERT_TRUE(s.create_arc(center, 10, sign * 6.0, sign * 1.0));
      ASSERT_TRUE(s.create_arc(center, 10, sign * (7.0 - overlap), sign * 1.0));
      const auto prof = analyze_profiles(s);
      EXPECT_FALSE(prof.ready_for_extrude());
      EXPECT_FALSE(prof.invalid_intersections.empty());
      EXPECT_TRUE(prof.faces.empty());
    }
  }
}

TEST(MergeVertices, DeduplicatesReboundPointAxesAndDistances) {
  for (auto kind : {ConstraintKind::kHorizontal, ConstraintKind::kVertical,
                    ConstraintKind::kDistance}) {
    Sketch s;
    const auto a = *s.create_point({0, 0});
    const auto b = *s.create_point({0.1, 0.1});
    const auto c = *s.create_point({20, 20});
    const auto d = *s.create_point({40, 40});
    ASSERT_TRUE(s.create_line(a, c));
    ASSERT_TRUE(s.create_line(b, d));
    auto add = [&](EntityId pt) {
      return kind == ConstraintKind::kDistance
                 ? s.add_dimension(kind, pt, c, 20)
                 : s.add_constraint(kind, pt, c);
    };
    const auto first = add(a);
    const auto duplicate = add(b);
    ASSERT_TRUE(first);
    ASSERT_TRUE(duplicate);
    ASSERT_EQ(merge_nearby_vertices(s, 0.5).status, MergeStatus::kMerged);
    EXPECT_EQ(s.constraints().size(), 1u);
    EXPECT_TRUE(s.constraint(*first));
    EXPECT_FALSE(s.constraint(*duplicate));
  }
}

TEST(Profiles, RejectsNonFiniteAnalyticalArea) {
  Sketch s;
  const auto center = *s.create_point({0, 0});
  ASSERT_TRUE(s.create_circle(center, 1e200));
  const auto prof = analyze_profiles(s);
  EXPECT_FALSE(prof.ready_for_extrude());
  EXPECT_TRUE(prof.faces.empty());
  EXPECT_FALSE(prof.invalid_intersections.empty());
}

TEST(Profiles, KeepsExactTranslatedArcAndCircleAreas) {
  Sketch s;
  constexpr double offset = 1e9;
  const auto bottom_left = *s.create_point({offset, offset - 5});
  const auto bottom_right = *s.create_point({offset + 20, offset - 5});
  const auto top_left = *s.create_point({offset, offset + 5});
  const auto top_right = *s.create_point({offset + 20, offset + 5});
  const auto left = *s.create_point({offset, offset});
  const auto right = *s.create_point({offset + 20, offset});
  ASSERT_TRUE(s.create_line(bottom_left, bottom_right));
  ASSERT_TRUE(s.create_line(top_right, top_left));
  ASSERT_TRUE(s.create_arc(right, 5, -kPi / 2, kPi));
  ASSERT_TRUE(s.create_arc(left, 5, kPi / 2, kPi));
  const auto hole = *s.create_point({offset + 10, offset});
  ASSERT_TRUE(s.create_circle(hole, 2));
  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  ASSERT_EQ(prof.faces[0].holes.size(), 1u);
  EXPECT_NEAR(prof.faces[0].area, 200 + 21 * kPi, 1e-6);
}

TEST(Profiles, AllowsAdjacentCocircularArcsWithoutOverlap) {
  Sketch s;
  const auto center = *s.create_point({0, 0});
  ASSERT_TRUE(s.create_arc(center, 10, 6, 1));
  ASSERT_TRUE(s.create_arc(center, 10, 7, 2 * kPi - 1));
  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  EXPECT_NEAR(prof.faces[0].area, 100 * kPi, 1e-6);
}

TEST(MergeVertices, PreservesDistinctReboundDimensionPayloads) {
  Sketch s;
  const auto a = *s.create_point({0, 0});
  const auto b = *s.create_point({0.1, 0.1});
  const auto c = *s.create_point({20, 0});
  const auto d = *s.create_point({40, 40});
  ASSERT_TRUE(s.create_line(a, c));
  ASSERT_TRUE(s.create_line(b, d));
  const auto driving = s.add_dimension(ConstraintKind::kDistance, a, c, 20);
  const auto reference =
      s.add_dimension(ConstraintKind::kDistance, b, c, 20, true);
  const auto different_value =
      s.add_dimension(ConstraintKind::kDistance, b, c, 21, true);
  const auto placed =
      s.add_dimension(ConstraintKind::kDistance, b, c, 20, true);
  ASSERT_TRUE(driving);
  ASSERT_TRUE(reference);
  ASSERT_TRUE(different_value);
  ASSERT_TRUE(placed);
  ASSERT_TRUE(
      s.set_dimension_placement(*placed, DimensionPlacement{5, 0.3, 0}));
  ASSERT_EQ(merge_nearby_vertices(s, 0.5).status, MergeStatus::kMerged);
  EXPECT_EQ(s.constraints().size(), 4u);
  EXPECT_TRUE(s.constraint(*driving));
  EXPECT_TRUE(s.constraint(*reference));
  EXPECT_TRUE(s.constraint(*different_value));
  ASSERT_TRUE(s.constraint(*placed));
  EXPECT_EQ(s.constraint(*placed)->placement,
            (std::optional<DimensionPlacement>{{5, 0.3, 0}}));
}

TEST(Profiles, AllowsAdjacentArcsWithRoundoffAtAngularSeam) {
  Sketch s;
  const auto center = *s.create_point({0, 0});
  ASSERT_TRUE(s.create_arc(center, 10, 0.2, 1));
  ASSERT_TRUE(s.create_arc(center, 10, 1.2, 2 * kPi - 1));
  const auto prof = analyze_profiles(s);
  EXPECT_TRUE(prof.invalid_intersections.empty());
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 1u);
  EXPECT_NEAR(prof.faces[0].area, 100 * kPi, 1e-6);
}

TEST(Profiles, RetainsSmallFiniteAreaBesideLargerFace) {
  Sketch s;
  const auto center = *s.create_point({100, 100});
  ASSERT_TRUE(s.create_circle(center, 1));
  const std::array<Position, 3> triangle{{{0, 0}, {3e-5, 0}, {0, 3e-5}}};
  ASSERT_TRUE(s.create_polyline(triangle, true));
  const auto prof = analyze_profiles(s);
  ASSERT_TRUE(prof.ready_for_extrude());
  ASSERT_EQ(prof.faces.size(), 2u);
  double smallest_area = prof.faces.front().area;
  for (const auto& face : prof.faces) {
    if (face.area < smallest_area) smallest_area = face.area;
  }
  EXPECT_NEAR(smallest_area, 4.5e-10, 1e-24);
}

}  // namespace
}  // namespace sketchcad
