#pragma once
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "sketchcad/diagnostics.h"
#include "sketchcad/document.h"
#include "sketchcad/sketch_tool.h"
#include "sketchcad/sketch_view.h"
#include "sketchcad/solver.h"

// Bridges the Qt-free document, tool session and view transform to QML.
// Geometry is published as SVG path strings in viewport pixels, one per
// rendering layer (ADR-0002).
class SketchController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString tool READ tool WRITE set_tool NOTIFY changed)
  Q_PROPERTY(QString geometry_path READ geometry_path NOTIFY changed)
  Q_PROPERTY(QString selected_path READ selected_path NOTIFY changed)
  Q_PROPERTY(QString preview_path READ preview_path NOTIFY changed)
  Q_PROPERTY(QString points_path READ points_path NOTIFY changed)
  Q_PROPERTY(QString grid_minor_path READ grid_minor_path NOTIFY changed)
  Q_PROPERTY(QString grid_major_path READ grid_major_path NOTIFY changed)
  Q_PROPERTY(QString axes_path READ axes_path NOTIFY changed)
  Q_PROPERTY(bool snap_visible READ snap_visible NOTIFY changed)
  Q_PROPERTY(double snap_x READ snap_x NOTIFY changed)
  Q_PROPERTY(double snap_y READ snap_y NOTIFY changed)
  Q_PROPERTY(QString snap_kind READ snap_kind NOTIFY changed)
  Q_PROPERTY(bool snap_enabled READ snap_enabled WRITE set_snap_enabled NOTIFY
                 changed)
  Q_PROPERTY(bool finger_draws READ finger_draws WRITE set_finger_draws NOTIFY
                 changed)
  Q_PROPERTY(bool can_undo READ can_undo NOTIFY changed)
  Q_PROPERTY(bool can_redo READ can_redo NOTIFY changed)
  Q_PROPERTY(int entity_count READ entity_count NOTIFY changed)
  Q_PROPERTY(bool has_selection READ has_selection NOTIFY changed)
  Q_PROPERTY(bool in_progress READ in_progress NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(double scale READ scale NOTIFY changed)
  // Current snap grid step in mm (depends on zoom).
  Q_PROPERTY(double grid_step READ grid_step NOTIFY changed)
  // {id, kind, value}; lengths in mm, angles in degrees.
  Q_PROPERTY(QVariantList dimensions READ dimensions NOTIFY changed)
  // Constraint and dimension keys valid for the current selection.
  Q_PROPERTY(QStringList applicable READ applicable NOTIFY changed)
  // {id, kind, value, entities, dependent, violated}.
  Q_PROPERTY(QVariantList constraints READ constraints NOTIFY changed)
  Q_PROPERTY(qulonglong selected_constraint READ selected_constraint WRITE
                 set_selected_constraint NOTIFY changed)
  Q_PROPERTY(QString constraint_path READ constraint_path NOTIFY changed)
  Q_PROPERTY(QString conflict_path READ conflict_path NOTIFY changed)
  // consistent, redundant, conflicting or unknown.
  Q_PROPERTY(QString diagnosis READ diagnosis NOTIFY changed)
  Q_PROPERTY(int dof READ dof NOTIFY changed)  // -1 when unknown.
  Q_PROPERTY(QString diagnosis_reason READ diagnosis_reason NOTIFY changed)
  // {id, x, y, text} in viewport pixels.
  Q_PROPERTY(QVariantList dimension_labels READ dimension_labels NOTIFY changed)
  // ESKD dimension graphics (dimension-style.adoc): lines/arcs and arrows.
  Q_PROPERTY(QString dimension_path READ dimension_path NOTIFY changed)
  Q_PROPERTY(QString dimension_arrows_path READ dimension_arrows_path
                 NOTIFY changed)
  // U05 inspector: {label, value, unit} rows and title for the selection.
  Q_PROPERTY(QVariantList selection_properties READ selection_properties
                 NOTIFY changed)
  Q_PROPERTY(QString selection_title READ selection_title NOTIFY changed)
  // Points with a fix constraint, drawn as filled handles.
  Q_PROPERTY(QString fixed_path READ fixed_path NOTIFY changed)

 public:
  explicit SketchController(QObject* parent = nullptr);

  const sketchcad::Document& document() const { return document_; }

  QString tool() const;
  void set_tool(const QString& name);
  QString geometry_path() const { return geometry_path_; }
  QString selected_path() const { return selected_path_; }
  QString preview_path() const { return preview_path_; }
  QString points_path() const { return points_path_; }
  QString grid_minor_path() const { return grid_minor_path_; }
  QString grid_major_path() const { return grid_major_path_; }
  QString axes_path() const { return axes_path_; }
  bool snap_visible() const;
  double snap_x() const;
  double snap_y() const;
  QString snap_kind() const;
  bool snap_enabled() const { return snap_enabled_; }
  void set_snap_enabled(bool enabled);
  bool finger_draws() const { return finger_draws_; }
  void set_finger_draws(bool enabled);
  bool can_undo() const { return document_.can_undo(); }
  bool can_redo() const { return document_.can_redo(); }
  int entity_count() const;
  bool has_selection() const { return session_.selection().has_value(); }
  bool in_progress() const { return session_.in_progress(); }
  // Translation key of the last rejected action, empty when none.
  QString message() const { return message_; }
  double scale() const { return view_.scale(); }
  double grid_step() const { return session_.snap_settings().grid_step_mm; }
  QVariantList dimensions() const;
  QStringList applicable() const;
  QVariantList constraints() const;
  qulonglong selected_constraint() const { return selected_constraint_; }
  void set_selected_constraint(qulonglong id);
  QString constraint_path() const { return constraint_path_; }
  QString conflict_path() const { return conflict_path_; }
  QString diagnosis() const;
  int dof() const;
  QString diagnosis_reason() const;
  QVariantList dimension_labels() const;
  QVariantList selection_properties() const;
  QString selection_title() const;
  QString fixed_path() const { return fixed_path_; }
  QString dimension_path() const { return dimension_path_; }
  QString dimension_arrows_path() const { return dimension_arrows_path_; }

  Q_INVOKABLE void set_viewport_size(double width, double height);
  Q_INVOKABLE void hover(double x, double y);
  Q_INVOKABLE void press(double x, double y);
  Q_INVOKABLE void drag(double x, double y);
  Q_INVOKABLE void release(double x, double y);
  Q_INVOKABLE void pan(double dx, double dy);
  Q_INVOKABLE void zoom_at(double x, double y, double factor);
  Q_INVOKABLE void fit();
  Q_INVOKABLE bool finish();
  Q_INVOKABLE bool cancel();
  Q_INVOKABLE void clear_selection();
  Q_INVOKABLE bool delete_selection();
  Q_INVOKABLE bool undo();
  Q_INVOKABLE bool redo();
  // Creates a dimension at the current measured value; 0 on failure.
  Q_INVOKABLE qulonglong add_dimension(const QString& kind, qulonglong first,
                                       qulonglong second = 0);
  // Angles in degrees. False with a message key when rejected.
  Q_INVOKABLE bool set_dimension(qulonglong id, double value);
  // Creates the constraint or dimension `key` on the selection; 0 when it is
  // not applicable (message invalid_action) or rejected.
  Q_INVOKABLE qulonglong apply(const QString& key);
  Q_INVOKABLE bool remove_constraint(qulonglong id);
  // Highlights the constraints the diagnosis lists as dependent.
  Q_INVOKABLE void highlight_dependent();
  Q_INVOKABLE QPointF screen_of(double x_mm, double y_mm) const;
  Q_INVOKABLE QPointF world_of(double x, double y) const;

 signals:
  void changed();

 private:
  sketchcad::Position world(double x, double y) const;
  void sync_tolerances();
  void refresh_scene();
  void refresh_grid();
  void refresh_dimensions();
  QString curve_path(const sketchcad::Sketch& sketch,
                     const sketchcad::Entity& entity) const;
  QString marker_path(sketchcad::Position p, double radius_px) const;
  QString entity_path(const sketchcad::Sketch& sketch, sketchcad::EntityId id,
                      double marker_px) const;
  std::optional<sketchcad::EntityId> add_action(sketchcad::Sketch& sketch,
                                                const QString& key) const;
  // Diagnosis and applicable actions, recomputed only when needed.
  void refresh_analysis();
  // After a rejected command, highlights the conflict of `candidate`.
  void highlight_rejected(bool ok, const sketchcad::Sketch& candidate);

  sketchcad::Document document_;
  sketchcad::ToolSession session_{document_};
  sketchcad::ViewTransform view_;
  double width_ = 0;
  double height_ = 0;
  bool snap_enabled_ = true;
  bool finger_draws_ = true;
  bool snap_hint_ = false;
  sketchcad::SolveStatus last_status_ = sketchcad::SolveStatus::kSolved;
  QString message_;
  QString geometry_path_;
  QString selected_path_;
  QString preview_path_;
  QString points_path_;
  QString grid_minor_path_;
  QString grid_major_path_;
  QString axes_path_;
  QString constraint_path_;
  QString conflict_path_;
  QString fixed_path_;
  QString dimension_path_;
  QString dimension_arrows_path_;
  qulonglong selected_constraint_ = 0;
  std::vector<sketchcad::EntityId> conflict_entities_;
  sketchcad::Diagnosis diagnosis_;
  std::uint64_t analysed_revision_ = 0;
  bool analysed_ = false;
  QStringList applicable_;
  std::optional<std::vector<sketchcad::EntityId>> applicable_selection_;
};
