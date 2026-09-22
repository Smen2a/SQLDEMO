#include "demo/gallery.h"

#include <cmath>

namespace sdf::demo {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kTileW = 240, kTileH = 180, kLabelH = 18;

Camera tile_camera() {
	Camera c;
	c.eye = {36, -48, 42};
	c.target = {1, 0, 3};
	c.fov_deg = 30;
	return c;
}

Body block(std::uint16_t material, vec3 half, float rounding = 0.0f) {
	Body b;
	b.base = Primitive::box({0, 0, 0}, half, rounding);
	b.base_material = material;
	// Flat-sawn by default: pith below the board, axis tilted slightly out of the face so
	// the growth rings surface as nested arches.
	b.grain_origin = {0, 4, -40};
	b.grain_axis = gl::normalize(vec3(1, 0.03f, 0.12f));
	return b;
}

Edit cut(Primitive prim, Blend blend = Blend::Hard, float r = 0, float r2 = 0) {
	Edit e;
	e.prim = prim;
	e.op = Op::Subtract;
	e.blend = blend;
	e.r = r;
	e.r2 = r2 > 0 ? r2 : r;
	return e;
}

Edit add(Primitive prim, std::uint16_t material, Blend blend = Blend::Hard, float r = 0) {
	Edit e;
	e.prim = prim;
	e.op = Op::Union;
	e.blend = blend;
	e.r = e.r2 = r;
	e.material = material;
	return e;
}

vec3 polar(float radius, float angle, float z) {
	return {radius * std::cos(angle), radius * std::sin(angle), z};
}

// A V-tool leaf outline: two curved cuts meeting at base and tip, plus a shallow vein.
void add_leaf(Body &b, vec3 base, vec3 tip, float bulge, float top, float depth = 1.6f) {
	const vec3 mid = (base + tip) * 0.5f;
	const vec3 along = gl::normalize(tip - base);
	const vec3 side = gl::normalize(gl::cross(vec3(0, 0, 1), along));
	const ToolProfile vee = ToolProfile::v_tool(60, depth + 2.0f);
	const vec3 lift(0, 0, top - depth);
	for (float s : {-1.0f, 1.0f}) {
		b.add(cut(Primitive::sweep(base + lift, mid + side * (s * bulge) + lift, tip + lift, {0, 0, 1}, vee)));
	}
	b.add(cut(Primitive::sweep(base + along * 2.0f + vec3(0, 0, top - 0.7f), tip - along * 2.5f + vec3(0, 0, top - 0.7f),
			{0, 0, 1}, vee)));
}

Image compose(const std::vector<Tile> &tiles, int columns, const MaterialTable &materials, const GalleryOptions &o) {
	const int s = o.scale;
	const int tw = kTileW * s, th = kTileH * s, lh = kLabelH * s;
	const int rows = int((tiles.size() + columns - 1) / columns);
	Image out(columns * tw, rows * (th + lh), 30);
	RenderSettings rs;
	rs.width = tw;
	rs.height = th;
	rs.samples_per_axis = o.samples_per_axis;
	rs.threads = o.threads;
	for (std::size_t i = 0; i < tiles.size(); ++i) {
		const int x = int(i % columns) * tw, y = int(i / columns) * (th + lh);
		fill_rect(out, x, y, tw, lh, 28, 30, 34);
		draw_text(out, x + 6 * s, y + 2 * s, tiles[i].label, 2 * s, 222, 224, 228);
		blit(out, render(tiles[i].body, materials, tiles[i].camera, rs), x, y + lh);
	}
	return out;
}

} // namespace

std::vector<Tile> blend_tiles() {
	struct Mode {
		const char *label;
		Blend blend;
		EdgeProfile shape;
		float r, r2;
	};
	const Mode modes[] = {
		{"HARD", Blend::Hard, EdgeProfile::ArcConcave, 0, 0},
		{"CHAMFER", Blend::Chamfer, EdgeProfile::ArcConcave, 2.5f, 2.5f},
		{"CHAMFER 1:4", Blend::Chamfer, EdgeProfile::ArcConcave, 1.0f, 4.0f},
		{"ROUND", Blend::Round, EdgeProfile::ArcConcave, 2.5f, 2.5f},
		{"SMOOTH C1", Blend::Smooth, EdgeProfile::ArcConcave, 5.0f, 5.0f},
		{"SMOOTH C2", Blend::SmoothC2, EdgeProfile::ArcConcave, 5.0f, 5.0f},
		{"COVE PROFILE", Blend::Profile, EdgeProfile::ArcConvex, 3.0f, 3.0f},
		{"OGEE PROFILE", Blend::Profile, EdgeProfile::Ogee, 3.0f, 3.0f},
	};
	std::vector<Tile> tiles;
	for (const Mode &m : modes) {
		Body b = block(mat::Ash, {22, 16, 5});
		Edit boss = add(Primitive::cylinder({-9, 0, 9.5f}, 6.5f, 5.0f, 0, quat_axis_angle({1, 0, 0}, kPi / 2)), mat::Ash,
				m.blend, m.r);
		boss.r2 = m.r2;
		boss.shape = m.shape;
		b.add(boss);
		Edit channel = cut(Primitive::sweep({9, -20, 1}, {9, 20, 1}, {0, 0, 1}, ToolProfile::flat(9, 10)), m.blend, m.r, m.r2);
		channel.shape = m.shape;
		b.add(channel);
		tiles.push_back({m.label, b, tile_camera()});
	}
	return tiles;
}

