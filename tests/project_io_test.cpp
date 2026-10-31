#include "sketchcad/project_io.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <locale>
#include <numbers>

#include "sketchcad/solver.h"
using namespace sketchcad;
namespace {
const std::string empty =
    R"({"format":"SketchCAD","version":1,"units":"mm","revision":"0","next_id":"1","entities":[],"constraints":[]})";
std::string changed(std::string s, std::string_view a, std::string_view b) {
  s.replace(s.find(a), a.size(), b);
  return s;
}
}  // namespace
TEST(ProjectIo, EmptyAndFutureFixture) {
  ASSERT_TRUE(std::holds_alternative<ProjectData>(decode_project(empty)));
  EXPECT_EQ(std::get<ProjectError>(decode_project(
                changed(empty, "\"version\":1", "\"version\":3"))),
            ProjectError::kUnsupportedVersion);
}
TEST(ProjectIo, RoundTripEveryPersistentField) {
  Sketch s;
  const auto a = *s.create_point({0, 0}, true), b = *s.create_point({10, 0});
  const auto c = *s.create_point({0, 0}), d = *s.create_point({0, 10});
  const auto e = *s.create_point({0, 20}), f = *s.create_point({10, 20});
  const auto l = *s.create_line(a, b, true), v = *s.create_line(c, d),
             parallel = *s.create_line(e, f);
  const auto center = *s.create_point({0, 5});
  const auto circle = *s.create_circle(center, 5);
  const auto inner = *s.create_circle(*s.create_point({0, 7}), 3, true);
  s.create_arc(*s.create_point({50, 50}), 3, 0.2, 1.5, true);
  using K = ConstraintKind;
  ASSERT_TRUE(s.add_constraint(K::kCoincident, a, c));
  ASSERT_TRUE(s.add_constraint(K::kHorizontal, l));
  ASSERT_TRUE(s.add_constraint(K::kVertical, v));
  ASSERT_TRUE(s.add_constraint(K::kParallel, l, parallel));
  ASSERT_TRUE(s.add_constraint(K::kPerpendicular, l, v));
  ASSERT_TRUE(s.add_constraint(K::kTangent, l, circle));
  ASSERT_TRUE(s.add_constraint(K::kTangent, circle, inner));
  ASSERT_TRUE(s.add_constraint(K::kEqual, l, v));
  ASSERT_TRUE(s.add_constraint(K::kFix, *s.create_point({72, 83})));
  ASSERT_TRUE(s.add_constraint(K::kOnCurve, c, l));
  const auto length = *s.add_dimension(K::kLength, l, 0, 10);
  ASSERT_TRUE(
      s.set_dimension_placement(length, DimensionPlacement{5, 0.25, 0}));
  ASSERT_TRUE(s.add_dimension(K::kDistance, a, b, 10, true));
  ASSERT_TRUE(s.add_dimension(K::kDistance, *s.create_point({0, -4}), l, 4));
  const auto angle = *s.add_dimension(K::kAngle, l, v, std::numbers::pi / 2);
  ASSERT_TRUE(s.set_dimension_placement(angle, DimensionPlacement{8, 0.5, 0}));
  const auto radius = *s.add_dimension(K::kRadius, circle, 0, 5, true);
  ASSERT_TRUE(
      s.set_dimension_placement(radius, DimensionPlacement{10, 0.5, 1.2}));
  const auto erased = *s.create_point({100, 100});
  s.erase(erased);
  ASSERT_EQ(check_constraints(s).status, SolveStatus::kSolved);
  const auto bytes = encode_project({s, 42});
  ASSERT_TRUE(bytes);
  const auto decoded = decode_project(*bytes);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(decoded));
  auto result = std::get<ProjectData>(decoded);
  EXPECT_EQ(result.revision, 42u);
  EXPECT_TRUE(result.sketch == s);
  EXPECT_EQ(result.sketch.snapshot().next_id, erased + 1);
  EXPECT_EQ(*result.sketch.create_point({0, 0}), erased + 1);
}
TEST(ProjectIo, RejectsMalformedAndWrongTypes) {
  const std::vector<std::string> invalid = {
      "{}",
      "null",
      empty + "x",
      empty.substr(0, empty.size() - 1),
      changed(empty, "\"version\":1", "\"version\":1,\"version\":1"),
      changed(empty, "\"version\":1", "\"version\":true"),
      changed(empty, "\"version\":1", "\"version\":1.0"),
      changed(empty, "\"next_id\":\"1\"", "\"next_id\":1"),
      changed(empty, "\"next_id\":\"1\"", "\"next_id\":\"01\""),
      changed(empty, "\"next_id\":\"1\"",
              "\"next_id\":\"18446744073709551616\""),
      changed(empty, "\"revision\":\"0\"",
              "\"revision\":\"18446744073709551615\""),
      changed(empty, "\"units\":\"mm\"", "\"units\":\"in\""),
      changed(empty, "\"units\":\"mm\"", "\"units\":\"mm\",\"extra\":1")};
  for (const auto& bytes : invalid)
    EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(bytes)))
        << bytes;
}
TEST(ProjectIo, PreservesLargeAndExhaustedIds) {
  SketchSnapshot snap;
  snap.entities = {
      SketchPoint{9007199254740993ULL, {1.2345678901234567, -4.3}, true}};
  snap.next_id = 0;
  auto s = Sketch::from_snapshot(snap);
  ASSERT_TRUE(s);
  auto bytes = encode_project({*s, 9007199254740993ULL});
  ASSERT_TRUE(bytes);
  auto decoded = decode_project(*bytes);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(decoded));
  auto result = std::get<ProjectData>(decoded);
  EXPECT_TRUE(result.sketch == *s);
  EXPECT_FALSE(result.sketch.create_point({0, 0}));
}
TEST(ProjectIo, EnforcesLimitsAndDoesNotRepairConstraints) {
  EXPECT_EQ(std::get<ProjectError>(
                decode_project(std::string(kProjectMaxBytes + 1, ' '))),
            ProjectError::kTooLarge);
  EXPECT_TRUE(std::holds_alternative<ProjectError>(
      decode_project(std::string(100, '[') + std::string(100, ']'))));
  Sketch s;
  auto a = *s.create_point({0, 0}), b = *s.create_point({10, 1});
  auto l = *s.create_line(a, b);
  s.add_constraint(ConstraintKind::kHorizontal, l);
  EXPECT_FALSE(encode_project({s, 0}));
}

