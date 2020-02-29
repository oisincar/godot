/*************************************************************************/
/*  navigation_region.cpp                                                */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "navigation_region.h"
#include "core/os/thread.h"
#include "mesh_instance.h"
#include "navigation.h"
#include "servers/navigation_server.h"

void NavigationRegion::set_enabled(bool p_enabled) {

	if (enabled == p_enabled)
		return;
	enabled = p_enabled;

	if (!is_inside_tree())
		return;

	if (!enabled) {

		NavigationServer::get_singleton()->region_set_map(region, RID());
	} else {

		if (navigation) {

			NavigationServer::get_singleton()->region_set_map(region, navigation->get_rid());
		}
	}

	if (debug_view) {
		MeshInstance *dm = Object::cast_to<MeshInstance>(debug_view);
		if (is_enabled()) {
			dm->set_material_override(get_tree()->get_debug_navigation_material());
		} else {
			dm->set_material_override(get_tree()->get_debug_navigation_disabled_material());
		}
	}

	update_gizmo();
}

bool NavigationRegion::is_enabled() const {

	return enabled;
}

/////////////////////////////

void NavigationRegion::_notification(int p_what) {

	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {

			Spatial *c = this;
			while (c) {

				navigation = Object::cast_to<Navigation>(c);
				if (navigation) {

					if (enabled) {

						NavigationServer::get_singleton()->region_set_map(region, navigation->get_rid());
					}
					break;
				}

				c = c->get_parent_spatial();
			}

			if (navmesh.is_valid() && get_tree()->is_debugging_navigation_hint()) {

				MeshInstance *dm = memnew(MeshInstance);
				dm->set_mesh(navmesh->get_debug_mesh());
				if (is_enabled()) {
					dm->set_material_override(get_tree()->get_debug_navigation_material());
				} else {
					dm->set_material_override(get_tree()->get_debug_navigation_disabled_material());
				}
				add_child(dm);
				debug_view = dm;
			}

		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {

			NavigationServer::get_singleton()->region_set_transform(region, get_global_transform());

		} break;
		case NOTIFICATION_EXIT_TREE: {

			if (navigation) {

				NavigationServer::get_singleton()->region_set_map(region, RID());
			}

			if (debug_view) {
				debug_view->queue_delete();
				debug_view = NULL;
			}
			navigation = NULL;
		} break;
	}
}

void NavigationRegion::set_navigation_mesh(const Ref<NavigationMesh> &p_navmesh) {

	if (p_navmesh == navmesh)
		return;

	if (navmesh.is_valid()) {
		navmesh->remove_change_receptor(this);
	}

	navmesh = p_navmesh;

	if (navmesh.is_valid()) {
		navmesh->add_change_receptor(this);
	}

	NavigationServer::get_singleton()->region_set_navmesh(region, p_navmesh);

	if (debug_view && navmesh.is_valid()) {
		Object::cast_to<MeshInstance>(debug_view)->set_mesh(navmesh->get_debug_mesh());
	}

	emit_signal("navigation_mesh_changed");

	update_gizmo();
	update_configuration_warning();
}

Ref<NavigationMesh> NavigationRegion::get_navigation_mesh() const {

	return navmesh;
}

struct BakeThreadsArgs {
	NavigationRegion *nav_mesh_instance;
};

void _bake_navigation_mesh(void *p_user_data) {
	BakeThreadsArgs *args = static_cast<BakeThreadsArgs *>(p_user_data);

	if (args->nav_mesh_instance->get_navigation_mesh().is_valid()) {
		Ref<NavigationMesh> nav_mesh = args->nav_mesh_instance->get_navigation_mesh()->duplicate();

		NavigationServer::get_singleton()->region_bake_navmesh(nav_mesh, args->nav_mesh_instance);
		args->nav_mesh_instance->call_deferred("_bake_finished", nav_mesh);
		memdelete(args);
	} else {

		ERR_PRINT("Can't bake the navigation mesh if the `NavigationMesh` resource doesn't exist");
		args->nav_mesh_instance->call_deferred("_bake_finished", Ref<NavigationMesh>());
		memdelete(args);
	}
}

void NavigationRegion::bake_navigation_mesh() {
	ERR_FAIL_COND(bake_thread != NULL);

	BakeThreadsArgs *args = memnew(BakeThreadsArgs);
	args->nav_mesh_instance = this;

	bake_thread = Thread::create(_bake_navigation_mesh, args);
	ERR_FAIL_COND(bake_thread == NULL);
}

void NavigationRegion::_bake_finished(Ref<NavigationMesh> p_nav_mesh) {
	set_navigation_mesh(p_nav_mesh);
	bake_thread = NULL;
}

String NavigationRegion::get_configuration_warning() const {

	if (!is_visible_in_tree() || !is_inside_tree())
		return String();

	if (!navmesh.is_valid()) {
		return TTR("A NavigationMesh resource must be set or created for this node to work.");
	}
	const Spatial *c = this;
	while (c) {

		if (Object::cast_to<Navigation>(c))
			return String();

		c = Object::cast_to<Spatial>(c->get_parent());
	}

	return TTR("NavigationRegion must be a child or grandchild to a Navigation node. It only provides navigation data.");
}

void NavigationRegion::_bind_methods() {

	ClassDB::bind_method(D_METHOD("set_navigation_mesh", "navmesh"), &NavigationRegion::set_navigation_mesh);
	ClassDB::bind_method(D_METHOD("get_navigation_mesh"), &NavigationRegion::get_navigation_mesh);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &NavigationRegion::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &NavigationRegion::is_enabled);

	ClassDB::bind_method(D_METHOD("bake_navigation_mesh"), &NavigationRegion::bake_navigation_mesh);
	ClassDB::bind_method(D_METHOD("_bake_finished", "nav_mesh"), &NavigationRegion::_bake_finished);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "navmesh", PROPERTY_HINT_RESOURCE_TYPE, "NavigationMesh"), "set_navigation_mesh", "get_navigation_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	ADD_SIGNAL(MethodInfo("navigation_mesh_changed"));
	ADD_SIGNAL(MethodInfo("bake_finished"));
}

void NavigationRegion::_changed_callback(Object *p_changed, const char *p_prop) {
	update_gizmo();
	update_configuration_warning();
}

NavigationRegion::NavigationRegion() {

	enabled = true;
	set_notify_transform(true);
	region = NavigationServer::get_singleton()->region_create();

	navigation = NULL;
	debug_view = NULL;
	bake_thread = NULL;
}

NavigationRegion::~NavigationRegion() {
	if (navmesh.is_valid())
		navmesh->remove_change_receptor(this);
	NavigationServer::get_singleton()->free(region);
}
