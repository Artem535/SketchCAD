#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "sketchcad/sketch.h"

namespace sketchcad {

inline constexpr double kProfileEndpointTolerance = 1e-5;
inline constexpr double kDefaultMergeTolerance = 0.5;

enum class MergeStatus {
  kMerged,
  kNothingToMerge,
  kInvalidTolerance,
  kCollapsedEntity,
  kConflict,
};

struct MergeResult {
  MergeStatus status = MergeStatus::kNothingToMerge;
  std::size_t merged_points = 0;
};

MergeResult merge_nearby_vertices(Sketch& sketch, double tolerance,
                                 bool collapse_short_edges = false);

enum class ProfileEdgeKind {
  kLine,
  kArc,
  kCircle,
};

struct ProfileEdge {
  EntityId entity_id = 0;
  ProfileEdgeKind kind = ProfileEdgeKind::kLine;
  Position start{0, 0};
  Position end{0, 0};
  Position center{0, 0};
  double radius = 0;
  double start_angle = 0;
  double sweep_angle = 0;
};

struct ProfileLoop {
  std::vector<ProfileEdge> edges;
  double signed_area = 0;
};

struct ProfileFace {
  ProfileLoop outer;
  std::vector<ProfileLoop> holes;
  double area = 0;
};

struct OpenEndpoint {
  Position position{0, 0};
  std::optional<EntityId> point_id;
  EntityId curve_id = 0;
};

struct ProfileAnalysis {
  std::vector<ProfileFace> faces;
  std::vector<OpenEndpoint> open_endpoints;
  std::vector<Position> invalid_intersections;

  bool ready_for_extrude() const {
    return !faces.empty() && open_endpoints.empty() &&
           invalid_intersections.empty();
  }
};

ProfileAnalysis analyze_profiles(const Sketch& sketch);

}  // namespace sketchcad
