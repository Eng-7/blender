/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/intern/wm_manipulator_map.c
 *  \ingroup wm
 */

#include <string.h>

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_ghash.h"

#include "BKE_context.h"
#include "BKE_global.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_glew.h"
#include "GPU_matrix.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm_event_system.h"

/* own includes */
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"

/**
 * Store all manipulator-maps here. Anyone who wants to register a manipulator for a certain
 * area type can query the manipulator-map to do so.
 */
static ListBase manipulatormaptypes = {NULL, NULL};

/**
 * Update when manipulator-map types change.
 */
/* so operator removal can trigger update */
enum {
	WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_INIT = (1 << 0),
	WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_REMOVE = (1 << 1),
};

static char wm_mmap_type_update_flag = 0;

/**
 * Manipulator-map update tagging.
 */
enum eManipulatorMapUpdateFlags {
	/* Tag manipulator-map for refresh. */
	MANIPULATORMAP_REFRESH = (1 << 0),
};


/* -------------------------------------------------------------------- */
/** \name wmManipulatorMap
 *
 * \{ */

/**
 * Creates a manipulator-map with all registered manipulators for that type
 */
wmManipulatorMap *WM_manipulatormap_new_from_type(
        const struct wmManipulatorMapType_Params *mmap_params)
{
	wmManipulatorMapType *mmap_type = WM_manipulatormaptype_ensure(mmap_params);
	wmManipulatorMap *mmap;

	mmap = MEM_callocN(sizeof(wmManipulatorMap), "ManipulatorMap");
	mmap->type = mmap_type;
	mmap->update_flag = MANIPULATORMAP_REFRESH;

	/* create all manipulator-groups for this manipulator-map. We may create an empty one
	 * too in anticipation of manipulators from operators etc */
	for (wmManipulatorGroupTypeRef *wgt_ref = mmap_type->grouptype_refs.first; wgt_ref; wgt_ref = wgt_ref->next) {
		wm_manipulatorgroup_new_from_type(mmap, wgt_ref->type);
	}

	return mmap;
}

void wm_manipulatormap_selected_clear(wmManipulatorMap *mmap)
{
	MEM_SAFE_FREE(mmap->mmap_context.selected);
	mmap->mmap_context.selected_len = 0;
}

void wm_manipulatormap_remove(wmManipulatorMap *mmap)
{
	if (!mmap)
		return;

	for (wmManipulatorGroup *mgroup = mmap->groups.first, *mgroup_next; mgroup; mgroup = mgroup_next) {
		mgroup_next = mgroup->next;
		BLI_assert(mgroup->parent_mmap == mmap);
		wm_manipulatorgroup_free(NULL, mgroup);
	}
	BLI_assert(BLI_listbase_is_empty(&mmap->groups));

	wm_manipulatormap_selected_clear(mmap);

	MEM_freeN(mmap);
}

const ListBase *WM_manipulatormap_group_list(wmManipulatorMap *mmap)
{
	return &mmap->groups;
}

bool WM_manipulatormap_is_any_selected(const wmManipulatorMap *mmap)
{
	return mmap->mmap_context.selected_len != 0;
}

/**
 * \note We could use a callback to define bounds, for now just use matrix location.
 */
bool WM_manipulatormap_minmax(
        const wmManipulatorMap *mmap, bool UNUSED(use_hidden), bool use_select,
        float r_min[3], float r_max[3])
{
	if (use_select) {
		int i;
		for (i = 0; i < mmap->mmap_context.selected_len; i++) {
			minmax_v3v3_v3(r_min, r_max, mmap->mmap_context.selected[i]->matrix_basis[3]);
		}
		return i != 0;
	}
	else {
		bool ok = false;
		BLI_assert(!"TODO");
		return ok;
	}
}

/**
 * Creates and returns idname hash table for (visible) manipulators in \a mmap
 *
 * \param poll  Polling function for excluding manipulators.
 * \param data  Custom data passed to \a poll
 *
 * TODO(campbell): this uses unreliable order,
 * best we use an iterator function instead of a hash.
 */
