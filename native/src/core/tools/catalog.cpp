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

const std::vector<RaspVariant> &rasp_catalog() {
	static const std::vector<RaspVariant> catalog = [] {
		auto rasp = [](float coarseness, bool round) {
			Rasp r;
			r.coarseness = coarseness;
			r.round = round;
			return r;
		};
		return std::vector<RaspVariant>{
				{"rasp_wood", "Wood rasp, coarse", rasp(1.0f, false)},
				{"rasp_cabinet", "Cabinet rasp", rasp(0.5f, false)},
				{"rasp_cabinet_round", "Cabinet rasp, round face", rasp(0.5f, true)},
				{"rasp_pattern", "Patternmaker's rasp, fine", rasp(0.25f, false)},
		};
	}();
	return catalog;
}

const RaspVariant *find_rasp(const std::string &id) {
	for (const RaspVariant &v : rasp_catalog()) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

const std::vector<SandingVariant> &sanding_catalog() {
	static const std::vector<SandingVariant> catalog = [] {
		auto block = [](float length, float breadth) {
			SandingBlock b;
			b.length = length;
			b.breadth = breadth;
			return b;
		};
		return std::vector<SandingVariant>{
				{"block", "Cork block 70 \u00d7 40", block(70.0f, 40.0f)},
				{"pad", "Small pad 35 \u00d7 20", block(35.0f, 20.0f)},
		};
	}();
	return catalog;
}

const SandingVariant *find_sanding(const std::string &id) {
	for (const SandingVariant &v : sanding_catalog()) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

const std::vector<PlaneVariant> &plane_catalog() {
	static const std::vector<PlaneVariant> catalog = [] {
		Spokeshave block;
		block.kind = Spokeshave::Kind::BlockPlane;
		block.blade_width = 35.0f;
		block.sole = 150.0f;
		block.sole_width = 42.0f;
		block.bed_deg = 37.0f; // a 12 degree bed, the bevel up at 25
		block.hand_force = 200.0f; // (one hand, pushing along its length)
		block.mouth = 0.5f;
		Spokeshave fenced = block;
		fenced.fence = true;
		Spokeshave shoulder;
		shoulder.kind = Spokeshave::Kind::ShoulderPlane;
		shoulder.blade_width = 19.0f;
		shoulder.sole = 160.0f;
		shoulder.sole_width = 19.0f;
		shoulder.bed_deg = 40.0f;
		shoulder.hand_force = 200.0f;
		shoulder.mouth = 0.5f;
		return std::vector<PlaneVariant>{
				{"spokeshave", "Spokeshave", Spokeshave{}},
				{"block_plane", "Block plane", block},
				{"block_plane_fence", "Block plane, chamfer fence", fenced},
				{"shoulder_plane", "Shoulder plane 19 mm", shoulder},
		};
	}();
	return catalog;
}

const PlaneVariant *find_plane(const std::string &id) {
	for (const PlaneVariant &v : plane_catalog()) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

} // namespace sdf::tools
