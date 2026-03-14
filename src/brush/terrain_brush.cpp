#include "terrain_brush.h"
#include "terrain_operators.h"

import std;
import MapGlobal;
import Terrain;
import DoodadsUndo;
import PathingUndo;
import TerrainUndo;
import Camera;

TerrainBrush::TerrainBrush() : Brush() {
	position_granularity = 1.f;
	size_granularity = 4;
	center_on_tile_corner = true;

	setup_operators();
	set_size(size);
}

void TerrainBrush::setup_operators() {
	height_operator = new HeightOperator(this);
	texture_operator = new TextureOperator(this);
	cliff_operator = new CliffOperator(this);
	cell_operator = new CellOperator(this);

	terrain_operators = {height_operator, texture_operator, cliff_operator, cell_operator};
}

void TerrainBrush::deactivate_operator(TerrainOperator* target) {
	if (target) {
		target->is_active = false;
	}
}

void TerrainBrush::activate_operator(TerrainOperator* target) {
	if (target) {
		target->is_active = true;
		center_on_tile_corner = target->center_on_tile_corner;

		// deactivate incompatible operators
		for (auto* op : terrain_operators) {
			if (op != target && !target->can_combine_with(op)) {
				op->is_active = false;
			}
		}
	}
}

void TerrainBrush::mouse_press_event(QMouseEvent* event, double frame_delta) {
	//if (event->button() == Qt::LeftButton && mode == Mode::selection && !event->modifiers() && input_handler.mouse.y > 0.f) {
	/*auto id = map->render_manager.pick_unit_id_under_mouse(map->units, input_handler.mouse);
		if (id) {
			Unit& unit = map->units.units[id.value()];
			selections = { &unit };
			dragging = true;
			drag_x_offset = input_handler.mouse_world.x - unit.position.x;
			drag_y_offset = input_handler.mouse_world.y - unit.position.y;
			return;
		}*/
	//}

	Brush::mouse_press_event(event, frame_delta);
}

void TerrainBrush::mouse_move_event(QMouseEvent* event, double frame_delta) {
	Brush::mouse_move_event(event, frame_delta);

	/*if (event->buttons() == Qt::LeftButton) {
		if (mode == Mode::selection) {
			if (dragging) {
				if (!dragged) {
					dragged = true;
					map->terrain_undo.new_undo_group();
					unit_state_undo = std::make_unique<UnitStateAction>();
					for (const auto& i : selections) {
						unit_state_undo->old_units.push_back(*i);
					}
				}
				for (auto& i : selections) {
					i->position.x = input_handler.mouse_world.x - drag_x_offset;
					i->position.y = input_handler.mouse_world.y - drag_y_offset;
					i->position.z = map->terrain.interpolated_height(i->position.x, i->position.y);
					i->update();
				}
			} else if (event->modifiers() & Qt::ControlModifier) {
				for (auto&& i : selections) {
					float target_rotation = std::atan2(input_handler.mouse_world.y - i->position.y, input_handler.mouse_world.x - i->position.x);
					if (target_rotation < 0) {
						target_rotation = (glm::pi<float>() + target_rotation) + glm::pi<float>();
					}

					i->angle = target_rotation;
					i->update();
				}
			} else if (selection_started) {
				const glm::vec2 size = glm::vec2(input_handler.mouse_world) - selection_start;
				selections = map->units.query_area({ selection_start.x, selection_start.y, size.x, size.y });
			}
		}
	}*/
}

void TerrainBrush::mouse_release_event(QMouseEvent* event) {
	//dragging = false;
	//if (dragged) {
	//	dragged = false;
	//	for (const auto& i : selections) {
	//		unit_state_undo->new_units.push_back(*i);
	//	}
	//	map->terrain_undo.add_undo_action(std::move(unit_state_undo));
	//}

	Brush::mouse_release_event(event);
}

// Make this an iterative function instead to avoid stack overflows
void TerrainBrush::check_nearby(const int begx, const int begy, const int i, const int j, QRect& area) const {
	QRect bounds = QRect(i - 1, j - 1, 3, 3).intersected({0, 0, map->terrain.width, map->terrain.height});

	for (int k = bounds.x(); k <= bounds.right(); k++) {
		for (int l = bounds.y(); l <= bounds.bottom(); l++) {
			if (k == 0 && l == 0) {
				continue;
			}

			int difference = map->terrain.corners[i][j].layer_height - map->terrain.corners[k][l].layer_height;
			if (std::abs(difference) > 2 && !contains(glm::ivec2(begx + (k - i), begy + (l - k)))) {
				map->terrain.corners[k][l].layer_height = map->terrain.corners[i][j].layer_height - std::clamp(difference, -2, 2);
				map->terrain.corners[k][l].ramp = false;

				area.setX(std::min(area.x(), k - 1));
				area.setY(std::min(area.y(), l - 1));
				area.setRight(std::max(area.right(), k));
				area.setBottom(std::max(area.bottom(), l));

				check_nearby(begx, begy, k, l, area);
			}
		}
	}
}