static GHash *WM_manipulatormap_manipulator_hash_new(
        const bContext *C, wmManipulatorMap *mmap,
        bool (*poll)(const wmManipulator *, void *),
        void *data, const bool include_hidden)
{
	GHash *hash = BLI_ghash_str_new(__func__);

	/* collect manipulators */
	for (wmManipulatorGroup *mgroup = mmap->groups.first; mgroup; mgroup = mgroup->next) {
		if (!mgroup->type->poll || mgroup->type->poll(C, mgroup->type)) {
			for (wmManipulator *mpr = mgroup->manipulators.first; mpr; mpr = mpr->next) {
				if ((include_hidden || (mpr->flag & WM_MANIPULATOR_HIDDEN) == 0) &&
				    (!poll || poll(mpr, data)))
				{
					BLI_ghash_insert(hash, mpr->name, mpr);
				}
			}
		}
	}

	return hash;
}

void WM_manipulatormap_tag_refresh(wmManipulatorMap *mmap)
{
	if (mmap) {
		mmap->update_flag |= MANIPULATORMAP_REFRESH;
	}
}

static void manipulatormap_tag_updated(wmManipulatorMap *mmap)
{
	mmap->update_flag = 0;
}

static bool manipulator_prepare_drawing(
        wmManipulatorMap *mmap, wmManipulator *mpr,
        const bContext *C, ListBase *draw_manipulators)
{
	int do_draw = wm_manipulator_is_visible(mpr);
	if (do_draw == 0) {
		/* skip */
	}
	else {
		if (do_draw & WM_MANIPULATOR_IS_VISIBLE_UPDATE) {
			/* hover manipulators need updating, even if we don't draw them */
			wm_manipulator_update(mpr, C, (mmap->update_flag & MANIPULATORMAP_REFRESH) != 0);
		}
		if (do_draw & WM_MANIPULATOR_IS_VISIBLE_DRAW) {
			BLI_addhead(draw_manipulators, BLI_genericNodeN(mpr));
		}
		return true;
	}

	return false;
}

/**
 * Update manipulators of \a mmap to prepare for drawing. Adds all manipulators that
 * should be drawn to list \a draw_manipulators, note that added items need freeing.
 */
static void manipulatormap_prepare_drawing(
        wmManipulatorMap *mmap, const bContext *C, ListBase *draw_manipulators, const int drawstep)
{
	if (!mmap || BLI_listbase_is_empty(&mmap->groups))
		return;
	wmManipulator *mpr_modal = mmap->mmap_context.modal;

	/* only active manipulator needs updating */
	if (mpr_modal) {
		if ((mpr_modal->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_DRAW_MODAL_ALL) == 0) {
			if (manipulator_prepare_drawing(mmap, mpr_modal, C, draw_manipulators)) {
				manipulatormap_tag_updated(mmap);
			}
			/* don't draw any other manipulators */
			return;
		}
	}

	for (wmManipulatorGroup *mgroup = mmap->groups.first; mgroup; mgroup = mgroup->next) {
		/* check group visibility - drawstep first to avoid unnecessary call of group poll callback */
		if (!wm_manipulatorgroup_is_visible_in_drawstep(mgroup, drawstep) ||
		    !wm_manipulatorgroup_is_visible(mgroup, C))
		{
			continue;
		}

		/* needs to be initialized on first draw */
		wm_manipulatorgroup_ensure_initialized(mgroup, C);
		/* update data if needed */
		/* XXX weak: Manipulator-group may skip refreshing if it's invisible (map gets untagged nevertheless) */
		if (mmap->update_flag & MANIPULATORMAP_REFRESH && mgroup->type->refresh) {
			mgroup->type->refresh(C, mgroup);
		}
		/* prepare drawing */
		if (mgroup->type->draw_prepare) {
			mgroup->type->draw_prepare(C, mgroup);
		}

		for (wmManipulator *mpr = mgroup->manipulators.first; mpr; mpr = mpr->next) {
			manipulator_prepare_drawing(mmap, mpr, C, draw_manipulators);
		}
	}

	manipulatormap_tag_updated(mmap);
}

