#include "tools/tools.h"

#include "body/materials.h"

#include <algorithm>
#include <cmath>

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
	// angle; v is the blade's face normal. The flat back lies at v = 0, the bevel on top.
	const float a = approach_deg * kDegToRad;
	const vec3 u(-std::cos(a), 0.0f, std::sin(a));
	const vec3 v(std::sin(a), 0.0f, std::cos(a));
	const Frame blade = Frame::at(vec3(0.0f), v, u); // x = u, y = -Y, z = v
	const float blade_length = 85.0f, ferrule = 12.0f, handle = 108.0f;

	Body b;
	b.base = Primitive::box(blade.point({blade_length * 0.5f, 0.0f, thickness * 0.5f}),
			{blade_length * 0.5f, width * 0.5f, thickness * 0.5f}, 0.0f, blade.rotation());
	b.base_material = mat::Steel;

	// The bevel: everything above a plane through the edge rising at the bevel angle.
	const float bev = bevel_deg * kDegToRad;
	const vec3 along_bevel = blade.direction({std::cos(bev), 0.0f, std::sin(bev)});
	const vec3 above_bevel = blade.direction({-std::sin(bev), 0.0f, std::cos(bev)});
	const Frame wedge = axes(blade.point({0, 0, 0}) + along_bevel * 8.0f + above_bevel * 10.0f, along_bevel, blade.y, above_bevel);
	b.add(cut(Primitive::box(wedge.origin, {20.0f, width, 10.0f}, 0.0f, wedge.rotation())));

	// Cylinders run along their local y: frames with y along the blade.
	const Frame shaft = axes(vec3(0.0f), v, u, gl::cross(v, u));
	const vec3 axis_at = blade.point({0.0f, 0.0f, thickness * 0.5f});
	const float ferrule_mid = blade_length + ferrule * 0.5f - 2.0f;
	b.add(join(Primitive::cylinder(axis_at + u * ferrule_mid, 5.5f, ferrule * 0.5f, 0.6f, shaft.rotation()), mat::Brass));
	const float handle_mid = blade_length + ferrule - 2.0f + handle * 0.5f;
	b.add(join(Primitive::cylinder(axis_at + u * handle_mid, 12.0f, handle * 0.5f, 5.0f, shaft.rotation()), mat::Ash,
			Blend::Round, 1.0f));
	b.grain_origin = axis_at + vec3(0.0f, 30.0f, 0.0f);
	b.grain_axis = u;
	return b;
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
	const ToolProfile profile = ToolProfile::flat(width, depth + 2.0f);
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
	return cut(Primitive::sweep(end, out, n, ToolProfile::flat(width, depth + 2.0f)));
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
	// A slice overlaps the one above it a little; the first reaches well above the surface.
	const float height = from_depth > 0.0f ? to_depth - from_depth + 0.05f : to_depth + 5.0f;
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
	return 0.6f / float(std::max(grit, 24));
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
			u.edits = chisel_.paring(start_, start_ + dir_ * reached_, n_, depth_);
			return u;
		}
		// Then the flat run grows forward in steps of at least a millimetre.
		const float along = gl::dot(d, dir_);
		if (along >= reached_ + 1.0f) {
			u.edits.push_back(cut(Primitive::sweep(edge(), start_ + dir_ * along - n_ * depth_, n_,
					ToolProfile::flat(chisel_.width, depth_ + 2.0f))));
			reached_ = along;
		}
		return u;
	}

	std::vector<Edit> finish() override {
		if (!moving_) {
			return {};
		}
		return {chisel_.lift_out(edge(), dir_, n_, depth_)};
	}

	Frame pose() const override { return Frame::at(moving_ ? edge() : start_, n_, dir_); }

private:
	vec3 edge() const { return start_ + dir_ * reached_ - n_ * depth_; }

	Chisel chisel_;
	vec3 start_, n_, dir_;
	float depth_;
	bool moving_ = false;
	float reached_ = 0.0f; // how far along dir_ the edge has cut
};

class SawStroke : public Stroke {
public:
	SawStroke(const Saw &s, vec3 contact, vec3 normal, vec3 along, float feed)
		: saw_(s), frame_(Frame::at(contact, normal, along)), feed_(feed) {}

	StrokeUpdate move_to(vec3 point) override {
		const float s = gl::dot(point - frame_.origin, frame_.x);
		depth_ += feed_ * std::fabs(s - position_);
		// The blade slides with the hand but stays in the board.
		const float reach = saw_.blade_length * 0.5f - 20.0f;
		position_ = std::clamp(s, -reach, reach);
		StrokeUpdate u;
		if (depth_ >= sliced_ + 0.25f) {
			u.edits.push_back(saw_.kerf_slice(frame_.origin, frame_.x, frame_.z, sliced_, depth_));
			sliced_ = depth_;
		}
		return u;
	}

	std::vector<Edit> finish() override {
		if (depth_ > sliced_ + 1e-3f) {
			return {saw_.kerf_slice(frame_.origin, frame_.x, frame_.z, sliced_, depth_)};
		}
		return {};
	}

	Frame pose() const override {
		Frame f = frame_;
		f.origin = frame_.point({position_, 0.0f, -sliced_});
		return f;
	}

private:
	Saw saw_;
	Frame frame_;
	float feed_;
	float depth_ = 0.0f, sliced_ = 0.0f, position_ = 0.0f;
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
			u.replace = true;
			u.edits.push_back(block_.pass(plane_, lo_, hi_, depth));
			cut_depth_ = depth;
			cut_lo_ = lo_;
			cut_hi_ = hi_;
		}
		return u;
	}

	std::vector<Edit> finish() override { return {}; }

	Frame pose() const override {
		Frame f = plane_;
		f.origin = plane_.point({at_.x, at_.y, -cut_depth_});
		return f;
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
};

} // namespace

std::unique_ptr<Stroke> chisel_stroke(const Chisel &chisel, vec3 contact, vec3 normal, vec3 facing, float depth) {
	return std::make_unique<ChiselStroke>(chisel, contact, normal, facing, depth);
}

std::unique_ptr<Stroke> saw_stroke(const Saw &saw, vec3 contact, vec3 normal, vec3 along, float feed) {
	return std::make_unique<SawStroke>(saw, contact, normal, along, feed);
}

std::unique_ptr<Stroke> sanding_stroke(const SandingBlock &block, vec3 contact, vec3 normal, vec3 along) {
	return std::make_unique<SandingStroke>(block, contact, normal, along);
}

} // namespace sdf::tools