std::vector<Tile> material_tiles() {
	std::vector<Tile> tiles;
	const ToolProfile vee = ToolProfile::v_tool(60, 4);

	{ // Flat-sawn ash: cuts reveal the arches of the growth rings.
		Body b = block(mat::Ash, {22, 16, 5});
		b.grain_origin = {0, 0, -30};
		b.grain_axis = gl::normalize(vec3(1, 0, 0.18f));
		add_leaf(b, {-17, -2, 0}, {17, 2, 0}, 8, 5, 2.6f);
		b.add(cut(Primitive::sweep({-12, 0, 3.8f}, {0, 2, 3.8f}, {12, 1, 3.8f}, {0, 0, 1}, ToolProfile::gouge(3, 5, 4))));
		tiles.push_back({"ASH FLAT SAWN", b, tile_camera()});
	}
	{ // Quarter-sawn oak: pith off to the side, so rings surface as straight stripes.
		Body b = block(mat::Oak, {22, 16, 5});
		b.grain_origin = {0, -60, 0};
		b.grain_axis = gl::normalize(vec3(1, 0.02f, 0.03f));
		for (int i = 0; i < 5; ++i) {
			const float x = -14 + 7 * float(i);
			b.add(cut(Primitive::sweep({x, -12, 3.2f}, {x + 1, 12, 3.2f}, {0, 0, 1}, vee)));
		}
		tiles.push_back({"OAK QUARTER SAWN", b, tile_camera()});
	}
	{ // Putty filling a gouged crack: smooth unions mix materials across the seam.
		Body b = block(mat::Walnut, {22, 16, 5});
		b.add(cut(Primitive::sweep({-20, -8, 2.5f}, {0, 6, 2.5f}, {20, -4, 2.5f}, {0, 0, 1}, ToolProfile::gouge(4, 7, 6))));
		b.add(add(Primitive::capsule({-19, -8, 4.2f}, {-2, 2, 4.8f}, 3.2f), mat::Putty, Blend::Smooth, 4));
		b.add(add(Primitive::capsule({-2, 2, 4.8f}, {19, -4, 4.4f}, 3.2f), mat::Putty, Blend::Smooth, 4));
		tiles.push_back({"WALNUT + PUTTY", b, tile_camera()});
	}
	{ // Flush brass inlay: a hard material boundary on one continuous surface, crossed
		// by an engraved line and set beside a raised brass pin.
		Body b = block(mat::Walnut, {22, 16, 5});
		Edit inlay;
		inlay.prim = Primitive::box({-4, 0, 5}, {7, 7, 3}, 0, quat_axis_angle({0, 0, 1}, kPi / 4));
		inlay.op = Op::Paint;
		inlay.material = mat::Brass;
		b.add(inlay);
		Edit line;
		line.prim = Primitive::plane(gl::normalize(vec3(1, -0.35f, 0)), -4);
		line.op = Op::Engrave;
		line.r = 1.2f;
		b.add(line);
		b.add(add(Primitive::cylinder({13, 7, 6.5f}, 3.0f, 2.0f, 0.6f, quat_axis_angle({1, 0, 0}, kPi / 2)), mat::Brass));
		tiles.push_back({"BRASS INLAY", b, tile_camera()});
	}
	{ // Tooled granite: rounded block, flat chisel strokes with slightly eased edges.
		Body b = block(mat::Granite, {22, 16, 6}, 1.5f);
		for (int i = 0; i < 6; ++i) {
			const float y = -12 + 4.6f * float(i);
			b.add(cut(Primitive::sweep({-24, y, 5.3f - 0.15f * float(i)}, {24, y + 1.2f, 5.0f}, {0, 0, 1},
					ToolProfile::flat(4.2f, 4)), Blend::Round, 0.5f));
		}
		b.add(cut(Primitive::sweep({-8, -20, 1.5f}, {-4, 0, 1.0f}, {-9, 20, 1.5f}, {0, 0, 1}, ToolProfile::gouge(5, 8, 8)),
				Blend::Round, 0.8f));
		tiles.push_back({"GRANITE", b, tile_camera()});
	}
	{ // Steel: a forged-style block with a chamfered eye, a filleted slot and a drilled hole.
		Body b = block(mat::Steel, {20, 12, 6}, 1.0f);
		b.add(cut(Primitive::box({-5, 0, 0}, {6, 4, 10}), Blend::Chamfer, 1.4f));
		b.add(cut(Primitive::sweep({7, -6, 3}, {7, 6, 3}, {0, 0, 1}, ToolProfile::flat(4, 6)), Blend::Round, 1.2f));
		b.add(cut(Primitive::cylinder({14, 0, 0}, 1.8f, 10, 0, quat_axis_angle({1, 0, 0}, kPi / 2)), Blend::Chamfer, 0.6f));
		tiles.push_back({"STEEL", b, tile_camera()});
	}
	return tiles;
}

