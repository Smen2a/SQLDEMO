#pragma once

#include "ops.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sdf {

class Layer;

using gl::vec2;
using gl::vec3;
using gl::vec4;

enum class Prim : int {
	Sphere = gl::SDF_PRIM_SPHERE,
	Box = gl::SDF_PRIM_BOX,
	Cylinder = gl::SDF_PRIM_CYLINDER,
	Capsule = gl::SDF_PRIM_CAPSULE,
	Plane = gl::SDF_PRIM_PLANE,
	SweepSegment = gl::SDF_PRIM_SWEEP_SEGMENT,
	SweepBezier = gl::SDF_PRIM_SWEEP_BEZIER,
};

enum class Blend : int {
	Hard = gl::SDF_BLEND_HARD,
	Chamfer = gl::SDF_BLEND_CHAMFER,
	Round = gl::SDF_BLEND_ROUND,
	Smooth = gl::SDF_BLEND_SMOOTH,
	SmoothC2 = gl::SDF_BLEND_SMOOTH_C2,
	Profile = gl::SDF_BLEND_PROFILE,
};

enum class EdgeProfile : int {
	ArcConcave = gl::SDF_PROFILE_ARC_CONCAVE,
	ArcConvex = gl::SDF_PROFILE_ARC_CONVEX,
	Ogee = gl::SDF_PROFILE_OGEE,
};

enum class Op : int {
	Union = gl::SDF_OP_UNION,
	Subtract = gl::SDF_OP_SUBTRACT,
	Intersect = gl::SDF_OP_INTERSECT,
	Engrave = gl::SDF_OP_ENGRAVE,
	Groove = gl::SDF_OP_GROOVE,
	Tongue = gl::SDF_OP_TONGUE,
	Paint = gl::SDF_OP_PAINT,
	Layer = gl::SDF_OP_LAYER, // a sampled smoothing layer (see body/layer.h)
};

struct Aabb {
	vec3 lo{gl::SDF_BIG, gl::SDF_BIG, gl::SDF_BIG};
	vec3 hi{-gl::SDF_BIG, -gl::SDF_BIG, -gl::SDF_BIG};

	static Aabb infinite() { return {vec3(-gl::SDF_BIG), vec3(gl::SDF_BIG)}; }
	bool empty() const { return lo.x > hi.x; }
	void include(vec3 p);
	void include(const Aabb &o);
	Aabb expanded(float r) const;
	bool overlaps(const Aabb &o) const;
	vec3 centre() const { return (lo + hi) * 0.5f; }
	vec3 size() const { return hi - lo; }
};

// A tool's cross-section. Dimensions in millimetres.
struct ToolProfile {
	int kind = gl::SDF_TOOL_FLAT;
	vec4 params;

	static ToolProfile flat(float width, float height);
	static ToolProfile v_tool(float included_angle_deg, float height);
	static ToolProfile gouge(float sweep_radius, float width, float height);
	// Radius of a circle (in the cross-section plane) containing the whole profile.
	float extent() const;
	// Largest lateral offset and the height of the cross-section.
	float half_width() const;
	float height() const;
};

struct Primitive {
	Prim type = Prim::Sphere;
	vec4 p[5];

	static Primitive sphere(vec3 centre, float radius);
	static Primitive box(vec3 centre, vec3 half_extents, float rounding = 0.0f, vec4 rotation = {0, 0, 0, 1});
	static Primitive cylinder(vec3 centre, float radius, float half_height, float rounding = 0.0f,
			vec4 rotation = {0, 0, 0, 1});
	static Primitive capsule(vec3 a, vec3 b, float radius);
	static Primitive plane(vec3 normal, float offset);
	static Primitive sweep(vec3 a, vec3 b, vec3 up, const ToolProfile &tool);
	static Primitive sweep(vec3 a, vec3 control, vec3 b, vec3 up, const ToolProfile &tool);

