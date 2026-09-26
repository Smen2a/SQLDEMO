#include "tools/tools.h"

#include "tools/debris.h"

#include "body/materials.h"
#include "tools/smoothing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace sdf::tools {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;

Edit cut(const Primitive &prim, Blend blend = Blend::Hard, float r = 0.0f) {
	Edit e;
	e.prim = prim;
	e.op = Op::Subtract;
	e.blend = blend;
	e.r = e.r2 = r;
	return e;
}

Edit join(const Primitive &prim, std::uint16_t material, Blend blend = Blend::Hard, float r = 0.0f) {
	Edit e;
	e.prim = prim;
	e.op = Op::Union;
	e.blend = blend;
	e.r = e.r2 = r;
	e.material = material;
	return e;
}

// A frame whose axes are given (right-handed, orthonormal), for primitive rotations.
Frame axes(vec3 origin, vec3 x, vec3 y, vec3 z) {
	return {origin, x, y, z};
}

} // namespace

Frame Frame::at(vec3 origin, vec3 normal, vec3 along) {
	Frame f;
	f.origin = origin;
	f.z = gl::normalize(normal);
	vec3 x = along - f.z * gl::dot(along, f.z);
	if (gl::dot(x, x) < 1e-12f) {
		// No usable direction: any perpendicular will do.
		x = std::fabs(f.z.x) < 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0);
		x = x - f.z * gl::dot(x, f.z);
	}
	f.x = gl::normalize(x);
	f.y = gl::cross(f.z, f.x);
	return f;
}