TEST(ProjectIo, RejectsMissingNullableAndInvalidGeometry) {
  Sketch s;
  const auto a = *s.create_point({0, 0}), b = *s.create_point({10, 0});
  const auto l = *s.create_line(a, b);
  s.add_dimension(ConstraintKind::kLength, l, 0, 10);
  auto bytes = encode_project({s, 0});
  ASSERT_TRUE(bytes);
  const std::vector<std::string> bad = {
      changed(*bytes, ",\"placement\":null", ""),
      changed(*bytes, "\"placement\":null", "\"placement\":{}"),
      changed(*bytes, "\"construction\":false", "\"construction\":0"),
      changed(*bytes, "\"end\":\"2\"", "\"end\":\"999\""),
      changed(*bytes, "\"id\":\"2\"", "\"id\":\"1\""),
      changed(*bytes, "\"kind\":\"point\"", "\"kind\":\"unknown\""),
      changed(*bytes, "\"reference\":false", "\"reference\":1"),
      changed(*bytes, "\"value\":10.0", "\"value\":20.0")};
  for (const auto& value : bad)
    EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(value)))
        << value;
}
TEST(ProjectIo, RecordAndByteBoundaries) {
  std::string bytes = empty;
  bytes.insert(bytes.size() - 1,
               std::string(kProjectMaxBytes - empty.size(), ' '));
  ASSERT_EQ(bytes.size(), kProjectMaxBytes);
  EXPECT_TRUE(std::holds_alternative<ProjectData>(decode_project(bytes)));
  std::string records;
  for (std::size_t i = 0; i <= kProjectMaxRecords; ++i) {
    if (i) records += ',';
    records += "{\"id\":\"" + std::to_string(i + 1) +
               "\",\"kind\":\"point\",\"construction\":false,\"x\":0,\"y\":0}";
  }
  auto large = changed(empty, "\"next_id\":\"1\"", "\"next_id\":\"300000\"");
  large = changed(large, "\"entities\":[]", "\"entities\":[" + records + "]");
  auto result = decode_project(large);
  ASSERT_TRUE(std::holds_alternative<ProjectError>(result));
  EXPECT_EQ(std::get<ProjectError>(result), ProjectError::kTooLarge);
}