void TerrainBrush::apply_begin() {
	int width = map->terrain.width;
	int height = map->terrain.height;
	const auto& corners = map->terrain.corners;

	const glm::ivec2 pos = center_on_tile_corner ? glm::vec2(input_handler.mouse_world) - size.x / 4.f / 2.f + 1.f
												 : glm::vec2(input_handler.mouse_world) - size.x / 4.f / 2.f + 0.5f;
	QRect area = QRect(pos.x, pos.y, size.x / 4.f, size.y / 4.f).intersected({0, 0, width, height});

	const int center_x = area.x() + area.width() * 0.5f;
	const int center_y = area.y() + area.height() * 0.5f;

	// setup for undo/redo
	map->world_undo.new_undo_group();
	old_corners = map->terrain.corners;
	old_pathing_cells_static = map->pathing_map.pathing_cells_static;
	texture_height_area = area;
	cliff_area = area;

	// apply all active operators
	for (TerrainOperator* op : terrain_operators) {
		if (op->is_enabled()) {
			op->apply_begin(area, center_x, center_y);
		}
	}
}

void TerrainBrush::apply(double frame_delta) {
	int width = map->terrain.width;
	int height = map->terrain.height;
	auto& corners = map->terrain.corners;

	const glm::ivec2 pos = center_on_tile_corner ? glm::vec2(input_handler.mouse_world) - size.x / 4.f / 2.f + 1.f
												 : glm::vec2(input_handler.mouse_world) - size.x / 4.f / 2.f + 0.5f;

	QRect area = QRect(pos.x, pos.y, size.x / 4.f, size.y / 4.f).intersected({0, 0, width, height});
	QRect updated_area = QRect(pos.x - 1, pos.y - 1, size.x / 4.f + 1, size.y / 4.f + 1).intersected({0, 0, width - 1, height - 1});

	if (area.width() <= 0 || area.height() <= 0) {
		return;
	}

	// apply all active operators
	for (TerrainOperator* op : terrain_operators) {
		if (op->is_active) {
			op->apply(area, frame_delta, updated_area);
		}
	}

	// Apply pathing
	for (int i = updated_area.x(); i <= updated_area.right(); i++) {
		for (int j = updated_area.y(); j <= updated_area.bottom(); j++) {
			for (int k = 0; k < 4; k++) {
				for (int l = 0; l < 4; l++) {
					const size_t pathing_x = i * 4 + k;
					const size_t pathing_y = j * 4 + l;

					map->pathing_map.pathing_cells_static[pathing_y * map->pathing_map.width + pathing_x] &= ~0b01001110;

					uint8_t mask =
						map->terrain
							.get_terrain_pathing(pathing_x, pathing_y, apply_tile_pathing, apply_cliff_pathing, apply_water_pathing);

					map->pathing_map.pathing_cells_static[pathing_y * map->pathing_map.width + pathing_x] |= mask;
				}
			}
		}
	}

	map->pathing_map.upload_static_pathing();

	if (height_operator->is_active || cliff_operator->is_active) {
		if (change_doodad_heights) {
			for (auto&& i : map->doodads.doodads) {
				if (area.contains(i.position.x, i.position.y)) {
					if (std::find_if(
							pre_change_doodads.begin(),
							pre_change_doodads.end(),
							[i](const Doodad& doodad) {
								return doodad.creation_number == i.creation_number;
							}
						)
						== pre_change_doodads.end()) {
						pre_change_doodads.push_back(i);
					}
					i.position.z = map->terrain.interpolated_height(i.position.x, i.position.y, true);
					i.update(map->terrain);
					post_change_doodads[i.creation_number] = i;
				}
			}
		}
		map->units.update_area(updated_area, map->terrain);
	}
}

void TerrainBrush::apply_end() {
	// apply all active operators
	for (TerrainOperator* op : terrain_operators) {
		if (op->is_active) {
			op->apply_end();
		}
	}

	if (change_doodad_heights) {
		auto undo = std::make_unique<DoodadStateAction>();
		undo->old_doodads = pre_change_doodads;
		for (const auto& [id, doodad] : post_change_doodads) {
			undo->new_doodads.push_back(doodad);
		}
		pre_change_doodads.clear();
		post_change_doodads.clear();
		map->world_undo.add_undo_action(std::move(undo));
	}

	QRect pathing_area = QRect(cliff_area.x() * 4, cliff_area.y() * 4, cliff_area.width() * 4, cliff_area.height() * 4)
							 .adjusted(-2, -2, 2, 2)
							 .intersected({0, 0, map->pathing_map.width, map->pathing_map.height});
	add_pathing_undo(pathing_area);

	map->terrain.update_minimap();
}

/// Adds the undo to the current undo group
void TerrainBrush::add_terrain_undo(const QRect& area, TerrainUndoType type) {
	auto undo_action = std::make_unique<TerrainGenericAction>();

	undo_action->area = area;
	undo_action->undo_type = type;

	// Copy old corners
	undo_action->old_corners.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->old_corners.push_back(old_corners[i][j]);
		}
	}

	// Copy new corners
	undo_action->new_corners.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->new_corners.push_back(map->terrain.corners[i][j]);
		}
	}

	map->world_undo.add_undo_action(std::move(undo_action));
}

void TerrainBrush::add_pathing_undo(const QRect& area) {
	auto undo_action = std::make_unique<PathingMapAction>();

	undo_action->area = area;
	const auto width = map->pathing_map.width;

	// Copy old corners
	undo_action->old_pathing.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->old_pathing.push_back(old_pathing_cells_static[j * width + i]);
		}
	}

	// Copy new corners
	undo_action->new_pathing.reserve(area.width() * area.height());
	for (int j = area.top(); j <= area.bottom(); j++) {
		for (int i = area.left(); i <= area.right(); i++) {
			undo_action->new_pathing.push_back(map->pathing_map.pathing_cells_static[j * width + i]);
		}
	}

	map->world_undo.add_undo_action(std::move(undo_action));
}
