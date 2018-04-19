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
* The Original Code is Copyright (C) 2008 Blender Foundation.
* All rights reserved.
*
* Contributor(s): Blender Foundation
*
* ***** END GPL LICENSE BLOCK *****
*/

#ifndef __BC_ANIMATION_CURVE_H__
#define __BC_ANIMATION_CURVE_H__

#include "collada_utils.h"
#include "BCSampleData.h"

extern "C"
{
#include "MEM_guardedalloc.h"
#include "BKE_fcurve.h"
#include "BKE_armature.h"
#include "BKE_material.h"
#include "ED_anim_api.h"
#include "ED_keyframing.h"
#include "ED_keyframes_edit.h"
}

typedef float(TangentPoint)[2];

typedef std::set<float> BCFrameSet;
typedef std::vector<float> BCFrames;
typedef std::vector<float> BCValues;
typedef std::vector<float> BCTimes;

typedef enum BC_animation_type {
	BC_ANIMATION_TYPE_OBJECT,
	BC_ANIMATION_TYPE_BONE,
	BC_ANIMATION_TYPE_CAMERA,
	BC_ANIMATION_TYPE_MATERIAL,
	BC_ANIMATION_TYPE_LIGHT
} BC_animation_type;

class BCCurveKey {
private:
	BC_animation_type key_type;
	std::string rna_path;
	int curve_array_index;
	int curve_subindex; /* only needed for materials */

public:

	BCCurveKey();
	BCCurveKey(const BC_animation_type type, const std::string path, const int array_index, const int subindex = -1);
	void operator=(const BCCurveKey &other);
	const std::string get_full_path() const;
	const std::string get_path() const;
	const int get_array_index() const;
	const int get_subindex() const;
	void set_object_type(BC_animation_type object_type);
	const BC_animation_type get_animation_type() const;
	const bool operator<(const BCCurveKey &other) const;

};

class BCBezTriple {
public:
	BezTriple & bezt;

	BCBezTriple(BezTriple bezt);
	const float get_frame() const;
	const float get_time(Scene *scene) const;
	const float get_value() const;
	const float get_angle() const;
	void get_in_tangent(Scene *scene, float point[2], bool as_angle) const;
	void get_out_tangent(Scene *scene, float point[2], bool as_angle) const;
	void get_tangent(Scene *scene, float point[2], bool as_angle, int index) const;

};

class BCKeyPoint {
private:
	float inTangent[2];
	float val;
	int frame;
	float outTangent[2];
	bool hasHandles;

public:

	BCKeyPoint()
	{
		val = 0;
		frame = 0;
		inTangent[0] = inTangent[1] = 0;
		outTangent[0] = outTangent[1] = 0;
		hasHandles = false;
	}

	BCKeyPoint(const float value, const int frame_index)
	{
		frame = frame_index;
		val = value;
		hasHandles = false;
	}

	BCKeyPoint(const BezTriple &bezt)
	{
		frame = bezt.vec[1][0];
		val = bezt.vec[1][1];

		inTangent[0] = bezt.vec[0][0];
		inTangent[1] = bezt.vec[0][1];
		outTangent[0] = bezt.vec[2][0];
		outTangent[1] = bezt.vec[2][1];
		hasHandles = true;
	}

	const float(&get_in_tangent() const)[2]
	{
		return inTangent;
	}

	const float(&get_out_tangent()const )[2]
	{
		return outTangent;
	}

	const float get_value() const
	{
		return val;
	}

	const float get_frame() const
	{
		return frame;
	}

	const bool has_handles() const
	{
		return hasHandles;
	}

};

typedef std::map<int, BCKeyPoint> BCValueMap;

class BCAnimationCurve {
private:
	BCCurveKey curve_key;
	BCValueMap samples;
	float min = 0;
	float max = 0;

	bool curve_is_local_copy = false;
	FCurve *fcurve;
	PointerRNA id_ptr;
	void init_pointer_rna(Object *ob);
	void delete_fcurve(FCurve *fcu);
	FCurve *create_fcurve(int array_index, const char *rna_path);
	void create_bezt(float frame, float output);
	void update_range(float val);
	void init_range(float val);

public:
	BCAnimationCurve();
	BCAnimationCurve(const BCAnimationCurve &other);
	BCAnimationCurve(const BCCurveKey &key, Object *ob);
	BCAnimationCurve(BCCurveKey key, Object *ob, FCurve *fcu);
	~BCAnimationCurve();

	const bool is_of_animation_type(BC_animation_type type) const;
	const int get_interpolation_type(float sample_frame) const;
	bool is_animated();
	const bool is_transform_curve() const;
	const bool is_rotation_curve() const;
	bool is_keyframe(int frame);
	void adjust_range(int frame);

	const std::string get_animation_name(Object *ob) const; /* xxx: this is collada specific */
	const std::string get_channel_target() const;
	const int get_channel_index() const;
	const int get_subindex() const;
	const std::string get_rna_path() const;
	FCurve *get_fcurve() const;
	const int sample_count() const;

	BCValueMap &get_value_map();
	const float get_value(const float frame);

	void get_key_values(BCValues &values) const;
	void get_sampled_values(BCValues &values) const;
	void get_key_frames(BCFrames &frames) const;
	void get_sampled_frames(BCFrames &frames) const;

	/* Curve edit functions create a copy of the underlaying FCurve */
	FCurve *get_edit_fcurve();
	bool add_value_from_rna(const int frame);
	bool add_value_from_matrix(const BCSample &sample, const int frame);
	void add_value(const float val, const int frame);
	void clean_handles();

	/* experimental stuff */
	const int closest_index_above(const float sample_frame, const int start_at) const;
	const int closest_index_below(const float sample_frame) const;

};

typedef std::map<BCCurveKey, BCAnimationCurve *> BCAnimationCurveMap;

#endif
