#include "tools/rubbing.h"

#include "eval/query.h"
#include "tools/debris.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf::tools {

namespace {

constexpr float kNone = -std::numeric_limits<float>::infinity();
constexpr float kTouch = 0.02f;       // mm: work this near the face's floor bears on it
constexpr float kRecut = 0.002f;      // mm deeper before a patch is cut again
constexpr float kSpread = 2.0f;       // mm wider before a patch is cut again
constexpr float kLeastContact = 0.1f; // bearing on less, the hand eases off
constexpr float kFreeStep = 2.5f;     // mm between a free face's height samples
constexpr float kLineStep = 2.0f;     // a line face's
constexpr float kAim = 5.0f;          // mm a free face moves before its patch takes its direction

// Grid indices [k0, k1] of the samples covering [a, b] along one axis (at least the nearest
// to its middle), and how many there would be off the map too.
bool axis(float a, float b, float origin, float step, int n, int &k0, int &k1, int &all) {
	k0 = int(std::ceil((a - origin) / step - 1e-4f));
	k1 = int(std::floor((b - origin) / step + 1e-4f));
	if (k0 > k1) {
		k0 = k1 = int(std::lround((0.5f * (a + b) - origin) / step));
	}
	all = k1 - k0 + 1;
	k0 = std::max(k0, 0);
	k1 = std::min(k1, n - 1);
	return k0 <= k1;
}

} // namespace

HeightMap::HeightMap(const Body &body, const Octree &octree, const Frame &plane, vec2 lo, vec2 hi, float step,
		int most) {
	// The work's extent over the plane: the map covers no more, and its rays start above it.
	const Aabb box = body.bounds();
	if (box.empty() || !(gl::length(box.size()) < 1e6f)) {
		return;
	}
	vec3 near(1e30f), far(-1e30f);
	for (int c = 0; c < 8; ++c) {
		const vec3 corner(c & 1 ? box.hi.x : box.lo.x, c & 2 ? box.hi.y : box.lo.y, c & 4 ? box.hi.z : box.lo.z);
		const vec3 d = corner - plane.origin;
		const vec3 local(gl::dot(d, plane.x), gl::dot(d, plane.y), gl::dot(d, plane.z));
		near = gl::min(near, local);
		far = gl::max(far, local);
	}
	lo = gl::max(lo, vec2(near.x, near.y) - vec2(step));
	hi = gl::min(hi, vec2(far.x, far.y) + vec2(step));
	if (lo.x > hi.x || lo.y > hi.y) {
		return;
	}
	step_ = vec2(std::max(step, (hi.x - lo.x) / float(most - 1)), std::max(step, (hi.y - lo.y) / float(most - 1)));
	nx_ = int(std::floor((hi.x - lo.x) / step_.x)) + 1;
	ny_ = int(std::floor((hi.y - lo.y) / step_.y)) + 1;
	lo_ = lo;
	h_.assign(std::size_t(nx_ * ny_), kNone);
	const float from = far.z + 1.0f, length = far.z - near.z + 2.0f;
	for (int j = 0; j < ny_; ++j) {
		for (int i = 0; i < nx_; ++i) {
			const vec3 origin = plane.point({lo.x + float(i) * step_.x, lo.y + float(j) * step_.y, from});
			const auto hit = raycast(body, octree, origin, -plane.z, length, 1e-3f);
			if (hit) {
				h_[std::size_t(j * nx_ + i)] = gl::dot(hit->point - plane.origin, plane.z);
			}
		}
	}
}

