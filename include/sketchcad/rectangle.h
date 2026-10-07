#pragma once
#include <array>
#include <optional>
namespace sketchcad {
struct Point {
  unsigned id;
  double x;
  double y;
  bool operator==(const Point&) const = default;
};
class Rectangle {
 public:
  const std::array<Point, 4>& points() const { return points_; }
  bool anchored() const { return anchored_; }
  bool solve(double width, double height, bool anchor,
             std::optional<double> additional_width = std::nullopt);
  bool translate(double dx, double dy);

 private:
  std::array<Point, 4> points_{
      {{1, 0, 0}, {2, 10, 0}, {3, 10, 10}, {4, 0, 10}}};
  bool anchored_ = false;
};
}  // namespace sketchcad