vec4 Frame::rotation() const {
	// The rotation matrix has x, y, z as its columns.
	const float m00 = x.x, m01 = y.x, m02 = z.x;
	const float m10 = x.y, m11 = y.y, m12 = z.y;
	const float m20 = x.z, m21 = y.z, m22 = z.z;
	const float trace = m00 + m11 + m22;
	vec4 q;
	if (trace > 0.0f) {
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		q = vec4((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
	} else if (m00 > m11 && m00 > m22) {
		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		q = vec4(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
	} else if (m11 > m22) {
		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		q = vec4((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
	} else {
		const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
		q = vec4((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
	}
	return q / gl::length(q);
}

// --- chisel ---------------------------------------------------------------------------

Body Chisel::model() const {
	// The blade's own frame: u from the edge towards the handle, rising at the approach
	// angle; v its face normal, away from the work. Held bevel down: the flat back is the
	// upper face, through the edge (v = 0); the blade lies below it, and the bevel is ground
	// back from the edge underneath.
	const float a = approach_deg * kDegToRad;
	const vec3 u(-std::cos(a), 0.0f, std::sin(a));
	const vec3 v(std::sin(a), 0.0f, std::cos(a));
	const Frame blade = Frame::at(vec3(0.0f), v, u); // x = u, y = -Y, z = v
	const float ferrule = 12.0f, handle = 108.0f;

	Body b;
	b.base_material = mat::Steel;
	if (kind == gl::SDF_TOOL_FLAT) {
		b.base = Primitive::box(blade.point({blade_length * 0.5f, 0.0f, -thickness * 0.5f}),
				{blade_length * 0.5f, width * 0.5f, thickness * 0.5f}, 0.0f, blade.rotation());
		if (skew_deg > 0.0f) {
			// The edge angled across the blade (one corner leading): all ahead of it goes.
			const float sk = skew_deg * kDegToRad;
			const vec3 along_edge = blade.direction({std::sin(sk), std::cos(sk), 0.0f});
			const vec3 ahead = blade.direction({-std::cos(sk), std::sin(sk), 0.0f});
			const Frame tip = axes(ahead * 60.0f, ahead, along_edge, gl::cross(ahead, along_edge));
			b.add(cut(Primitive::box(tip.origin, {60.0f, 60.0f, 60.0f}, 0.0f, tip.rotation())));
		}
	} else {
		// A gouge's curved plate (or a V-tool's two): its inside is the edge's own section,
		// its outside that `thickness` further out; both run the blade's length.
		const vec3 p0 = blade.point({0.0f, 0.0f, 0.0f}), p1 = blade.point({blade_length, 0.0f, 0.0f});
		const float height = corner_depth() + thickness + 1.0f;
		ToolProfile outside;
		vec3 drop;
		if (kind == gl::SDF_TOOL_GOUGE) {
			outside = ToolProfile::gouge(sweep_radius + thickness, width + 2.0f * thickness, height);
			drop = v * thickness;
		} else {
			outside = ToolProfile::v_tool(v_angle_deg, height);
			drop = v * (thickness / std::sin(0.5f * v_angle_deg * kDegToRad));
		}
		b.base = Primitive::sweep(p0 - drop, p1 - drop, v, outside);
		b.add(cut(Primitive::sweep(p0 - u * 2.0f, p1 + u * 2.0f, v, profile(height + 5.0f))));
	}

	// The bevel: everything below a plane through the edge falling back at the bevel angle.
	const float bev = bevel_deg * kDegToRad;
	const vec3 along_bevel = blade.direction({std::cos(bev), 0.0f, -std::sin(bev)});
	const vec3 below_bevel = blade.direction({-std::sin(bev), 0.0f, -std::cos(bev)});
	const Frame wedge = axes(along_bevel * 20.0f + below_bevel * 20.0f, along_bevel, blade.y,
			gl::cross(along_bevel, blade.y));
	b.add(cut(Primitive::box(wedge.origin, {20.0f, width + 2.0f * thickness + 4.0f, 20.0f}, 0.0f, wedge.rotation())));

	// Cylinders run along their local y: frames with y along the blade.
	const Frame shaft = axes(vec3(0.0f), v, u, gl::cross(v, u));
	const vec3 axis_at = blade.point({0.0f, 0.0f, -thickness * 0.5f});
	const float ferrule_mid = blade_length + ferrule * 0.5f - 2.0f;
	b.add(join(Primitive::cylinder(axis_at + u * ferrule_mid, 5.5f, ferrule * 0.5f, 0.6f, shaft.rotation()), mat::Brass));
	const float handle_mid = blade_length + ferrule - 2.0f + handle * 0.5f;
	b.add(join(Primitive::cylinder(axis_at + u * handle_mid, 12.0f, handle * 0.5f, 5.0f, shaft.rotation()), mat::Ash,
			Blend::Round, 1.0f));
	b.grain_origin = axis_at + vec3(0.0f, 30.0f, 0.0f);
	b.grain_axis = u;
	return b;
}

ToolProfile Chisel::profile(float height) const {
	if (kind == gl::SDF_TOOL_GOUGE) {
		return ToolProfile::gouge(sweep_radius, width, height);
	}
	if (kind == gl::SDF_TOOL_V) {
		return ToolProfile::v_tool(v_angle_deg, height);
	}
	return ToolProfile::flat(width, height);
}

float Chisel::corner_depth() const {
	const float half = 0.5f * width;
	if (kind == gl::SDF_TOOL_GOUGE) {
		// A U (a veiner: half the width is the radius) has straight sides as tall again.
		return half >= sweep_radius * 0.999f ? 2.0f * sweep_radius
											 : sweep_radius - std::sqrt(sweep_radius * sweep_radius - half * half);
	}
	if (kind == gl::SDF_TOOL_V) {
		return half / std::tan(0.5f * v_angle_deg * kDegToRad);
	}
	return 0.0f;
}

float Chisel::chip_area(float depth) const {
	depth = std::max(depth, 0.0f);
	const float corners = corner_depth();
	const float within = std::min(depth, corners), beyond = std::max(depth - corners, 0.0f);
	if (kind == gl::SDF_TOOL_GOUGE) {
		const float r = sweep_radius, d = std::min(within, r);
		// A circular segment d deep, then straight sides (a U) up to its corners.
		const float segment = r * r * std::acos((r - d) / r) - (r - d) * std::sqrt(std::max(2.0f * r * d - d * d, 0.0f));
		return segment + std::min(width, 2.0f * r) * (within - d) + width * beyond;
	}
	if (kind == gl::SDF_TOOL_V) {
		return within * within * std::tan(0.5f * v_angle_deg * kDegToRad) + width * beyond;
	}
	return width * depth;
}

std::vector<Edit> Chisel::paring(vec3 start, vec3 end, vec3 normal, float depth) const {
	const vec3 n = gl::normalize(normal);
	vec3 d = end - start;
	d = d - n * gl::dot(d, n);
	const float length = gl::length(d);
	if (length < 1e-3f || depth <= 0.0f) {
		return {};
	}
	const vec3 t = d / length;
	const float slope = std::tan(approach_deg * kDegToRad);
	const float ramp = depth / slope;
	// The profile reaches from the cutting edge to well above the surface.
	const ToolProfile profile = this->profile(depth + 2.0f);
	const vec3 entry = start + n * 0.2f;
	if (length <= ramp) {
		return {cut(Primitive::sweep(entry, start + t * length - n * (length * slope), n, profile))};
	}
	const vec3 bottom = start + t * ramp - n * depth;
	return {cut(Primitive::sweep(entry, bottom, n, profile)),
			cut(Primitive::sweep(bottom, start + t * length - n * depth, n, profile))};
}

vec3 Chisel::edge(vec3 start, vec3 end, vec3 normal, float depth) const {
	const vec3 n = gl::normalize(normal);
	vec3 d = end - start;
	d = d - n * gl::dot(d, n);
	const float length = gl::length(d);
	if (length < 1e-3f) {
		return start;
	}
	const float slope = std::tan(approach_deg * kDegToRad);
	return start + d - n * std::min(depth, length * slope);
}

Edit Chisel::lift_out(vec3 end, vec3 direction, vec3 normal, float depth) const {
	const vec3 n = gl::normalize(normal);
	vec3 t = direction - n * gl::dot(direction, n);
	t = gl::dot(t, t) > 1e-12f ? gl::normalize(t) : Frame::at(end, n, vec3(1, 0, 0)).x;
	const float slope = std::tan(approach_deg * kDegToRad);
	const vec3 out = end + t * (depth / slope) + n * (depth + 0.2f);
	return cut(Primitive::sweep(end, out, n, profile(depth + 2.0f)));
}

// --- saw -------------------------------------------------------------------------------

Body Saw::model() const {
	const float half = blade_length * 0.5f;
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, blade_height * 0.5f}, {half, plate * 0.5f, blade_height * 0.5f});
	b.base_material = mat::Steel;
	// Teeth: V notches along the bottom edge.
	const vec4 turned = quat_axis_angle({0, 1, 0}, 3.14159265f / 4.0f);
	for (float x = -half + tooth_pitch * 0.5f; x < half; x += tooth_pitch) {
		b.add(cut(Primitive::box({x, 0.0f, 0.0f}, {tooth_pitch * 0.35f, plate * 2.0f, tooth_pitch * 0.35f}, 0.0f, turned)));
	}
	// A brass back stiffening the top edge, and a walnut handle with a hand hole.
	b.add(join(Primitive::box({-10.0f, 0.0f, blade_height + 3.0f}, {half - 10.0f, 1.6f, 3.5f}, 0.5f), mat::Brass));
	b.add(join(Primitive::box({half + 26.0f, 0.0f, blade_height - 5.0f}, {32.0f, 11.0f, 46.0f}, 8.0f), mat::Walnut,
			Blend::Round, 2.0f));
	b.add(cut(Primitive::box({half + 28.0f, 0.0f, blade_height - 2.0f}, {11.0f, 20.0f, 24.0f}, 10.0f), Blend::Round, 2.0f));
	b.grain_origin = {half + 26.0f, 30.0f, 0.0f};
	b.grain_axis = gl::normalize(vec3(0.3f, 0.0f, 1.0f));
	return b;
}

Edit Saw::kerf_cut(vec3 centre, vec3 along, vec3 normal, float depth) const {
	return kerf_slice(centre, along, normal, 0.0f, depth);
}

Edit Saw::kerf_slice(vec3 centre, vec3 along, vec3 normal, float from_depth, float to_depth) const {
	const Frame f = Frame::at(centre, normal, along);
	const float half = blade_length * 0.5f;
	// A slice reaches a millimetre up into the one above it (the seam between overlapping
	// cuts must not read as a surface: see ChiselStroke); the first, well above the work.
	const float height = from_depth > 1.0f ? to_depth - from_depth + 1.0f : to_depth + 5.0f;
	return cut(Primitive::sweep(centre - f.x * half - f.z * to_depth, centre + f.x * half - f.z * to_depth, f.z,
			ToolProfile::flat(kerf, height)));
}

// --- sanding block ---------------------------------------------------------------------

Body SandingBlock::model() const {
	const float paper = 0.8f, block = 25.0f;
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, paper * 0.5f}, {length * 0.5f, breadth * 0.5f, paper * 0.5f});
	b.base_material = mat::Abrasive;
	b.add(join(Primitive::box({0.0f, 0.0f, paper + block * 0.5f}, {length * 0.5f, breadth * 0.5f, block * 0.5f}, 3.0f),
			mat::Cork));
	return b;
}

float SandingBlock::removal_per_mm() const {
	return 0.6f * pressure / float(std::max(grit, 24));
}

Edit SandingBlock::pass(const Frame &plane, vec2 lo, vec2 hi, float depth) const {
	// A box over the footprint, cut with a smooth blend of radius r = feather: the rim then
	// rises smoothly into the old surface (no crease), and edges inside the footprint round
	// over, as sanding does. The blend also deepens the floor, uniformly where the old surface
	// is flat and parallel to it: by (r - s)^2 / 4r with s the box floor's depth, while s < r.
	// So the floor goes at s = 2 sqrt(r depth) - r (above the surface for depths under r / 4)
	// to come out `depth` deep.
	const float r = feather, above = 10.0f;
	const float floor = depth >= r ? depth : 2.0f * std::sqrt(r * depth) - r;
	const vec2 centre = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
	const Primitive slab = Primitive::box(plane.point({centre.x, centre.y, (above - floor) * 0.5f}),
			{half.x, half.y, (above + floor) * 0.5f}, 0.0f, plane.rotation());
	return cut(slab, Blend::Smooth, r);
}

// --- sanding sponge --------------------------------------------------------------------

Body SandingSponge::model() const {
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, thickness * 0.5f}, {length * 0.5f, breadth * 0.5f, thickness * 0.5f}, 7.0f);
	b.base_material = mat::Abrasive;
	return b;
}

float SandingSponge::rate() const {
	return 0.8f * pressure / float(std::max(grit, 24));
}

// --- strokes ---------------------------------------------------------------------------

namespace {

vec3 on_plane(vec3 v, vec3 n) {
	return v - n * gl::dot(v, n);
}

class ChiselStroke : public Stroke {
public:
	ChiselStroke(const Chisel &c, vec3 contact, vec3 normal, vec3 facing, float depth)
		: chisel_(c), start_(contact), n_(gl::normalize(normal)), depth_(depth) {
		dir_ = Frame::at(contact, n_, facing).x;
	}

	StrokeUpdate move_to(vec3 point) override {
		const vec3 d = on_plane(point - start_, n_);
		StrokeUpdate u;
		if (!moving_) {
			if (gl::length(d) < 2.0f) {
				return u;
			}
			// The push shows its direction: ramp in to full depth along it.
			moving_ = true;
			dir_ = gl::normalize(d);
			reached_ = depth_ / std::tan(chisel_.approach_deg * kDegToRad);
			run_from_ = ramp_end_ = reached_;
			u.edits = chisel_.paring(start_, start_ + dir_ * reached_, n_, depth_);
			return u;
		}
		// Then the flat run grows forward: its newest piece is re-cut as it lengthens, and
		// left behind once it is 10 mm long.
		const float along = gl::dot(d, dir_);
		if (along < reached_ + 0.5f) {
			return u;
		}
		reached_ = along;
		u.drop = open_ ? 1 : 0;
		// Each piece starts inside the one before (not the ramp): cuts ending flush would
		// leave a wall of zero thickness, and even overlapping ones leave a seam where the
		// field, only a bound inside the union, drops to half the overlap. A seam thinner
		// than the hit epsilon would show as a wall; 2 mm keeps it deeper than the floor.
		const float from = std::max(run_from_ - kOverlap, ramp_end_);
		u.edits.push_back(cut(Primitive::sweep(point_at(from), point_at(reached_), n_, chisel_.profile(depth_ + 2.0f))));
		open_ = reached_ - run_from_ < 10.0f;
		if (!open_) {
			run_from_ = reached_;
		}
		return u;
	}

	std::vector<Edit> edits() const override {
		if (!moving_) {
			return {};
		}
		return chisel_.paring(start_, start_ + dir_ * reached_, n_, depth_);
	}

	std::vector<Edit> finish() override {
		if (!moving_) {
			return {};
		}
		return {chisel_.lift_out(point_at(reached_), dir_, n_, depth_)};
	}

	Frame pose() const override { return Frame::at(moving_ ? point_at(reached_) : start_, n_, dir_); }

	static constexpr float kOverlap = 2.0f;

private:
	vec3 point_at(float along) const { return start_ + dir_ * along - n_ * depth_; }

	Chisel chisel_;
	vec3 start_, n_, dir_;
	float depth_;
	bool moving_ = false, open_ = false;
	float reached_ = 0.0f;  // how far along dir_ the edge has cut
	float run_from_ = 0.0f; // where the open piece of the flat run starts
	float ramp_end_ = 0.0f;
};

class SawStroke : public Stroke {
public:
	SawStroke(const Saw &s, vec3 contact, vec3 normal, vec3 along, float feed, float max_depth)
		: saw_(s), frame_(Frame::at(contact, normal, along)), feed_(feed), max_depth_(max_depth) {}

	StrokeUpdate move_to(vec3 point) override {
		const float s = gl::dot(point - frame_.origin, frame_.x);
		depth_ = std::min(depth_ + feed_ * std::fabs(s - position_), max_depth_);
		// The blade slides with the hand but stays in the board.
		const float reach = saw_.blade_length * 0.5f - 20.0f;
		position_ = std::clamp(s, -reach, reach);
		StrokeUpdate u;
		// The kerf's newest slice is re-cut as it deepens, and left behind at 1 mm.
		if (depth_ >= cut_ + 0.05f) {
			u.drop = open_ ? 1 : 0;
			u.edits.push_back(saw_.kerf_slice(frame_.origin, frame_.x, frame_.z, frozen_, depth_));
			cut_ = depth_;
			open_ = depth_ - frozen_ < 1.0f;
			if (!open_) {
				frozen_ = depth_;
			}
		}
		return u;
	}

	std::vector<Edit> edits() const override {
		if (cut_ <= 0.0f) {
			return {};
		}
		return {saw_.kerf_cut(frame_.origin, frame_.x, frame_.z, cut_)};
	}

	Frame pose() const override {
		Frame f = frame_;
		f.origin = frame_.point({position_, 0.0f, -cut_});
		return f;
	}

	// Through the work: the kerf's middle plane, which holds the saw's line and the normal.
	std::optional<Separation> separation() const override {
		if (max_depth_ >= 1e8f || cut_ < max_depth_ - 1e-3f) {
			return std::nullopt;
		}
		return Separation{Plane{frame_.origin, frame_.y}, saw_.kerf};
	}

	// Sawdust: each slice of kerf the saw goes down through is the kerf's width times the
	// material along the blade's line at that depth (the chord, sampled every millimetre and
	// kept per half millimetre of depth). It leaves the kerf at the chord's two ends.
	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		out.ended = out.ended || ended;
		while (dusted_ < cut_) {
			const float to = std::min(cut_, (std::floor(dusted_ / kChordStep) + 1.0f) * kChordStep);
			const Chord &c = chord(body, octree, 0.5f * (dusted_ + to));
			pending_ += (to - dusted_) * saw_.kerf * c.length;
			if (c.length > 0.0f) {
				ends_[0] = c.from;
				ends_[1] = c.to;
			}
			dusted_ = to;
		}
		if (pending_ < kLeastDust && !(ended && pending_ > 0.0f)) {
			return;
		}
		for (int e = 0; e < 2; ++e) {
			Dust d;
			d.point = frame_.point({ends_[e], 0.0f, 0.0f});
			d.direction = gl::normalize(frame_.x * (e == 0 ? -1.0f : 1.0f) + frame_.z * 0.5f);
			d.volume = 0.5f * pending_;
			d.grain = 0.5f;
			out.dust.push_back(d);
		}
		pending_ = 0.0f;
	}

private:
	struct Chord {
		float length = 0.0f, from = 0.0f, to = 0.0f; // mm along the blade's line
	};
	static constexpr float kChordStep = 0.5f;
	const Chord &chord(const Body &body, const Octree &octree, float depth) {
		const int key = int(depth / kChordStep);
		auto it = chords_.find(key);
		if (it != chords_.end()) {
			return it->second;
		}
		Chord c;
		const float half = saw_.blade_length * 0.5f;
		bool any = false;
		for (float x = -half + 0.5f; x < half; x += 1.0f) {
			if (octree.distance(body, frame_.point({x, 0.0f, -depth})) < 0.0f) {
				c.length += 1.0f;
				c.from = any ? c.from : x;
				c.to = x;
				any = true;
			}
		}
		return chords_.emplace(key, c).first->second;
	}

	Saw saw_;
	Frame frame_;
	float feed_, max_depth_;
	float depth_ = 0.0f, position_ = 0.0f;
	float cut_ = 0.0f;    // depth the kerf has been cut to
	float frozen_ = 0.0f; // depth where the open slice starts
	bool open_ = false;
	// Dust: the depth reported down to, what is held back, where the kerf's ends were.
	float dusted_ = 0.0f, pending_ = 0.0f;
	float ends_[2] = {0.0f, 0.0f};
	std::map<int, Chord> chords_;
};

class SandingStroke : public Stroke {
public:
	SandingStroke(const SandingBlock &b, vec3 contact, vec3 normal, vec3 along)
		: block_(b), plane_(Frame::at(contact, normal, along)) {
		cover({0.0f, 0.0f});
	}

	StrokeUpdate move_to(vec3 point) override {
		const vec3 d = point - plane_.origin;
		const vec2 at(gl::dot(d, plane_.x), gl::dot(d, plane_.y));
		travel_ += gl::length(at - at_);
		at_ = at;
		cover(at);
		const float depth = block_.removal_per_mm() * travel_;
		StrokeUpdate u;
		// Re-cut only once the pass has deepened (0.02 mm) or spread (2 mm) noticeably.
		if (depth >= cut_depth_ + 0.02f || lo_.x < cut_lo_.x - 2.0f || lo_.y < cut_lo_.y - 2.0f ||
				hi_.x > cut_hi_.x + 2.0f || hi_.y > cut_hi_.y + 2.0f) {
			u.drop = cut_ ? 1 : 0;
			u.edits.push_back(block_.pass(plane_, lo_, hi_, depth));
			cut_ = true;
			cut_depth_ = depth;
			cut_lo_ = lo_;
			cut_hi_ = hi_;
		}
		return u;
	}

	std::vector<Edit> edits() const override {
		if (!cut_) {
			return {};
		}
		return {block_.pass(plane_, cut_lo_, cut_hi_, cut_depth_)};
	}

	Frame pose() const override {
		Frame f = plane_;
		f.origin = plane_.point({at_.x, at_.y, -cut_depth_});
		return f;
	}

	// Fine dust: the pass takes its depth off the rectangle it has covered, as far as that
	// is over material (probed half way down); what that comes to beyond what was reported
	// leaves from under the block, over its face.
	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		out.ended = out.ended || ended;
		if (cut_) {
			const vec2 size = cut_hi_ - cut_lo_;
			const float fraction = material_fraction(body, octree, plane_, cut_lo_, cut_hi_,
					std::max(0.5f * cut_depth_, 0.005f), 7, 7);
			const float taken = cut_depth_ * size.x * size.y * fraction;
			pending_ += std::max(taken - dusted_, 0.0f);
			dusted_ = std::max(dusted_, taken);
		}
		if (pending_ < kLeastDust && !(ended && pending_ > 0.0f)) {
			return;
		}
		Dust d;
		d.point = plane_.point({at_.x, at_.y, 0.0f});
		d.direction = plane_.z;
		d.volume = pending_;
		d.grain = 0.25f;
		d.spread = 0.5f * std::min(block_.length, block_.breadth);
		out.dust.push_back(d);
		pending_ = 0.0f;
	}

private:
	void cover(vec2 at) {
		const vec2 half(block_.length * 0.5f, block_.breadth * 0.5f);
		lo_ = vec2(std::min(lo_.x, at.x - half.x), std::min(lo_.y, at.y - half.y));
		hi_ = vec2(std::max(hi_.x, at.x + half.x), std::max(hi_.y, at.y + half.y));
	}

	SandingBlock block_;
	Frame plane_;
	vec2 at_{0.0f, 0.0f}, lo_{1e9f, 1e9f}, hi_{-1e9f, -1e9f};
	vec2 cut_lo_{0.0f, 0.0f}, cut_hi_{0.0f, 0.0f};
	float travel_ = 0.0f, cut_depth_ = 0.0f;
	bool cut_ = false;
	float dusted_ = 0.0f, pending_ = 0.0f; // dust: reported so far (mm^3), held back
};