// Calls f(index) for each sample in a region, or the one nearest its middle when none is.
template <typename F> void HeightMap::over(const Region &r, F &&f) const {
	if (nx_ == 0) {
		return;
	}
	vec2 lo(1e30f), hi(-1e30f);
	for (const vec2 c : {r.lo, r.hi, vec2(r.lo.x, r.hi.y), vec2(r.hi.x, r.lo.y)}) {
		lo = gl::min(lo, r.plane(c));
		hi = gl::max(hi, r.plane(c));
	}
	int i0, i1, j0, j1, all;
	bool any = false;
	if (axis(lo.x, hi.x, lo_.x, step_.x, nx_, i0, i1, all) && axis(lo.y, hi.y, lo_.y, step_.y, ny_, j0, j1, all)) {
		for (int j = j0; j <= j1; ++j) {
			for (int i = i0; i <= i1; ++i) {
				if (r.contains(lo_ + vec2(float(i) * step_.x, float(j) * step_.y))) {
					f(std::size_t(j * nx_ + i));
					any = true;
				}
			}
		}
	}
	if (!any) {
		const vec2 mid = r.plane((r.lo + r.hi) * 0.5f);
		const int i = int(std::lround((mid.x - lo_.x) / step_.x)), j = int(std::lround((mid.y - lo_.y) / step_.y));
		if (i >= 0 && i < nx_ && j >= 0 && j < ny_) {
			f(std::size_t(j * nx_ + i));
		}
	}
}

float HeightMap::highest(const Region &r) const {
	float top = kNone;
	over(r, [&](std::size_t k) { top = std::max(top, h_[k]); });
	return top;
}

float HeightMap::median(const Region &r) const {
	std::vector<float> on;
	over(r, [&](std::size_t k) {
		if (h_[k] != kNone) {
			on.push_back(h_[k]);
		}
	});
	if (on.empty()) {
		return kNone;
	}
	std::nth_element(on.begin(), on.begin() + std::ptrdiff_t(on.size() / 2), on.end());
	return on[on.size() / 2];
}

void HeightMap::lower(const Region &r, float floor) {
	over(r, [&](std::size_t k) { h_[k] = std::min(h_[k], floor); });
}

float HeightMap::share_above(vec2 lo, vec2 hi, float level) const {
	if (nx_ == 0) {
		return 0.0f;
	}
	int i0, i1, j0, j1, across = 0, along = 0;
	const bool x = axis(lo.x, hi.x, lo_.x, step_.x, nx_, i0, i1, along);
	const bool y = axis(lo.y, hi.y, lo_.y, step_.y, ny_, j0, j1, across);
	if (!x || !y) {
		return 0.0f;
	}
	int bearing = 0;
	for (int j = j0; j <= j1; ++j) {
		for (int i = i0; i <= i1; ++i) {
			bearing += h_[std::size_t(j * nx_ + i)] >= level;
		}
	}
	// Samples off the map are off the work: they bear on nothing.
	return float(bearing) / float(std::max(along * across, 1));
}

namespace {

// The ground a face has worked over, lying along the way it went, cut down below the
// highest point under it.
struct Patch {
	int id = 0;
	Region centre;       // the range the face's centre has covered (along its axis, across)
	vec2 reach{0.0f};    // half the face's extent along the patch's axis and across it
	bool aimed = false;  // its axis is the way the face moved (not yet: the face's own x)
	float rest = kNone;  // plane z the face rests at (the highest point under it)
	float depth = 0.0f;  // mm it has taken below that
	float travel = 0.0f; // mm of cutting travel over it, each weighted by 1 / contact
	// Its cut as last made (and handed on).
	bool cut = false;
	int version = 0;
	Region cut_region;
	float cut_floor = 0.0f, cut_depth = 0.0f;
	float cut_typical = 0.0f; // how far below most of its ground its floor is
	float dusted = 0.0f; // mm^3 of its dust reported