/**
 * Draw all visible manipulators in \a mmap.
 * Uses global draw_manipulators listbase.
 */
static void manipulators_draw_list(const wmManipulatorMap *mmap, const bContext *C, ListBase *draw_manipulators)
{
	/* Can be empty if we're dynamically added and removed. */
	if ((mmap == NULL) || BLI_listbase_is_empty(&mmap->groups)) {
		return;
	}

	const bool draw_multisample = (U.ogl_multisamples != USER_MULTISAMPLE_NONE);

	/* TODO this will need it own shader probably? don't think it can be handled from that point though. */
/*	const bool use_lighting = (U.manipulator_flag & V3D_MANIPULATOR_SHADED) != 0; */

	/* enable multisampling */
	if (draw_multisample) {
		glEnable(GL_MULTISAMPLE);
	}

	bool is_depth_prev = false;

	/* draw_manipulators contains all visible manipulators - draw them */
	for (LinkData *link = draw_manipulators->first, *link_next; link; link = link_next) {
		wmManipulator *mpr = link->data;
		link_next = link->next;

		bool is_depth = (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_DEPTH_3D) != 0;

		/* Weak! since we don't 100% support depth yet (select ignores depth) always show highlighted */
		if (is_depth && (mpr->state & WM_MANIPULATOR_STATE_HIGHLIGHT)) {
			is_depth = false;
		}

		if (is_depth == is_depth_prev) {
			/* pass */
		}
		else {
			if (is_depth) {
				glEnable(GL_DEPTH_TEST);
			}
			else {
				glDisable(GL_DEPTH_TEST);
			}
			is_depth_prev = is_depth;
		}

		mpr->type->draw(C, mpr);
		/* free/remove manipulator link after drawing */
		BLI_freelinkN(draw_manipulators, link);
	}

	if (is_depth_prev) {
		glDisable(GL_DEPTH_TEST);
	}

	if (draw_multisample) {
		glDisable(GL_MULTISAMPLE);
	}
}

void WM_manipulatormap_draw(wmManipulatorMap *mmap, const bContext *C, const int drawstep)
{
	ListBase draw_manipulators = {NULL};

	manipulatormap_prepare_drawing(mmap, C, &draw_manipulators, drawstep);
	manipulators_draw_list(mmap, C, &draw_manipulators);
	BLI_assert(BLI_listbase_is_empty(&draw_manipulators));
}

static void manipulator_draw_select_3D_loop(const bContext *C, ListBase *visible_manipulators)
{
	int selectionbase = 0;
	wmManipulator *mpr;

	/* TODO(campbell): this depends on depth buffer being written to, currently broken for the 3D view. */
	bool is_depth_prev = false;

	for (LinkData *link = visible_manipulators->first; link; link = link->next) {
		mpr = link->data;
		
		bool is_depth = (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_DEPTH_3D) != 0;
		if (is_depth == is_depth_prev) {
			/* pass */
		}
		else {
			if (is_depth) {
				glEnable(GL_DEPTH_TEST);
			}
			else {
				glDisable(GL_DEPTH_TEST);
			}
			is_depth_prev = is_depth;
		}

		/* pass the selection id shifted by 8 bits. Last 8 bits are used for selected manipulator part id */

		mpr->type->draw_select(C, mpr, selectionbase << 8);


		selectionbase++;
	}

	if (is_depth_prev) {
		glDisable(GL_DEPTH_TEST);
	}
}

