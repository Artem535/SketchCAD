#include "sketchcad/rectangle.h"

#include <gtest/gtest.h>

#include <limits>
using sketchcad::Rectangle;
TEST(Rectangle, SolvesDimensionsAndPreservesIds) {
  Rectangle r;
  ASSERT_TRUE(r.solve(50, 30, false));
  auto p = r.points();
  EXPECT_NEAR(p[1].x - p[0].x, 50, 1e-7);
  EXPECT_NEAR(p[2].y - p[1].y, 30, 1e-7);
  EXPECT_NEAR(p[0].y, p[1].y, 1e-7);
  EXPECT_NEAR(p[2].y, p[3].y, 1e-7);
  EXPECT_NEAR(p[0].x, p[3].x, 1e-7);
  EXPECT_NEAR(p[1].x, p[2].x, 1e-7);
  for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(p[i].id, i + 1);
}
TEST(Rectangle, TranslatesFreeGeometry) {
  Rectangle r;
  ASSERT_TRUE(r.solve(50, 30, false));
  auto p = r.points();
  ASSERT_TRUE(r.translate(7, -9));
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(r.points()[i].x, p[i].x + 7, 1e-7);
    EXPECT_NEAR(r.points()[i].y, p[i].y - 9, 1e-7);
  }
}
TEST(Rectangle, AnchorPreventsTranslation) {
  Rectangle r;
  ASSERT_TRUE(r.solve(50, 30, true));
  EXPECT_TRUE(r.anchored());
  EXPECT_NEAR(r.points()[0].x, 0, 1e-7);
  EXPECT_NEAR(r.points()[0].y, 0, 1e-7);
  auto p = r.points();
  EXPECT_FALSE(r.translate(1, 1));
  EXPECT_EQ(p, r.points());
  ASSERT_TRUE(r.solve(50, 30, false));
  EXPECT_TRUE(r.translate(1, 1));
}
TEST(Rectangle, ConflictRollsBackGeometryAndAnchor) {
  Rectangle r;
  ASSERT_TRUE(r.solve(50, 30, false));
  auto p = r.points();
  EXPECT_FALSE(r.solve(50, 30, true, 60));
  EXPECT_EQ(p, r.points());
  EXPECT_FALSE(r.anchored());
  EXPECT_TRUE(r.solve(50, 30, false, 50));
}
TEST(Rectangle, InvalidInputsRollBack) {
  Rectangle r;
  auto p = r.points();
  for (double v : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_FALSE(r.solve(v, 30, true));
    EXPECT_FALSE(r.solve(50, v, true));
    EXPECT_EQ(p, r.points());
    EXPECT_FALSE(r.anchored());
  }
  EXPECT_FALSE(r.translate(std::numeric_limits<double>::infinity(), 0));
  EXPECT_EQ(p, r.points());
}