	Region ground() const {
		Region g = centre;
		g.lo = centre.lo - reach;
		g.hi = centre.hi + reach;
		return g;
	}
};

class RubStroke : public Stroke {
public:
	RubStroke(const RubFace &face, const Work &work, const Frame &plane, float pace)
		: face_(face), plane_(plane), rate_(face.rate * pace), half_(0.5f * face.length, 0.5f * face.width) {
		const vec2 lo = face_.line ? vec2(face_.from - half_.x, -half_.y) : vec2(-kRubReach);
		const vec2 hi = face_.line ? vec2(face_.to + half_.x, half_.y) : vec2(kRubReach);
		map_ = HeightMap(work.body, work.octree, plane_, lo, hi, face_.line ? kLineStep : kFreeStep);
		at_ = face_.line ? vec2(std::clamp(0.0f, face_.from, face_.to), 0.0f) : vec2(0.0f);
		begin(at_);
		const Patch &p = patches_.back();
		contact_ = map_.share_above(at_ - half_, at_ + half_, p.rest - kTouch);
	}

	StrokeUpdate move_to(vec3 point) override {
		const vec3 d = point - plane_.origin;
		vec2 at(gl::dot(d, plane_.x), gl::dot(d, plane_.y));
		if (face_.line) {
			at = vec2(std::clamp(at.x, face_.from, face_.to), 0.0f);
		}
		const vec2 step = at - at_;
		if (step.x == 0.0f && step.y == 0.0f) {
			return {};
		}
		if (step.x != 0.0f) {
			heading_ = step.x > 0.0f ? 1.0f : -1.0f;
		}
		const float cutting = face_.cuts == 0 ? gl::length(step) : std::max(float(face_.cuts) * step.x, 0.0f);
		at_ = at;

		// On along the same line over the same ground, or a new patch.
		Patch *p = &patches_.back();
		if (!grow(*p, at)) {
			finish_patch(*p);
			begin(at);
			p = &patches_.back();
		}

		// It takes its share off, bearing on what reaches its floor.
		contact_ = map_.share_above(at - half_, at + half_, p->rest - p->depth - kTouch);
		if (cutting > 0.0f && contact_ > 0.0f) {
			p->travel += cutting / std::max(contact_, kLeastContact);
			// Each point has been under the face for its length along the way it goes (its
			// area over its width across it), of every pass over it.
			const float along = face_.length * face_.width / (2.0f * p->reach.y);
			const float range = p->centre.hi.x - p->centre.lo.x;
			p->depth = std::max(p->depth, rate_ * p->travel * std::min(1.0f, along / std::max(range, 1e-3f)));
		}
		const Region g = p->ground();
		if (p->depth >= p->cut_depth + kRecut ||
				(p->cut && (g.lo.x < p->cut_region.lo.x - kSpread || g.lo.y < p->cut_region.lo.y - kSpread ||
									g.hi.x > p->cut_region.hi.x + kSpread || g.hi.y > p->cut_region.hi.y + kSpread ||
									g.u.x != p->cut_region.u.x || g.u.y != p->cut_region.u.y))) {
			recut(*p);
		}
		return handed_on();
	}

	std::vector<Edit> edits() const override {
		std::vector<Edit> out;
		for (const Patch &p : patches_) {
			if (p.cut) {
				out.push_back(edit(p));
			}
		}
		return out;
	}

	Frame pose() const override {
		const Patch &p = patches_.back();
		Frame f = plane_;
		f.origin = plane_.point({at_.x, at_.y, p.rest == kNone ? 0.0f : p.rest - p.cut_depth});
		return f;
	}

	StrokeState state() const override {
		StrokeState s;
		s.depth = patches_.back().depth;
		s.contact = contact_;
		return s;
	}