static int manipulator_find_intersected_3d_intern(
        ListBase *visible_manipulators, const bContext *C, const int co[2],
        const float hotspot)
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);
	View3D *v3d = sa->spacedata.first;
	rcti rect;
	/* Almost certainly overkill, but allow for many custom manipulators. */
	GLuint buffer[MAXPICKBUF];
	short hits;
	const bool do_passes = GPU_select_query_check_active();
	rect.xmin = co[0] - hotspot;
	rect.xmax = co[0] + hotspot;
	rect.ymin = co[1] - hotspot;
	rect.ymax = co[1] + hotspot;

	ED_view3d_draw_setup_view(CTX_wm_window(C), C, CTX_data_scene(C), ar, v3d, NULL, NULL, &rect);

	if (do_passes)
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &rect, GPU_SELECT_NEAREST_FIRST_PASS, 0);
	else
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &rect, GPU_SELECT_ALL, 0);
	/* do the drawing */
	manipulator_draw_select_3D_loop(C, visible_manipulators);

	hits = GPU_select_end();

	if (do_passes && (hits > 0)) {
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &rect, GPU_SELECT_NEAREST_SECOND_PASS, hits);
		manipulator_draw_select_3D_loop(C, visible_manipulators);
		GPU_select_end();
	}

	ED_view3d_draw_setup_view(CTX_wm_window(C), C, CTX_data_scene(C), ar, v3d, NULL, NULL, NULL);

	const GLuint *hit_near = GPU_select_buffer_near(buffer, hits);

	return hit_near ? hit_near[3] : -1;
}

/**
 * Try to find a 3D manipulator at screen-space coordinate \a co. Uses OpenGL picking.
 */
static wmManipulator *manipulator_find_intersected_3d(
        bContext *C, const int co[2], ListBase *visible_manipulators,
        int *r_part)
{
	wmManipulator *result = NULL;
	const float hotspot = 14.0f;
	int ret;

	*r_part = 0;
	/* set up view matrices */
	view3d_operator_needs_opengl(C);

	ret = manipulator_find_intersected_3d_intern(visible_manipulators, C, co, 0.5f * hotspot);

	if (ret != -1) {
		LinkData *link;
		int retsec;
		retsec = manipulator_find_intersected_3d_intern(visible_manipulators, C, co, 0.2f * hotspot);

		if (retsec != -1)
			ret = retsec;

		link = BLI_findlink(visible_manipulators, ret >> 8);
		*r_part = ret & 255;
		result = link->data;
	}

	return result;
}

/**
 * Try to find a manipulator under the mouse position. 2D intersections have priority over
 * 3D ones (could check for smallest screen-space distance but not needed right now).
 */
wmManipulator *wm_manipulatormap_highlight_find(
        wmManipulatorMap *mmap, bContext *C, const wmEvent *event,
        int *r_part)
{
	wmManipulator *mpr = NULL;
	ListBase visible_3d_manipulators = {NULL};

	for (wmManipulatorGroup *mgroup = mmap->groups.first; mgroup; mgroup = mgroup->next) {
		if (wm_manipulatorgroup_is_visible(mgroup, C)) {
			if (mgroup->type->flag & WM_MANIPULATORGROUPTYPE_3D) {
				wm_manipulatorgroup_intersectable_manipulators_to_list(mgroup, &visible_3d_manipulators);
			}
			else if ((mpr = wm_manipulatorgroup_find_intersected_mainpulator(mgroup, C, event, r_part))) {
				break;
			}
		}
	}

	if (!BLI_listbase_is_empty(&visible_3d_manipulators)) {
		mpr = manipulator_find_intersected_3d(C, event->mval, &visible_3d_manipulators, r_part);
		BLI_freelistN(&visible_3d_manipulators);
	}

	return mpr;
}

void WM_manipulatormap_add_handlers(ARegion *ar, wmManipulatorMap *mmap)
{
	wmEventHandler *handler = MEM_callocN(sizeof(wmEventHandler), "manipulator handler");

	BLI_assert(mmap == ar->manipulator_map);
	handler->manipulator_map = mmap;
	BLI_addtail(&ar->handlers, handler);
}

