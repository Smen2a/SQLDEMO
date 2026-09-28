#include "plans/part.h"

#include "pieces/pieces.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sdf::plans {

vec3 outward(Face f) {
	switch (f) {
		case Face::Side:
			return {0, 0, -1};
		case Face::Back:
			return {0, 0, 1};
		case Face::Edge:
			return {0, -1, 0};
		case Face::OtherEdge:
			return {0, 1, 0};
		case Face::End:
			return {-1, 0, 0};
		case Face::FarEnd:
			return {1, 0, 0};
	}
	return {0, 0, -1};
}

int axis_of(Face f) {
	return f == Face::Side || f == Face::Back ? 2 : f == Face::Edge || f == Face::OtherEdge ? 1 : 0;
}

float position(Face f, vec3 size) {
	return f == Face::Back ? size.z : f == Face::OtherEdge ? size.y : f == Face::FarEnd ? size.x : 0.0f;
}

namespace {

constexpr float kSame = 0.05f; // mm: a stock face this near the part's is the part's face
constexpr float kPast = 1.0f;  // mm a cut runs past the blank's faces

float get(vec3 v, int axis) {
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

void set(vec3 &v, int axis, float value) {
	(axis == 0 ? v.x : axis == 1 ? v.y : v.z) = value;
}

vec3 unit(int axis, float sign = 1.0f) {
	vec3 v(0.0f);
	set(v, axis, sign);
	return v;
}

Face opposite(Face f) {
	switch (f) {
		case Face::Side:
			return Face::Back;
		case Face::Back:
			return Face::Side;
		case Face::Edge:
			return Face::OtherEdge;
		case Face::OtherEdge:
			return Face::Edge;
		case Face::End:
			return Face::FarEnd;
		case Face::FarEnd:
			return Face::End;
	}
	return f;
}

// A feature's x range: its own, or the whole length.
vec2 span(const Feature &f, float length) {
	return f.along.y > f.along.x ? f.along : vec2(0.0f, length);
}

struct Layout {
	const Part &part;
	vec3 stock;
	PartLines out;

	// Whether the part's face is one of the stock's: the reference faces always are; the
	// others where the stock is the part's size that way.
	bool has(Face f) const {
		switch (f) {
			case Face::Back:
				return std::fabs(stock.z - part.size.z) < kSame;
			case Face::OtherEdge:
				return std::fabs(stock.y - part.size.y) < kSame;
			case Face::FarEnd:
				return std::fabs(stock.x - part.size.x) < kSame;
			default:
				return true;
		}
	}

	void add(vec3 face, vec3 a, vec3 b, PlanLine::As as, vec3 toward, float distance, const std::string &feature) {
		const float length = gl::length(b - a);
		if (length < 1e-3f) {
			return;
		}
		PlanLine l;
		l.as = as;
		l.face = face;
		l.origin = a;
		l.dir = (b - a) / length;
		l.length = length;
		// (In the face, square to the line: a slope's is turned off the waste's way to be.)
		const vec3 square = toward - l.dir * gl::dot(l.dir, toward);
		l.toward = gl::length(square) > 1e-6f ? gl::normalize(square) : toward;
		l.distance = distance;
		l.feature = feature;
		out.lines.push_back(l);
	}

	// On the part's face `f`, if the stock has it (else held back for later).
	void line(Face f, vec3 a, vec3 b, PlanLine::As as, vec3 toward, float distance, const std::string &feature) {
		if (!has(f)) {
			++out.later;
			return;
		}
		add(outward(f), a, b, as, toward, distance, feature);
	}

	// The point on the edge between faces `f` and `g` (two long faces) at x.
	vec3 edge(Face f, Face g, float x) const {
		vec3 p(x, 0.0f, 0.0f);
		set(p, axis_of(f), position(f, part.size));
		set(p, axis_of(g), position(g, part.size));
		return p;
	}

	// The part's size on the stock: knifed to length round it, gauged to width and to
	// thickness from its far faces (the waste beyond).
	void size() {
		const vec3 s = part.size, k = stock;
		const auto knife = PlanLine::As::Knife;
		const auto gauge = PlanLine::As::Gauge;
		if (k.x > s.x + kSame) {
			const float x = s.x;
			add(unit(2, -1), {x, 0, 0}, {x, k.y, 0}, knife, unit(0), 0.0f, "");
			add(unit(2), {x, 0, k.z}, {x, k.y, k.z}, knife, unit(0), 0.0f, "");
			add(unit(1, -1), {x, 0, 0}, {x, 0, k.z}, knife, unit(0), 0.0f, "");
			add(unit(1), {x, k.y, 0}, {x, k.y, k.z}, knife, unit(0), 0.0f, "");
		}
		if (k.y > s.y + kSame) {
			const float y = s.y, d = k.y - s.y;
			add(unit(2, -1), {0, y, 0}, {k.x, y, 0}, gauge, unit(1), d, "");
			add(unit(2), {0, y, k.z}, {k.x, y, k.z}, gauge, unit(1), d, "");
			add(unit(0, -1), {0, y, 0}, {0, y, k.z}, gauge, unit(1), d, "");
			add(unit(0), {k.x, y, 0}, {k.x, y, k.z}, gauge, unit(1), d, "");
		}
		if (k.z > s.z + kSame) {
			const float z = s.z, d = k.z - s.z;
			add(unit(1, -1), {0, 0, z}, {k.x, 0, z}, gauge, unit(2), d, "");
			add(unit(1), {0, k.y, z}, {k.x, k.y, z}, gauge, unit(2), d, "");
			add(unit(0, -1), {0, 0, z}, {0, k.y, z}, gauge, unit(2), d, "");
			add(unit(0), {k.x, 0, z}, {k.x, k.y, z}, gauge, unit(2), d, "");
		}
	}

	void hole(const Feature &f) {
		const vec2 x = span(f, part.size.x);
		std::vector<Face> faces{f.face};
		if (f.through) {
			faces.push_back(opposite(f.face));
		}
		for (const Face g : faces) {
			if (axis_of(g) == 0) {
				continue; // (not into an end)
			}
			const int across = axis_of(g) == 2 ? 1 : 2; // y on the side and back, z on the edges
			const float at = position(g, part.size);
			auto p = [&](float px, float c) {
				vec3 q(px, 0.0f, 0.0f);
				set(q, axis_of(g), at);
				set(q, across, c);
				return q;
			};
			const auto knife = PlanLine::As::Knife;
			line(g, p(x.x, f.across.x), p(x.y, f.across.x), knife, unit(across), 0.0f, f.name);
			line(g, p(x.x, f.across.y), p(x.y, f.across.y), knife, unit(across), 0.0f, f.name);
			line(g, p(x.x, f.across.x), p(x.x, f.across.y), knife, unit(0), 0.0f, f.name);
			line(g, p(x.y, f.across.x), p(x.y, f.across.y), knife, unit(0), 0.0f, f.name);
		}
	}

	// Four rebates round an end: each cheek's shoulder gauged across its face from the end,
	// and its depth gauged from that face on the faces beside and on the end.
	void tenon(const Feature &f) {
		const vec3 s = part.size;
		const float xs = f.far ? s.x - f.length : f.length; // the shoulders
		const float xa = f.far ? s.x - f.length : 0.0f, xb = f.far ? s.x : f.length;
		const float xe = f.far ? s.x : 0.0f; // the end
		const Face end = f.far ? Face::FarEnd : Face::End;
		const vec3 to_end = unit(0, f.far ? 1.0f : -1.0f);
		const auto gauge = PlanLine::As::Gauge;
		if (f.z.x > 0.0f) { // a cheek off the face side
			line(Face::Side, {xs, 0, 0}, {xs, s.y, 0}, gauge, to_end, f.length, f.name);
			line(Face::Edge, {xa, 0, f.z.x}, {xb, 0, f.z.x}, gauge, unit(2, -1), f.z.x, f.name);
			line(Face::OtherEdge, {xa, s.y, f.z.x}, {xb, s.y, f.z.x}, gauge, unit(2, -1), f.z.x, f.name);
			line(end, {xe, 0, f.z.x}, {xe, s.y, f.z.x}, gauge, unit(2, -1), f.z.x, f.name);
		}
		if (f.z.y < s.z) { // off the back
			const float d = s.z - f.z.y;
			line(Face::Back, {xs, 0, s.z}, {xs, s.y, s.z}, gauge, to_end, f.length, f.name);
			line(Face::Edge, {xa, 0, f.z.y}, {xb, 0, f.z.y}, gauge, unit(2), d, f.name);
			line(Face::OtherEdge, {xa, s.y, f.z.y}, {xb, s.y, f.z.y}, gauge, unit(2), d, f.name);
			line(end, {xe, 0, f.z.y}, {xe, s.y, f.z.y}, gauge, unit(2), d, f.name);
		}
		if (f.y.x > 0.0f) { // off the face edge
			line(Face::Edge, {xs, 0, 0}, {xs, 0, s.z}, gauge, to_end, f.length, f.name);
			line(Face::Side, {xa, f.y.x, 0}, {xb, f.y.x, 0}, gauge, unit(1, -1), f.y.x, f.name);
			line(Face::Back, {xa, f.y.x, s.z}, {xb, f.y.x, s.z}, gauge, unit(1, -1), f.y.x, f.name);
			line(end, {xe, f.y.x, 0}, {xe, f.y.x, s.z}, gauge, unit(1, -1), f.y.x, f.name);
		}
		if (f.y.y < s.y) { // off the other edge
			const float d = s.y - f.y.y;
			line(Face::OtherEdge, {xs, s.y, 0}, {xs, s.y, s.z}, gauge, to_end, f.length, f.name);
			line(Face::Side, {xa, f.y.y, 0}, {xb, f.y.y, 0}, gauge, unit(1), d, f.name);
			line(Face::Back, {xa, f.y.y, s.z}, {xb, f.y.y, s.z}, gauge, unit(1), d, f.name);
			line(end, {xe, f.y.y, 0}, {xe, f.y.y, s.z}, gauge, unit(1), d, f.name);
		}
	}

	// A saw kerf down from an end: knifed across the end.
	void kerf(const Feature &f) {
		const vec3 s = part.size;
		const float xe = f.far ? s.x : 0.0f;
		const Face end = f.far ? Face::FarEnd : Face::End;
		if (f.axis == 1) {
			line(end, {xe, f.at, 0}, {xe, f.at, s.z}, PlanLine::As::Knife, unit(1), 0.0f, f.name);
		} else {
			line(end, {xe, 0, f.at}, {xe, s.y, f.at}, PlanLine::As::Knife, unit(2), 0.0f, f.name);
		}
	}

	// A rebate (or, with both widths the same, a chamfer) along the edge between `face` and
	// `other`: gauged `wide` on the face and `deep` on the other, from the edge between
	// them; knifed across both where it stops short of the ends.
	void along_edge(const Feature &f, float wide, float deep) {
		if (axis_of(f.face) == 0 || axis_of(f.other) == 0 || axis_of(f.face) == axis_of(f.other)) {
			return; // (an edge along the length only)
		}
		const vec2 x = span(f, part.size.x);
		const vec3 nf = outward(f.face), ng = outward(f.other);
		const auto gauge = PlanLine::As::Gauge;
		line(f.face, edge(f.face, f.other, x.x) - ng * wide, edge(f.face, f.other, x.y) - ng * wide, gauge, ng, wide,
				f.name);
		line(f.other, edge(f.face, f.other, x.x) - nf * deep, edge(f.face, f.other, x.y) - nf * deep, gauge, nf, deep,
				f.name);
		for (const float at : {x.x, x.y}) {
			if (at > kSame && at < part.size.x - kSame) {
				const vec3 e = edge(f.face, f.other, at);
				line(f.face, e, e - ng * wide, PlanLine::As::Knife, unit(0), 0.0f, f.name);
				line(f.other, e, e - nf * deep, PlanLine::As::Knife, unit(0), 0.0f, f.name);
			}
		}
	}

	// A taper off the side or the back: its slope on both edges (pencil only), gauged on
	// the ends where it leaves them short of the full thickness.
	void taper(const Feature &f) {
		const vec3 s = part.size;
		const bool back = f.face == Face::Back;
		auto z = [&](float t) { return back ? t : s.z - t; }; // where the slope meets that thickness
		const vec3 waste = unit(2, back ? 1.0f : -1.0f);
		const auto guide = PlanLine::As::Guide;
		line(Face::Edge, {0, 0, z(f.from)}, {s.x, 0, z(f.to)}, guide, waste, 0.0f, f.name);
		line(Face::OtherEdge, {0, s.y, z(f.from)}, {s.x, s.y, z(f.to)}, guide, waste, 0.0f, f.name);
		if (f.from < s.z - kSame) {
			line(Face::End, {0, 0, z(f.from)}, {0, s.y, z(f.from)}, PlanLine::As::Gauge, waste, s.z - f.from, f.name);
		}
		if (f.to < s.z - kSame) {
			line(Face::FarEnd, {s.x, 0, z(f.to)}, {s.x, s.y, z(f.to)}, PlanLine::As::Gauge, waste, s.z - f.to, f.name);
		}
	}
};

} // namespace

PartLines part_lines(const Part &part, vec3 stock) {
	Layout l{part, gl::max(stock, part.size), {}};
	l.size();
	for (const Feature &f : part.features) {
		switch (f.kind) {
			case Feature::Kind::Hole:
				l.hole(f);
				break;
			case Feature::Kind::Tenon:
				l.tenon(f);
				break;
			case Feature::Kind::Kerf:
				l.kerf(f);
				break;
			case Feature::Kind::Rebate:
				l.along_edge(f, f.width, f.depth);
				break;
			case Feature::Kind::Chamfer:
				l.along_edge(f, f.width, f.width);
				break;
			case Feature::Kind::Taper:
				l.taper(f);
				break;
		}
	}
	return std::move(l.out);
}

Body part_solid(const Part &part, std::uint16_t material) {
	const vec3 s = part.size;
	// The blank, as demo::stock makes it (flat-sawn, eased by a millimetre), placed at its
	// middle.
	const float ease = std::min(1.0f, 0.2f * std::min(s.x, std::min(s.y, s.z)));
	Body b;
	b.base = Primitive::box(s * 0.5f, s * 0.5f, ease);
	b.base_material = material;
	b.grain_origin = s * 0.5f + vec3(0.0f, 0.1f * s.y, -(0.5f * s.z + 47.5f));
	b.grain_axis = gl::normalize(vec3(1, 0.04f, 0.08f));
	auto cut = [&](vec3 lo, vec3 hi) {
		Edit e;
		e.prim = Primitive::box((lo + hi) * 0.5f, (hi - lo) * 0.5f);
		e.op = Op::Subtract;
		b.add(e);
	};
	// An x range, run past the ends it reaches.
	auto past = [&](vec2 x) {
		return vec2(x.x <= kSame ? -kPast : x.x, x.y >= s.x - kSame ? s.x + kPast : x.y);
	};
	const float m = kPast;
	for (const Feature &f : part.features) {
		switch (f.kind) {
			case Feature::Kind::Hole: {
				if (axis_of(f.face) == 0) {
					break;
				}
				const vec2 x = past(span(f, s.x));
				const int axis = axis_of(f.face), across = axis == 2 ? 1 : 2;
				const float size = get(s, axis);
				const bool low = position(f.face, s) == 0.0f; // the side or the face edge
				vec3 lo(x.x, 0.0f, 0.0f), hi(x.y, 0.0f, 0.0f);
				set(lo, across, f.across.x);
				set(hi, across, f.across.y);
				set(lo, axis, f.through || low ? -m : size - f.depth);
				set(hi, axis, f.through || !low ? size + m : f.depth);
				cut(lo, hi);
				break;
			}
			case Feature::Kind::Tenon: {
				const float xa = f.far ? s.x - f.length : -m, xb = f.far ? s.x + m : f.length;
				if (f.y.x > 0.0f) {
					cut({xa, -m, -m}, {xb, f.y.x, s.z + m});
				}
				if (f.y.y < s.y) {
					cut({xa, f.y.y, -m}, {xb, s.y + m, s.z + m});
				}
				if (f.z.x > 0.0f) {
					cut({xa, -m, -m}, {xb, s.y + m, f.z.x});
				}
				if (f.z.y < s.z) {
					cut({xa, -m, f.z.y}, {xb, s.y + m, s.z + m});
				}
				break;
			}
			case Feature::Kind::Kerf: {
				const float xa = f.far ? s.x - f.depth : -m, xb = f.far ? s.x + m : f.depth;
				const float half = 0.5f * (f.width > 0.0f ? f.width : 0.8f);
				if (f.axis == 1) {
					cut({xa, f.at - half, -m}, {xb, f.at + half, s.z + m});
				} else {
					cut({xa, -m, f.at - half}, {xb, s.y + m, f.at + half});
				}
				break;
			}
			case Feature::Kind::Rebate: {
				if (axis_of(f.face) == 0 || axis_of(f.other) == 0 || axis_of(f.face) == axis_of(f.other)) {
					break;
				}
				const vec2 x = past(span(f, s.x));
				vec3 lo(x.x, 0.0f, 0.0f), hi(x.y, 0.0f, 0.0f);
				// Within `depth` of the face, `width` of the other.
				for (const auto &[face, reach] : {std::pair{f.face, f.depth}, std::pair{f.other, f.width}}) {
					const int axis = axis_of(face);
					const float at = position(face, s);
					const bool low = at == 0.0f;
					set(lo, axis, low ? -m : at - reach);
					set(hi, axis, low ? reach : at + m);
				}
				cut(lo, hi);
				break;
			}
			case Feature::Kind::Chamfer: {
				if (axis_of(f.face) == 0 || axis_of(f.other) == 0 || axis_of(f.face) == axis_of(f.other)) {
					break;
				}
				const vec2 x = past(span(f, s.x));
				vec3 centre(0.5f * (x.x + x.y), 0.0f, 0.0f);
				set(centre, axis_of(f.face), position(f.face, s));
				set(centre, axis_of(f.other), position(f.other, s));
				// A square turned 45 degrees about the edge: its corners the chamfer's width
				// along each face.
				const float h = f.width / std::sqrt(2.0f), q = std::sin(0.25f * 3.14159265f * 0.5f);
				Edit e;
				e.prim = Primitive::box(centre, {0.5f * (x.y - x.x), h, h}, 0.0f,
						{q, 0.0f, 0.0f, std::cos(0.25f * 3.14159265f * 0.5f)});
				e.op = Op::Subtract;
				b.add(e);
				break;
			}
			case Feature::Kind::Taper: {
				const bool back = f.face == Face::Back;
				const float slope = (f.to - f.from) / s.x;
				const vec3 point(0.0f, 0.0f, back ? f.from : s.z - f.from);
				const vec3 normal = gl::normalize(vec3(-slope, 0.0f, back ? 1.0f : -1.0f));
				b.add(Plane{point, normal}.keep_behind());
				break;
			}
		}
	}
	return b;
}

} // namespace sdf::plans