	// Its dust: each patch's depth over its ground, as far as that is over material (probed
	// half way down), less what was reported; it leaves from where the face is.
	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		out.ended = out.ended || ended;
		for (Patch &p : patches_) {
			if (!p.cut) {
				continue;
			}
			const Region &r = p.cut_region;
			const float fraction = material_fraction(body, octree, frame(r, p.cut_floor + p.cut_depth), r.lo, r.hi,
					std::max(0.5f * p.cut_depth, 0.005f), 7, 7);
			const float across = face_.section ? face_.section(p.cut_depth) : (r.hi.y - r.lo.y) * p.cut_depth;
			const float taken = across * (r.hi.x - r.lo.x) * fraction;
			pending_ += std::max(taken - p.dusted, 0.0f);
			p.dusted = std::max(p.dusted, taken);
		}
		if (pending_ < kLeastDust && !(ended && pending_ > 0.0f)) {
			return;
		}
		const Patch &p = patches_.back();
		Dust d;
		d.point = plane_.point({at_.x, at_.y, p.rest == kNone ? 0.0f : p.rest});
		d.direction = face_.thrown ? gl::normalize(plane_.x * heading_ + plane_.z * 0.5f) : plane_.z;
		d.volume = pending_;
		d.grain = face_.grain;
		d.spread = 0.5f * (face_.thrown ? face_.width : std::min(face_.length, face_.width));
		out.dust.push_back(d);
		pending_ = 0.0f;
	}

private:
	// The plane turned along a region's axis, raised to `z`.
	Frame frame(const Region &r, float z) const {
		Frame f;
		f.z = plane_.z;
		f.x = plane_.x * r.u.x + plane_.y * r.u.y;
		f.y = gl::cross(f.z, f.x);
		f.origin = plane_.point({r.origin.x, r.origin.y, z});
		return f;
	}

	// Half the face's extent along `u` and across it (a turned face's bounding box).
	vec2 reach_along(vec2 u) const {
		const float c = std::fabs(u.x), s = std::fabs(u.y);
		return {c * half_.x + s * half_.y, s * half_.x + c * half_.y};
	}

	// A new patch where the face is now; the two that merge most compactly merged if that
	// makes too many.
	void begin(vec2 at) {
		Patch p;
		p.id = next_id_++;
		p.centre.origin = at;
		p.aimed = face_.line;
		p.reach = half_;
		p.rest = map_.highest(p.ground());
		patches_.push_back(p);
		if (int(patches_.size()) > kMostPatches) {
			merge();
		}
	}

	// Whether the patch takes the face's centre at `at`: along its line (once the face has
	// moved far enough to show which way that is), not onto higher ground once it has cut.
	bool grow(Patch &p, vec2 at) {
		Patch g = p;
		if (!g.aimed && gl::length(at - g.centre.origin) >= kAim) {
			// The way it goes: the patch lies along it from where it began.
			g.aimed = true;
			g.centre.u = gl::normalize(at - g.centre.origin);
			g.centre.lo = g.centre.hi = vec2(0.0f);
			g.reach = reach_along(g.centre.u);
		}
		const vec2 l = g.centre.local(at);
		g.centre.lo = gl::min(g.centre.lo, l);
		g.centre.hi = gl::max(g.centre.hi, l);
		// Drifting across its line by up to half the face; not jumping beyond it.
		if (g.centre.hi.y - g.centre.lo.y > g.reach.y || l.x < p.centre.lo.x - 2.0f * g.reach.x ||
				l.x > p.centre.hi.x + 2.0f * g.reach.x) {
			return false;
		}
		const float rest = map_.highest(g.ground());
		if (p.depth >= 1e-4f && rest > p.rest + kTouch) {
			return false; // it rests on higher ground now
		}
		g.rest = std::max(g.rest, rest);
		p = g;
		return true;
	}

	// A patch the face leaves: cut to all it has taken (the ground under it lowered to
	// that), or let go if it took nothing.
	void finish_patch(Patch &p) {
		if (p.depth > p.cut_depth + 1e-6f && (p.cut || p.depth >= kRecut)) {
			recut(p);
		}
		if (!p.cut) {
			patches_.pop_back();
			return;
		}
		map_.lower(p.cut_region, p.cut_floor);
	}

