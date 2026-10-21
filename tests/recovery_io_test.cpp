#include "sketchcad/recovery_io.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "sketchcad/solver.h"
using namespace sketchcad;
namespace {
const RecoveryMeta meta{"0f8fad5b-d9cb-469f-a165-70867728950e", 7,
                        "2026-10-21T08:15:30.250Z",
                        "file:///home/user/%D0%A7.sketchcad"};
std::string valid() {
  return encode_recovery(meta, {Sketch{}, 0}).value_or("");
}
std::string changed(std::string s, std::string_view a, std::string_view b) {
  const auto at = s.find(a);
  EXPECT_NE(at, std::string::npos) << a;
  if (at != std::string::npos) s.replace(at, a.size(), b);
  return s;
}
RecoveryError error_of(const std::string& bytes) {
  const auto result = decode_recovery(bytes);
  EXPECT_TRUE(std::holds_alternative<RecoveryError>(result)) << bytes;
  return std::holds_alternative<RecoveryError>(result)
             ? std::get<RecoveryError>(result)
             : RecoveryError::kInvalid;
}
}  // namespace
TEST(RecoveryIo, RoundTripMetadataAndProject) {
  Sketch s;
  const auto a = *s.create_point({0, 0}, true), b = *s.create_point({10, 0});
  const auto l = *s.create_line(a, b);
  ASSERT_TRUE(s.add_constraint(ConstraintKind::kHorizontal, l));
  const auto length = *s.add_dimension(ConstraintKind::kLength, l, 0, 10);
  ASSERT_TRUE(
      s.set_dimension_placement(length, DimensionPlacement{5, 0.25, 0}));
  ASSERT_TRUE(s.add_dimension(ConstraintKind::kDistance, a, b, 10, true));
  const auto erased = *s.create_point({100, 100});
  ASSERT_TRUE(s.erase(erased));
  ASSERT_EQ(check_constraints(s).status, SolveStatus::kSolved);
  const auto bytes = encode_recovery(meta, {s, 42});
  ASSERT_TRUE(bytes);
  const auto decoded = decode_recovery(*bytes);
  ASSERT_TRUE(std::holds_alternative<RecoveryData>(decoded));
  const auto& data = std::get<RecoveryData>(decoded);
  EXPECT_EQ(data.meta.session_id, meta.session_id);
  EXPECT_EQ(data.meta.generation, 7u);
  EXPECT_EQ(data.meta.captured_at, meta.captured_at);
  EXPECT_EQ(data.meta.original_url, meta.original_url);
  EXPECT_EQ(data.project.revision, 42u);
  EXPECT_TRUE(data.project.sketch == s);
  EXPECT_EQ(data.project.sketch.snapshot().next_id, erased + 1);
  EXPECT_EQ(encode_recovery(data.meta, data.project), bytes);
}
TEST(RecoveryIo, HandAuthoredFixture) {
  std::ifstream in(RECOVERY_FIXTURE_DIR "/recovery-v1.json");
  std::stringstream text;
  text << in.rdbuf();
  const auto decoded = decode_recovery(text.str());
  ASSERT_TRUE(std::holds_alternative<RecoveryData>(decoded));
  const auto& data = std::get<RecoveryData>(decoded);
  EXPECT_EQ(data.meta.generation, 18446744073709551614u);
  EXPECT_EQ(data.meta.original_url, "file:///home/user/Чертёж.sketchcad");
  EXPECT_EQ(data.project.revision, 3u);
  EXPECT_EQ(data.project.sketch.snapshot().next_id, 9u);
  ASSERT_TRUE(data.project.sketch.entity(4));
}
TEST(RecoveryIo, EncodeRejectsInvalidMetadata) {
  for (auto broken :
       {RecoveryMeta{"bad", 1, meta.captured_at, ""},
        RecoveryMeta{meta.session_id, UINT64_MAX, meta.captured_at, ""},
        RecoveryMeta{meta.session_id, 1, "yesterday", ""}})
    EXPECT_FALSE(encode_recovery(broken, {Sketch{}, 0}));
}
TEST(RecoveryIo, ValidatesSessionIdAndTimestamp) {
  EXPECT_TRUE(valid_session_id(meta.session_id));
  for (auto id : {"", "0F8FAD5B-D9CB-469F-A165-70867728950E",
                  "{0f8fad5b-d9cb-469f-a165-70867728950e}",
                  "0f8fad5b-d9cb-469f-a165-70867728950",
                  "0f8fad5bd9cb469fa16570867728950e",
                  "0f8fad5b-d9cb-469f-a165-70867728950g", "../../etc/passwd"})
    EXPECT_FALSE(valid_session_id(id)) << id;
  EXPECT_TRUE(valid_captured_at("2026-02-28T23:59:59.999Z"));
  for (auto t : {"", "2026-10-21T08:15:30Z", "2026-10-21 08:15:30.250Z",
                 "2026-13-21T08:15:30.250Z", "2026-10-32T08:15:30.250Z",
                 "2026-10-21T24:15:30.250Z", "2026-10-21T08:60:30.250Z",
                 "2026-10-21T08:15:61.250Z", "2026-10-21T08:15:30.250+03:00",
                 "2026-10-21T08:15:30.25aZ"})
    EXPECT_FALSE(valid_captured_at(t)) << t;
}
TEST(RecoveryIo, RejectsMalformedEnvelopes) {
  const std::string v = valid();
  ASSERT_TRUE(std::holds_alternative<RecoveryData>(decode_recovery(v)));
  const std::vector<std::string> invalid = {
      "{}",
      "null",
      v + "x",
      v.substr(0, v.size() - 1),
      changed(v, "\"format\":\"SketchCADRecovery\"",
              "\"format\":\"SketchCAD\""),
      changed(v, "\"version\":1", "\"version\":1,\"version\":1"),
      changed(v, "\"version\":1", "\"version\":\"1\""),
      changed(v, "\"version\":1", "\"version\":1,\"extra\":0"),
      changed(v, "\"generation\":\"7\"", "\"generation\":7"),
      changed(v, "\"generation\":\"7\"", "\"generation\":\"07\""),
      changed(v, "\"generation\":\"7\"", "\"generation\":\"-7\""),
      changed(v, "\"generation\":\"7\"",
              "\"generation\":\"18446744073709551615\""),
      changed(v, "\"generation\":\"7\"",
              "\"generation\":\"18446744073709551616\""),
      changed(v, "\"session_id\":\"0f8fad5b", "\"session_id\":\"0F8FAD5B"),
      changed(v, "\"captured_at\":\"2026-10-21T08:15:30.250Z\"",
              "\"captured_at\":\"2026-10-21\""),
      changed(v, "\"original_url\":\"file:///home/user/%D0%A7.sketchcad\"",
              "\"original_url\":null"),
      changed(v, ",\"original_url\":\"file:///home/user/%D0%A7.sketchcad\"",
              ""),
      changed(v, "\"version\":1",
              "\"version\":1,\"x\":[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]"),
  };
  for (const auto& bytes : invalid)
    EXPECT_EQ(error_of(bytes), RecoveryError::kInvalid) << bytes;
  EXPECT_EQ(error_of(changed(v, "\"version\":1", "\"version\":2")),
            RecoveryError::kUnsupportedVersion);
}
TEST(RecoveryIo, RejectsInvalidPayload) {
  const std::string v = valid();
  for (const auto& bytes :
       {changed(v, "\\\"format\\\":\\\"SketchCAD\\\"",
                "\\\"format\\\":\\\"Other\\\""),
        changed(v, "\\\"version\\\":1", "\\\"version\\\":2"),
        changed(v, "\\\"entities\\\":[]", "\\\"entities\\\":[1]")})
    EXPECT_EQ(error_of(bytes), RecoveryError::kInvalidProject) << bytes;
}
TEST(RecoveryIo, SizeLimit) {
  std::string big = valid();
  big.insert(big.size() - 1, std::string(kRecoveryMaxBytes, ' '));
  EXPECT_EQ(error_of(big), RecoveryError::kTooLarge);
}