void wm_manipulatormaps_handled_modal_update(
        bContext *C, wmEvent *event, wmEventHandler *handler)
{
	const bool modal_running = (handler->op != NULL);

	/* happens on render or when joining areas */
	if (!handler->op_region || !handler->op_region->manipulator_map) {
		return;
	}

	wmManipulatorMap *mmap = handler->op_region->manipulator_map;
	wmManipulator *mpr = wm_manipulatormap_modal_get(mmap);
	ScrArea *area = CTX_wm_area(C);
	ARegion *region = CTX_wm_region(C);

	wm_manipulatormap_handler_context(C, handler);

	/* regular update for running operator */
	if (modal_running) {
		if (mpr && (mpr->op_data.type != NULL) &&
		    (mpr->op_data.type == handler->op->type))
		{
			if (mpr->custom_modal) {
				mpr->custom_modal(C, mpr, event, 0);
			}
			else if (mpr->type->modal) {
				mpr->type->modal(C, mpr, event, 0);
			}
		}
	}
	/* operator not running anymore */
	else {
		wm_manipulatormap_highlight_set(mmap, C, NULL, 0);
		wm_manipulatormap_modal_set(mmap, C, event, NULL);
	}

	/* restore the area */
	CTX_wm_area_set(C, area);
	CTX_wm_region_set(C, region);
}

/**
 * Deselect all selected manipulators in \a mmap.
 * \return if selection has changed.
 */
bool wm_manipulatormap_deselect_all(wmManipulatorMap *mmap, wmManipulator ***sel)
{
	if (*sel == NULL || mmap->mmap_context.selected_len == 0)
		return false;

	for (int i = 0; i < mmap->mmap_context.selected_len; i++) {
		(*sel)[i]->state &= ~WM_MANIPULATOR_STATE_SELECT;
		(*sel)[i] = NULL;
	}
	wm_manipulatormap_selected_clear(mmap);

	/* always return true, we already checked
	 * if there's anything to deselect */
	return true;
}

BLI_INLINE bool manipulator_selectable_poll(const wmManipulator *mpr, void *UNUSED(data))
{
	return (mpr->parent_mgroup->type->flag & WM_MANIPULATORGROUPTYPE_SELECT);
}

/**
 * Select all selectable manipulators in \a mmap.
 * \return if selection has changed.
 */
static bool wm_manipulatormap_select_all_intern(
        bContext *C, wmManipulatorMap *mmap, wmManipulator ***sel,
        const int action)
{
	/* GHash is used here to avoid having to loop over all manipulators twice (once to
	 * get tot_sel for allocating, once for actually selecting). Instead we collect
	 * selectable manipulators in hash table and use this to get tot_sel and do selection */

	GHash *hash = WM_manipulatormap_manipulator_hash_new(C, mmap, manipulator_selectable_poll, NULL, true);
	GHashIterator gh_iter;
	int i, *selected_len = &mmap->mmap_context.selected_len;
	bool changed = false;

	*selected_len = BLI_ghash_size(hash);
	*sel = MEM_reallocN(*sel, sizeof(**sel) * (*selected_len));

	GHASH_ITER_INDEX (gh_iter, hash, i) {
		wmManipulator *mpr_iter = BLI_ghashIterator_getValue(&gh_iter);

		if ((mpr_iter->state & WM_MANIPULATOR_STATE_SELECT) == 0) {
			changed = true;
		}
		mpr_iter->state |= WM_MANIPULATOR_STATE_SELECT;
		if (mpr_iter->type->select) {
			mpr_iter->type->select(C, mpr_iter, action);
		}
		(*sel)[i] = mpr_iter;
		BLI_assert(i < (*selected_len));
	}
	/* highlight first manipulator */
	wm_manipulatormap_highlight_set(mmap, C, (*sel)[0], (*sel)[0]->highlight_part);

	BLI_ghash_free(hash, NULL, NULL);
	return changed;
}

/**
 * Select/Deselect all selectable manipulators in \a mmap.
 * \return if selection has changed.
 *
 * TODO select all by type
 */
bool WM_manipulatormap_select_all(bContext *C, wmManipulatorMap *mmap, const int action)
{
	wmManipulator ***sel = &mmap->mmap_context.selected;
	bool changed = false;

	switch (action) {
		case SEL_SELECT:
			changed = wm_manipulatormap_select_all_intern(C, mmap, sel, action);
			break;
		case SEL_DESELECT:
			changed = wm_manipulatormap_deselect_all(mmap, sel);
			break;
		default:
			BLI_assert(0);
			break;
	}

	if (changed)
		WM_event_add_mousemove(C);

	return changed;
}