	// Two patches (not the one in use) as one: the pair whose rectangle round both wastes
	// least, at their floors' mean over their areas.
	void merge() {
		const std::size_t n = patches_.size() - 1;
		std::size_t a = 0, b = 1;
		float best = 1e30f;
		for (std::size_t i = 0; i < n; ++i) {
			for (std::size_t j = i + 1; j < n; ++j) {
				const Region r = around(patches_[i].cut_region, patches_[j].cut_region);
				const float waste = r.area() / std::max(patches_[i].cut_region.area() + patches_[j].cut_region.area(), 1e-3f);
				if (waste < best) {
					best = waste;
					a = i;
					b = j;
				}
			}
		}
		Patch &pa = patches_[a];
		const Patch &pb = patches_[b];
		const float wa = pa.cut_region.area(), wb = pb.cut_region.area();
		Patch m = pa;
		m.id = next_id_++;
		m.cut_region = around(pa.cut_region, pb.cut_region);
		m.centre = m.cut_region;
		m.reach = vec2(0.0f);
		m.rest = std::max(pa.rest, pb.rest);
		m.cut_floor = (pa.cut_floor * wa + pb.cut_floor * wb) / std::max(wa + wb, 1e-3f);
		m.cut_depth = m.depth = std::max(m.rest - m.cut_floor, 0.0f);
		m.cut_typical = std::min(pa.cut_typical, pb.cut_typical);
		m.dusted = pa.dusted + pb.dusted;
		patches_[a] = m;
		patches_.erase(patches_.begin() + std::ptrdiff_t(b));
	}

	// The rectangle round two regions, along the first's axis.
	static Region around(const Region &a, const Region &b) {
		Region r = a;
		for (const vec2 c : {b.lo, b.hi, vec2(b.lo.x, b.hi.y), vec2(b.hi.x, b.lo.y)}) {
			const vec2 l = a.local(b.plane(c));
			r.lo = gl::min(r.lo, l);
			r.hi = gl::max(r.hi, l);
		}
		return r;
	}

	void recut(Patch &p) {
		if (p.rest == kNone || p.depth <= 0.0f) {
			return;
		}
		p.cut = true;
		++p.version;
		p.cut_region = p.ground();
		p.cut_depth = p.depth;
		p.cut_floor = p.rest - p.depth;
		p.cut_typical = std::clamp(map_.median(p.cut_region) - p.cut_floor, 0.0f, p.depth);
	}

	Edit edit(const Patch &p) const {
		const Region &r = p.cut_region;
		return face_.pass(frame(r, 0.0f), r.lo, r.hi, p.cut_floor, p.cut_depth, p.cut_typical);
	}

	// What changed since the last update, for the edit session: the patches' cuts from the
	// first that differs from what it holds.
	StrokeUpdate handed_on() {
		std::vector<std::pair<int, int>> now;
		for (const Patch &p : patches_) {
			if (p.cut) {
				now.push_back({p.id, p.version});
			}
		}
		std::size_t same = 0;
		while (same < now.size() && same < held_.size() && now[same] == held_[same]) {
			++same;
		}
		StrokeUpdate u;
		u.drop = held_.size() - same;
		std::size_t k = 0;
		for (const Patch &p : patches_) {
			if (p.cut && k++ >= same) {
				u.edits.push_back(edit(p));
			}
		}
		held_ = std::move(now);
		return u;
	}

	RubFace face_;
	Frame plane_;
	float rate_;
	vec2 half_;
	HeightMap map_;
	std::vector<Patch> patches_;
	std::vector<std::pair<int, int>> held_; // (id, version) of the cuts the session holds
	int next_id_ = 1;
	vec2 at_{0.0f};
	float heading_ = 1.0f;
	float contact_ = 1.0f;
	float pending_ = 0.0f; // dust held back (mm^3)
};

} // namespace

std::unique_ptr<Stroke> rub_stroke(const RubFace &face, const Work &work, const Frame &plane, float pace) {
	return std::make_unique<RubStroke>(face, work, plane, pace);
}

} // namespace sdf::tools