TEST(ProjectIo, OpensCommittedFixturesUnderCommaLocale) {
  struct Comma : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
  };
  struct Restore {
    std::locale saved = std::locale();
    ~Restore() { std::locale::global(saved); }
  } restore;
  std::locale::global(std::locale(std::locale::classic(), new Comma));
  std::ifstream file(std::string(PROJECT_FIXTURE_DIR) +
                     "/project-v1.sketchcad");
  ASSERT_TRUE(file.good());
  const std::string bytes((std::istreambuf_iterator<char>(file)), {});
  auto result = decode_project(bytes);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(result));
  auto data = std::get<ProjectData>(result);
  EXPECT_EQ(data.revision, 42u);
  EXPECT_EQ(
      std::get<SketchPoint>(*data.sketch.entity(9007199254740993ULL)).position,
      (Position{1.25, -2.5}));
  auto encoded = encode_project(data);
  ASSERT_TRUE(encoded);
  EXPECT_TRUE(std::holds_alternative<ProjectData>(decode_project(*encoded)));
  std::ifstream future(std::string(PROJECT_FIXTURE_DIR) +
                       "/project-future.sketchcad");
  const std::string newer((std::istreambuf_iterator<char>(future)), {});
  EXPECT_EQ(std::get<ProjectError>(decode_project(newer)),
            ProjectError::kUnsupportedVersion);
}

TEST(ProjectIo, RejectsSideBeforeNarrowing) {
  Sketch s;
  const auto a = *s.create_point({0, 0}), b = *s.create_point({10, 0});
  const auto l = *s.create_line(a, b);
  s.add_dimension(ConstraintKind::kLength, l, 0, 10);
  const auto bytes = encode_project({s, 0});
  ASSERT_TRUE(bytes);
  for (const std::string side : {"4294967297", "-4294967295", "4294967295",
                                 "-4294967297", "18446744073709551615"}) {
    const auto invalid = changed(*bytes, "\"side\":1", "\"side\":" + side);
    EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(invalid)))
        << side;
  }
}
TEST(ProjectIo, FutureEnvelopeDoesNotRequireVersionOneFields) {
  const std::string future =
      R"({"format":"SketchCAD","version":3,"future_field":true})";
  const auto decoded = decode_project(future);
  ASSERT_TRUE(std::holds_alternative<ProjectError>(decoded));
  EXPECT_EQ(std::get<ProjectError>(decoded), ProjectError::kUnsupportedVersion);
}

TEST(ArcBindings, ProjectRoundTripAndRadiusAfterLoad) {
  using K = ConstraintKind;
  Sketch s;
  const auto c = *s.create_point({2, 2});
  const auto px = *s.create_point({2, 0}), fx = *s.create_point({10, 0});
  const auto py = *s.create_point({0, 2}), fy = *s.create_point({0, 10});
  const auto lx = *s.create_line(fx, px), ly = *s.create_line(py, fy);
  const auto arc = *s.create_arc(c, 2, 3.141592653589793, 1.570796326794897);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, ly, arc, py));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, lx, arc, px));
  s.add_constraint(K::kFix, fx);
  s.add_constraint(K::kFix, fy);
  s.add_constraint(K::kHorizontal, lx);
  s.add_constraint(K::kVertical, ly);
  const auto radius = *s.add_dimension(K::kRadius, arc, 0, 2);
  const auto bytes = encode_project({s, 12});
  ASSERT_TRUE(bytes);
  EXPECT_NE(bytes->find("\"version\":2"), std::string::npos);
  auto legacy = *bytes;
  const auto version_start = legacy.find("\"version\":") + 10;
  legacy.replace(version_start, 1, "1");
  EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(legacy)));
  EXPECT_NE(bytes->find("tangent_start"), std::string::npos);
  EXPECT_NE(bytes->find("tangent_end"), std::string::npos);
  const auto decoded = decode_project(*bytes);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(decoded));
  Sketch loaded = std::get<ProjectData>(decoded).sketch;
  EXPECT_EQ(loaded, s);
  ASSERT_TRUE(loaded.set_dimension(radius, 3));
  ASSERT_EQ(solve(loaded).status, SolveStatus::kSolved);
  const auto a = std::get<SketchArc>(*loaded.entity(arc));
  const auto center = std::get<SketchPoint>(*loaded.entity(c)).position;
  const auto contact = std::get<SketchPoint>(*loaded.entity(px)).position;
  EXPECT_NEAR(contact.x,
              center.x + a.radius * std::cos(a.start_angle + a.sweep_angle),
              1e-6);
  EXPECT_NEAR(contact.y,
              center.y + a.radius * std::sin(a.start_angle + a.sweep_angle),
              1e-6);
  EXPECT_TRUE(std::holds_alternative<ProjectError>(
      decode_project(changed(*bytes, "tangent_start", "unknown_binding"))));
  EXPECT_TRUE(std::holds_alternative<ProjectError>(
      decode_project(changed(*bytes, "\"side\":1", "\"side\":0"))));
}