/**
 * Prepare context for manipulator handling (but only if area/region is
 * part of screen). Version of #wm_handler_op_context for manipulators.
 */
void wm_manipulatormap_handler_context(bContext *C, wmEventHandler *handler)
{
	bScreen *screen = CTX_wm_screen(C);

	if (screen) {
		if (handler->op_area == NULL) {
			/* do nothing in this context */
		}
		else {
			ScrArea *sa;

			for (sa = screen->areabase.first; sa; sa = sa->next)
				if (sa == handler->op_area)
					break;
			if (sa == NULL) {
				/* when changing screen layouts with running modal handlers (like render display), this
				 * is not an error to print */
				if (handler->manipulator_map == NULL)
					printf("internal error: modal manipulator-map handler has invalid area\n");
			}
			else {
				ARegion *ar;
				CTX_wm_area_set(C, sa);
				for (ar = sa->regionbase.first; ar; ar = ar->next)
					if (ar == handler->op_region)
						break;
				/* XXX no warning print here, after full-area and back regions are remade */
				if (ar)
					CTX_wm_region_set(C, ar);
			}
		}
	}
}

bool WM_manipulatormap_cursor_set(const wmManipulatorMap *mmap, wmWindow *win)
{
	for (; mmap; mmap = mmap->next) {
		wmManipulator *mpr = mmap->mmap_context.highlight;
		if (mpr && mpr->type->cursor_get) {
			WM_cursor_set(win, mpr->type->cursor_get(mpr));
			return true;
		}
	}

	return false;
}

void wm_manipulatormap_highlight_set(
        wmManipulatorMap *mmap, const bContext *C, wmManipulator *mpr, int part)
{
	if ((mpr != mmap->mmap_context.highlight) ||
	    (mpr && part != mpr->highlight_part))
	{
		if (mmap->mmap_context.highlight) {
			mmap->mmap_context.highlight->state &= ~WM_MANIPULATOR_STATE_HIGHLIGHT;
			mmap->mmap_context.highlight->highlight_part = 0;
		}

		mmap->mmap_context.highlight = mpr;

		if (mpr) {
			mpr->state |= WM_MANIPULATOR_STATE_HIGHLIGHT;
			mpr->highlight_part = part;

			if (C && mpr->type->cursor_get) {
				wmWindow *win = CTX_wm_window(C);
				WM_cursor_set(win, mpr->type->cursor_get(mpr));
			}
		}
		else {
			if (C) {
				wmWindow *win = CTX_wm_window(C);
				WM_cursor_set(win, CURSOR_STD);
			}
		}

		/* tag the region for redraw */
		if (C) {
			ARegion *ar = CTX_wm_region(C);
			ED_region_tag_redraw(ar);
		}
	}
}

wmManipulator *wm_manipulatormap_highlight_get(wmManipulatorMap *mmap)
{
	return mmap->mmap_context.highlight;
}