class HandSandingStroke : public Stroke {
public:
	HandSandingStroke(const SandingSponge &sponge, vec3 contact, vec3 normal, vec3 along, float spacing)
		: sponge_(sponge), plane_(Frame::at(contact, normal, along)), at_(contact), from_(contact), spacing_(spacing) {}

	StrokeUpdate move_to(vec3 point) override {
		at_ = point - plane_.z * gl::dot(point - plane_.origin, plane_.z);
		std::lock_guard<std::mutex> lock(mutex_);
		path_.push_back(at_);
		return {};
	}

	std::vector<Edit> edits() const override { return {}; }

	Frame pose() const override {
		Frame f = plane_;
		f.origin = at_;
		return f;
	}

	bool deferred() const override { return true; }

	// The flow's own measure of what it took out of the material, fine dust from where the
	// sponge bears. Reads nothing of the body (its work may be running on another thread).
	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		(void)body;
		(void)octree;
		out.ended = out.ended || ended;
		std::lock_guard<std::mutex> lock(mutex_);
		if (dust_ < kLeastDust && !(ended && dust_ > 0.0f)) {
			return;
		}
		Dust d;
		d.point = dust_centre_ / dust_;
		d.direction = plane_.z;
		d.volume = dust_;
		d.grain = 0.2f;
		d.spread = 0.5f * sponge_.reach;
		out.dust.push_back(d);
		dust_ = 0.0f;
		dust_centre_ = vec3(0.0f);
	}

	StrokeUpdate work(const Body &body, const Octree &octree) override {
		std::vector<vec3> path;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			path.swap(path_);
		}
		if (!grid_) {
			SmoothingGrid::Params params;
			params.spacing = spacing_;
			grid_ = std::make_unique<SmoothingGrid>(body, octree, params);
		}
		// Pressed along its path a quarter of its reach at a time (its pressure falls off
		// smoothly over the reach): each point it passes gets the flow its travel there earns.
		for (const vec3 &to : path) {
			const vec3 d = to - from_;
			const float length = gl::length(d);
			const int steps = std::max(1, int(std::ceil(length / (0.25f * sponge_.reach))));
			for (int k = 0; k < steps && length > 0.0f; ++k) {
				grid_->press(from_ + d * ((float(k) + 0.5f) / float(steps)), sponge_.reach,
						sponge_.rate() * length / float(steps));
			}
			from_ = to;
		}
		const Aabb changed = grid_->update();
		{
			// What the flow took out of the material this time: dust, reported by debris().
			const SmoothingGrid::Stats &last = grid_->last();
			std::lock_guard<std::mutex> lock(mutex_);
			dust_ += last.removed;
			dust_centre_ = dust_centre_ + last.removed_centre;
		}
		if (changed.empty()) {
			return {};
		}
		StrokeUpdate u;
		u.drop = layered_ ? 1 : 0;
		u.edits.push_back(Edit::smoothing(grid_->layer()));
		if (layered_) {
			u.changed = changed;
		}
		layered_ = true;
		return u;
	}

