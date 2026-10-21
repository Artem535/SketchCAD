#include "sketchcad/autosave_schedule.h"

#include <gtest/gtest.h>
using namespace sketchcad;
TEST(AutosaveSchedule, NothingPendingWithoutChanges) {
  AutosaveSchedule s;
  EXPECT_FALSE(s.deadline());
  EXPECT_FALSE(s.due(1000000));
}
TEST(AutosaveSchedule, DebouncesFiveSecondsAfterLastChange) {
  AutosaveSchedule s;
  s.changed(1000);
  EXPECT_EQ(s.deadline(), 6000);
  EXPECT_FALSE(s.due(5999));
  s.changed(3000);
  EXPECT_EQ(s.deadline(), 8000);
  EXPECT_FALSE(s.due(7999));
  EXPECT_TRUE(s.due(8000));
}
TEST(AutosaveSchedule, ContinuousCommitsAreCappedAtThirtySeconds) {
  AutosaveSchedule s;
  for (std::int64_t t = 0; t < 29000; t += 1000) {
    s.changed(t);
    EXPECT_FALSE(s.due(t)) << t;
  }
  s.changed(29000);
  EXPECT_EQ(s.deadline(), 30000);
  EXPECT_FALSE(s.due(29999));
  EXPECT_TRUE(s.due(30000));
}
TEST(AutosaveSchedule, WrittenAndClearResetTheWindow) {
  AutosaveSchedule s;
  s.changed(0);
  s.written();
  EXPECT_FALSE(s.deadline());
  s.changed(40000);
  EXPECT_EQ(s.deadline(), 45000);
  s.clear();
  EXPECT_FALSE(s.deadline());
  s.changed(50000);
  for (std::int64_t t = 50000; t <= 70000; t += 1000) s.changed(t);
  EXPECT_EQ(s.deadline(), 75000);
}
TEST(AutosaveSchedule, FailureRetriesAfterThirtySecondsOncePerEpisode) {
  AutosaveSchedule s;
  s.changed(0);
  EXPECT_TRUE(s.failed(5000));
  EXPECT_TRUE(s.failing());
  EXPECT_EQ(s.deadline(), 35000);
  s.changed(6000);
  EXPECT_EQ(s.deadline(), 35000);
  EXPECT_FALSE(s.due(34999));
  EXPECT_TRUE(s.due(35000));
  EXPECT_FALSE(s.failed(35000));
  EXPECT_EQ(s.deadline(), 65000);
  s.written();
  EXPECT_FALSE(s.failing());
  EXPECT_FALSE(s.deadline());
  s.changed(70000);
  EXPECT_EQ(s.deadline(), 75000);
  EXPECT_TRUE(s.failed(75000));
  s.clear();
  EXPECT_FALSE(s.failing());
}