void wm_manipulatormap_modal_set(
        wmManipulatorMap *mmap, bContext *C, const wmEvent *event, wmManipulator *mpr)
{
	if (mpr && C) {
		mpr->state |= WM_MANIPULATOR_STATE_MODAL;
		mmap->mmap_context.modal = mpr;

		if (mpr->op_data.type) {
			/* first activate the manipulator itself */
			if (mpr->type->invoke &&
			    (mpr->type->modal || mpr->custom_modal))
			{
				mpr->type->invoke(C, mpr, event);
			}

			WM_operator_name_call_ptr(C, mpr->op_data.type, WM_OP_INVOKE_DEFAULT, &mpr->op_data.ptr);

			/* we failed to hook the manipulator to the operator handler or operator was cancelled, return */
			if (!mmap->mmap_context.modal) {
				mpr->state &= ~WM_MANIPULATOR_STATE_MODAL;
				MEM_SAFE_FREE(mpr->interaction_data);
			}
			return;
		}
		else {
			if (mpr->type->invoke &&
			    (mpr->type->modal || mpr->custom_modal))
			{
				mpr->type->invoke(C, mpr, event);
			}
		}
		WM_cursor_grab_enable(CTX_wm_window(C), true, true, NULL);
	}
	else {
		mpr = mmap->mmap_context.modal;

		/* deactivate, manipulator but first take care of some stuff */
		if (mpr) {
			mpr->state &= ~WM_MANIPULATOR_STATE_MODAL;
			MEM_SAFE_FREE(mpr->interaction_data);
		}
		mmap->mmap_context.modal = NULL;

		if (C) {
			WM_cursor_grab_disable(CTX_wm_window(C), NULL);
			ED_region_tag_redraw(CTX_wm_region(C));
			WM_event_add_mousemove(C);
		}
	}
}

wmManipulator *wm_manipulatormap_modal_get(wmManipulatorMap *mmap)
{
	return mmap->mmap_context.modal;
}

wmManipulator **wm_manipulatormap_selected_get(wmManipulatorMap *mmap, int *r_selected_len)
{
	*r_selected_len = mmap->mmap_context.selected_len;
	return mmap->mmap_context.selected;
}

ListBase *wm_manipulatormap_groups_get(wmManipulatorMap *mmap)
{
	return &mmap->groups;
}

/** \} */ /* wmManipulatorMap */


/* -------------------------------------------------------------------- */
/** \name wmManipulatorMapType
 *
 * \{ */

wmManipulatorMapType *WM_manipulatormaptype_find(
        const struct wmManipulatorMapType_Params *mmap_params)
{
	for (wmManipulatorMapType *mmap_type = manipulatormaptypes.first; mmap_type; mmap_type = mmap_type->next) {
		if (mmap_type->spaceid == mmap_params->spaceid &&
		    mmap_type->regionid == mmap_params->regionid)
		{
			return mmap_type;
		}
	}

	return NULL;
}

wmManipulatorMapType *WM_manipulatormaptype_ensure(
        const struct wmManipulatorMapType_Params *mmap_params)
{
	wmManipulatorMapType *mmap_type = WM_manipulatormaptype_find(mmap_params);

	if (mmap_type) {
		return mmap_type;
	}

	mmap_type = MEM_callocN(sizeof(wmManipulatorMapType), "manipulatortype list");
	mmap_type->spaceid = mmap_params->spaceid;
	mmap_type->regionid = mmap_params->regionid;
	BLI_addhead(&manipulatormaptypes, mmap_type);

	return mmap_type;
}

void wm_manipulatormaptypes_free(void)
{
	for (wmManipulatorMapType *mmap_type = manipulatormaptypes.first, *mmap_type_next;
	     mmap_type;
	     mmap_type = mmap_type_next)
	{
		mmap_type_next = mmap_type->next;
		for (wmManipulatorGroupTypeRef *wgt_ref = mmap_type->grouptype_refs.first, *wgt_next;
		     wgt_ref;
		     wgt_ref = wgt_next)
		{
			wgt_next = wgt_ref->next;
			WM_manipulatormaptype_group_free(wgt_ref);
		}
		MEM_freeN(mmap_type);
	}
}

/**
 * Initialize keymaps for all existing manipulator-groups
 */
void wm_manipulators_keymap(wmKeyConfig *keyconf)
{
	/* we add this item-less keymap once and use it to group manipulator-group keymaps into it */
	WM_keymap_find(keyconf, "Manipulators", 0, 0);

	for (wmManipulatorMapType *mmap_type = manipulatormaptypes.first; mmap_type; mmap_type = mmap_type->next) {
		for (wmManipulatorGroupTypeRef *wgt_ref = mmap_type->grouptype_refs.first; wgt_ref; wgt_ref = wgt_ref->next) {
			wm_manipulatorgrouptype_setup_keymap(wgt_ref->type, keyconf);
		}
	}
}