Body carved_panel_body() {
	Body b = block(mat::Ash, {60, 40, 7}, 1.2f);
	b.grain_origin = {0, 8, -45};
	b.grain_axis = gl::normalize(vec3(1, 0.06f, 0.10f));
	const float top = 7.0f;
	// Cross-sections reach from the cutting edge to a little above the surface: the tool's
	// shank beyond that meets no wood, and a taller profile only inflates bounds.
	const ToolProfile vee = ToolProfile::v_tool(60, 4);

	// Border groove.
	const float bx = 53, by = 33, bz = top - 1.6f;
	b.add(cut(Primitive::sweep({-bx, -by, bz}, {bx, -by, bz}, {0, 0, 1}, vee)));
	b.add(cut(Primitive::sweep({-bx, by, bz}, {bx, by, bz}, {0, 0, 1}, vee)));
	b.add(cut(Primitive::sweep({-bx, -by, bz}, {-bx, by, bz}, {0, 0, 1}, vee)));
	b.add(cut(Primitive::sweep({bx, -by, bz}, {bx, by, bz}, {0, 0, 1}, vee)));

	// Rosette: eight lens-shaped petals, each outlined by two V cuts and scooped with a gouge.
	for (int i = 0; i < 8; ++i) {
		const float a = kPi / 4 * float(i) + kPi / 8;
		const vec3 inner = polar(6, a, top - 1.8f), outer = polar(26, a, top - 1.8f);
		const vec3 mid = (inner + outer) * 0.5f;
		const vec3 side = polar(7, a + kPi / 2, 0);
		b.add(cut(Primitive::sweep(inner, mid + side, outer, {0, 0, 1}, vee)));
		b.add(cut(Primitive::sweep(inner, mid - side, outer, {0, 0, 1}, vee)));
		b.add(cut(Primitive::sweep(polar(9, a, top - 1.0f), polar(21, a, top - 1.0f), {0, 0, 1}, ToolProfile::gouge(3, 5, 3.5f)),
				Blend::Smooth, 0.8f));
	}
	// Centre boss: a ring of short radial gouge cuts.
	for (int i = 0; i < 12; ++i) {
		const float a = kPi / 6 * float(i);
		b.add(cut(Primitive::sweep(polar(1.5f, a, top - 0.6f), polar(5.2f, a, top - 1.3f), {0, 0, 1},
				ToolProfile::gouge(1.2f, 2, 3.5f))));
	}
	// Corner leaves.
	for (int i = 0; i < 4; ++i) {
		const float sx = i % 2 ? 1.0f : -1.0f, sy = i / 2 ? 1.0f : -1.0f;
		add_leaf(b, {sx * 34, sy * 20, 0}, {sx * 48, sy * 28, 0}, 3.5f, top);
	}
	return b;
}

Camera carved_panel_camera() {
	Camera c;
	c.eye = {18, -105, 105};
	c.target = {0, -4, 0};
	c.fov_deg = 38;
	return c;
}

Image blend_gallery(const GalleryOptions &o) {
	return compose(blend_tiles(), 4, MaterialTable::standard(), o);
}

Image material_gallery(const GalleryOptions &o) {
	return compose(material_tiles(), 3, MaterialTable::standard(), o);
}

Image carved_panel(const GalleryOptions &o) {
	RenderSettings rs;
	rs.width = 640 * o.scale;
	rs.height = 400 * o.scale;
	rs.samples_per_axis = o.samples_per_axis;
	rs.threads = o.threads;
	rs.light_dir = gl::normalize(vec3(-0.62f, -0.42f, 0.66f));
	return render(carved_panel_body(), MaterialTable::standard(), carved_panel_camera(), rs);
}

} // namespace sdf::demo
