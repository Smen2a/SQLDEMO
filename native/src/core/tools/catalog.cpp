#include "tools/catalog.h"

namespace sdf::tools {

namespace {

Chisel flat(float width, float thickness, float bevel, float blade, float hand, float mallet, float skew = 0.0f) {
	Chisel c;
	c.width = width;
	c.thickness = thickness;
	c.bevel_deg = bevel;
	c.blade_length = blade;
	c.hand_force = hand;
	c.mallet = mallet;
	c.skew_deg = skew;
	return c;
}

Chisel gouge(float width, float radius, float blade) {
	Chisel c = flat(width, 2.5f, 22.0f, blade, 180.0f, 0.8f);
	c.kind = gl::SDF_TOOL_GOUGE;
	c.sweep_radius = radius;
	return c;
}

} // namespace

const std::vector<ChiselVariant> &chisel_catalog() {
	static const std::vector<ChiselVariant> catalog = [] {
		std::vector<ChiselVariant> v;
		v.push_back({"bench_6", "chisel", "Bench chisel 6 mm", flat(6.0f, 4.0f, 27.0f, 90.0f, 200.0f, 1.0f)});
		v.push_back({"bench_12", "chisel", "Bench chisel 12 mm", flat(12.0f, 4.0f, 27.0f, 95.0f, 200.0f, 1.0f)});
		v.push_back({"bench_25", "chisel", "Bench chisel 25 mm", flat(25.0f, 4.5f, 25.0f, 105.0f, 200.0f, 1.0f)});
		// Long and thin: it flexes before a hand can put its whole weight behind it.
		v.push_back({"paring_25", "chisel", "Paring chisel 25 mm", flat(25.0f, 3.0f, 20.0f, 180.0f, 160.0f, 0.0f)});
		v.push_back({"mortise_8", "chisel", "Mortise chisel 8 mm", flat(8.0f, 9.0f, 32.0f, 130.0f, 200.0f, 1.5f)});
		v.push_back({"skew_18", "chisel", "Skew chisel 18 mm", flat(18.0f, 3.5f, 25.0f, 95.0f, 200.0f, 0.5f, 30.0f)});
		v.push_back({"gouge_3_12", "gouge", "Gouge #3 12 mm", gouge(12.0f, 21.6f, 90.0f)});
		v.push_back({"gouge_7_12", "gouge", "Gouge #7 12 mm", gouge(12.0f, 7.2f, 90.0f)});
		v.push_back({"veiner_11_3", "gouge", "Veiner #11 3 mm", gouge(3.0f, 1.5f, 80.0f)});
		Chisel v_tool = flat(6.0f, 2.0f, 22.0f, 85.0f, 180.0f, 0.8f);
		v_tool.kind = gl::SDF_TOOL_V;
		v_tool.v_angle_deg = 60.0f;
		v.push_back({"v_60_6", "gouge", "V-tool 60° 6 mm", v_tool});
		return v;
	}();
	return catalog;
}

const ChiselVariant *find_chisel(const std::string &id) {
	for (const ChiselVariant &v : chisel_catalog()) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

} // namespace sdf::tools