private:
	SandingSponge sponge_;
	Frame plane_;
	vec3 at_;      // where the sponge is (the thread moving it)
	vec3 from_;    // where its recorded path has been worked up to (the thread working)
	float spacing_;
	std::mutex mutex_;
	std::vector<vec3> path_; // recorded, not yet worked
	std::unique_ptr<SmoothingGrid> grid_;
	bool layered_ = false;   // whether the stroke has made its layer edit yet
	float dust_ = 0.0f;      // mm^3 the flow has taken out, not yet reported (under mutex_)
	vec3 dust_centre_{0.0f}; // its centre, weighted by volume
};

} // namespace

std::unique_ptr<Stroke> hand_sanding_stroke(const SandingSponge &sponge, vec3 contact, vec3 normal, vec3 along,
		float spacing) {
	return std::make_unique<HandSandingStroke>(sponge, contact, normal, along, spacing);
}

std::unique_ptr<Stroke> chisel_stroke(const Chisel &chisel, vec3 contact, vec3 normal, vec3 facing, float depth) {
	return std::make_unique<ChiselStroke>(chisel, contact, normal, facing, depth);
}

std::unique_ptr<Stroke> saw_stroke(const Saw &saw, vec3 contact, vec3 normal, vec3 along, float feed, float max_depth) {
	return std::make_unique<SawStroke>(saw, contact, normal, along, feed, max_depth);
}

std::unique_ptr<Stroke> sanding_stroke(const SandingBlock &block, vec3 contact, vec3 normal, vec3 along) {
	return std::make_unique<SandingStroke>(block, contact, normal, along);
}

} // namespace sdf::tools