	float eval(vec3 q) const {
		return gl::sdf_primitive(q, int(type), p[0], p[1], p[2], p[3], p[4]);
	}
	Aabb bounds() const;
	// Size of the smallest detail this primitive produces (thinnest dimension, tool width).
	float feature_size() const;
	// Upper bound on |grad| of eval(). 1 for everything except curved sweeps, whose
	// rotating frame stretches distances by 1 / (1 - curvature * profile extent).
	float lipschitz() const;
};

struct Edit {
	Primitive prim;
	Op op = Op::Subtract;
	Blend blend = Blend::Hard;
	float r = 0.0f;
	float r2 = 0.0f;
	EdgeProfile shape = EdgeProfile::ArcConcave;
	std::uint16_t material = 0;
	// Op::Layer: the layer it applies (its primitive is then just the layer's box, for
	// culling). Shared, and immutable once built.
	std::shared_ptr<const Layer> layer;

	// Applies `layer` over the field before it.
	static Edit smoothing(std::shared_ptr<const Layer> layer);

	// Distance beyond the primitive's bounds within which this edit can change the field.
	float influence() const;
	// How far from the body's current surface this edit's result depends on the body's
	// actual distance values rather than just their signs (blend support, groove depth...).
	// Zero for hard unions, cuts and intersections.
	float value_reach() const;
	Aabb bounds() const;
	float lipschitz() const;
	// Smallest detail the edit produces: its primitive's, or a narrower blend.
	float feature_size() const;
};

// Result of evaluating a body: distance plus the two dominant materials and their mix.
struct Sample {
	float d = gl::SDF_BIG;
	float m0 = 0.0f, m1 = 0.0f, t = 0.0f;
};

// One part: an analytic base shape and an ordered list of edits applied to it.
// (Sampled base grids for forging and consolidation come later.)
class Body {
public:
	Primitive base;
	std::uint16_t base_material = 0;
	// Where this part sat in the log or block it was cut from: procedural grain, rings and
	// strata are evaluated relative to this line (a pith point and the grain axis).
	vec3 grain_origin{0, 0, 0};
	vec3 grain_axis{1, 0, 0};

	// Upper bound on an acceptable edit's Lipschitz constant. A curved stroke that bends
	// tighter than about twice its tool's half-width is not a cut a real tool can make, and
	// its distance bound degrades fast enough to cripple pruning and tracing around it.
	static constexpr float kMaxEditLipschitz = 2.0f;
	static bool accepts(const Edit &e) { return e.prim.lipschitz() <= kMaxEditLipschitz; }

	// Appends the edit, or returns false and leaves the body unchanged if accepts() fails.
	bool add(const Edit &e);
	void replace(std::size_t i, const Edit &e);
	void pop();
	const std::vector<Edit> &edits() const { return edits_; }
	// Bounds of edit i's primitive (infinite for Intersect) and how far beyond them the
	// edit can reach.
	const Aabb &edit_box(std::size_t i) const { return culls_[i].box; }
	float edit_influence(std::size_t i) const { return culls_[i].influence; }

	// Skips edits that provably cannot matter at p: those whose primitive's bounding box is
	// at least |d| + influence away. For every operator that leaves the value unchanged
	// when the primitive's distance is exact, and never changes the sign.
	Sample sample(vec3 p) const;
	// Evaluates every edit; the reference sample() is tested against.
	Sample sample_exhaustive(vec3 p) const;
	float distance(vec3 p) const { return sample(p).d; }
	// Tetrahedral finite-difference normal with step h.
	vec3 normal(vec3 p, float h) const;
	Aabb bounds() const;
	float lipschitz() const;

private:
	struct Cull {
		Aabb box;        // primitive bounds, not expanded
		float influence; // blend / operator reach beyond them
	};
	std::vector<Edit> edits_;
	std::vector<Cull> culls_;
};

// True when, over the given operand intervals, the edit's blend provably reduces to the
// hard min (in union form: a = body so far, b = primitive).
bool blend_inactive(const Edit &e, float a_lo, float a_hi, float b_lo, float b_hi);

// Unit quaternion rotating by `angle` radians about unit `axis`.
vec4 quat_axis_angle(vec3 axis, float angle);

} // namespace sdf