/** \} */ /* wmManipulatorMapType */

/* -------------------------------------------------------------------- */
/** \name Updates for Dynamic Type Registraion
 *
 * \{ */


void WM_manipulatorconfig_update_tag_init(
        wmManipulatorMapType *mmap_type, wmManipulatorGroupType *wgt)
{
	/* tag for update on next use */
	mmap_type->type_update_flag |= (WM_MANIPULATORMAPTYPE_UPDATE_INIT | WM_MANIPULATORMAPTYPE_KEYMAP_INIT);
	wgt->type_update_flag |= (WM_MANIPULATORMAPTYPE_UPDATE_INIT | WM_MANIPULATORMAPTYPE_KEYMAP_INIT);

	wm_mmap_type_update_flag |= WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_INIT;
}

void WM_manipulatorconfig_update_tag_remove(
        wmManipulatorMapType *mmap_type, wmManipulatorGroupType *wgt)
{
	/* tag for update on next use */
	mmap_type->type_update_flag |= WM_MANIPULATORMAPTYPE_UPDATE_REMOVE;
	wgt->type_update_flag |= WM_MANIPULATORMAPTYPE_UPDATE_REMOVE;

	wm_mmap_type_update_flag |= WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_REMOVE;
}

/**
 * Run incase new types have been added (runs often, early exit where possible).
 * Follows #WM_keyconfig_update concentions.
 */
void WM_manipulatorconfig_update(struct Main *bmain)
{
	if (G.background)
		return;

	if (wm_mmap_type_update_flag == 0)
		return;

	if (wm_mmap_type_update_flag & WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_REMOVE) {
		for (wmManipulatorMapType *mmap_type = manipulatormaptypes.first;
		     mmap_type;
		     mmap_type = mmap_type->next)
		{
			if (mmap_type->type_update_flag & WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_REMOVE) {
				mmap_type->type_update_flag &= ~WM_MANIPULATORMAPTYPE_UPDATE_REMOVE;
				for (wmManipulatorGroupTypeRef *wgt_ref = mmap_type->grouptype_refs.first, *wgt_ref_next;
				     wgt_ref;
				     wgt_ref = wgt_ref_next)
				{
					wgt_ref_next = wgt_ref->next;
					WM_manipulatormaptype_group_unlink(NULL, bmain, mmap_type, wgt_ref->type);
				}
			}
		}

		wm_mmap_type_update_flag &= ~WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_REMOVE;
	}

	if (wm_mmap_type_update_flag & WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_INIT) {
		for (wmManipulatorMapType *mmap_type = manipulatormaptypes.first;
		     mmap_type;
		     mmap_type = mmap_type->next)
		{
			const uchar type_update_all = WM_MANIPULATORMAPTYPE_UPDATE_INIT | WM_MANIPULATORMAPTYPE_KEYMAP_INIT;
			if (mmap_type->type_update_flag & type_update_all) {
				mmap_type->type_update_flag &= ~type_update_all;
				for (wmManipulatorGroupTypeRef *wgt_ref = mmap_type->grouptype_refs.first;
				     wgt_ref;
				     wgt_ref = wgt_ref->next)
				{
					if (wgt_ref->type->type_update_flag & WM_MANIPULATORMAPTYPE_KEYMAP_INIT) {
						wgt_ref->type->type_update_flag &= ~WM_MANIPULATORMAPTYPE_KEYMAP_INIT;
						WM_manipulatormaptype_group_init_runtime_keymap(bmain, wgt_ref->type);
					}

					if (wgt_ref->type->type_update_flag & WM_MANIPULATORMAPTYPE_UPDATE_INIT) {
						wgt_ref->type->type_update_flag &= ~WM_MANIPULATORMAPTYPE_UPDATE_INIT;
						WM_manipulatormaptype_group_init_runtime(bmain, mmap_type, wgt_ref->type);
					}
				}
			}
		}

		wm_mmap_type_update_flag &= ~WM_MANIPULATORMAPTYPE_GLOBAL_UPDATE_INIT;
	}
}

/** \} */