TEST(ArcBindings, ProjectRejectsStaleBoundArcAnglesWithoutRepair) {
  using K = ConstraintKind;
  Sketch s;
  const auto center = *s.create_point({0, 2});
  const auto start = *s.create_point({-2, 2}), end = *s.create_point({0, 0});
  const auto l = *s.create_line(start, *s.create_point({-2, 8}));
  const auto l2 = *s.create_line(*s.create_point({8, 0}), end);
  const auto arc =
      *s.create_arc(center, 2, std::numbers::pi, std::numbers::pi / 2);
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentStart, l, arc, start));
  ASSERT_TRUE(s.add_arc_tangency(K::kTangentEnd, l2, arc, end));
  const auto bytes = encode_project({s, 0});
  ASSERT_TRUE(bytes);
  for (const std::string field : {"start_angle", "sweep_angle"}) {
    auto malformed = *bytes;
    const auto value_start =
        malformed.find("\"" + field + "\":") + field.size() + 3;
    const auto value_end = malformed.find_first_of(",}", value_start);
    malformed.replace(value_start, value_end - value_start, "0.25");
    EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(malformed)))
        << field;
    Sketch stale = s;
    const auto a = std::get<SketchArc>(*stale.entity(arc));
    ASSERT_TRUE(stale.update_arc(
        arc, center, 2, field == "start_angle" ? 0.25 : a.start_angle,
        field == "sweep_angle" ? 0.25 : a.sweep_angle));
    // Hard residuals remain satisfied; editing may synchronize this cache.
    ASSERT_EQ(check_constraints(stale).status, SolveStatus::kSolved);
    const auto before = stale.snapshot();
    EXPECT_FALSE(encode_project({stale, 0})) << field;
    EXPECT_EQ(stale.snapshot().entities, before.entities);
    ASSERT_EQ(solve(stale).status, SolveStatus::kSolved);
    EXPECT_TRUE(encode_project({stale, 0}));
  }
}

TEST(ArcBindings, ProjectRejectsFlippedNormalMetadata) {
  Sketch s;
  const auto p = *s.create_point({0, 0}), q = *s.create_point({4, 0});
  const auto center = *s.create_point({0, 2});
  const auto line = *s.create_line(q, p);
  const auto arc = *s.create_arc(center, 2, 0, 3 * std::numbers::pi / 2);
  ASSERT_TRUE(s.add_arc_tangency(ConstraintKind::kTangentEnd, line, arc, p));
  const auto bytes = encode_project({s, 0});
  ASSERT_TRUE(bytes);
  EXPECT_NE(bytes->find("\"internal\":true"), std::string::npos);
  const auto decoded = decode_project(*bytes);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(decoded));
  EXPECT_EQ(std::get<ProjectData>(decoded).sketch, s);
  auto flipped = *bytes;
  const auto value = flipped.find("\"internal\":") + 11;
  const auto end = flipped.find_first_of(",}", value);
  const bool was_true = flipped.substr(value, end - value) == "true";
  flipped.replace(value, end - value, was_true ? "false" : "true");
  EXPECT_TRUE(std::holds_alternative<ProjectError>(decode_project(flipped)));
}

TEST(ProjectIo, VersionOneAndTwoBackwardCompatibility) {
  const auto v1 = decode_project(empty);
  ASSERT_TRUE(std::holds_alternative<ProjectData>(v1));
  const auto encoded = encode_project(std::get<ProjectData>(v1));
  ASSERT_TRUE(encoded);
  EXPECT_NE(encoded->find("\"version\":1"), std::string::npos);
  EXPECT_TRUE(std::holds_alternative<ProjectData>(decode_project(
      changed(empty, "\"version\":1", "\"version\":2"))));
}
