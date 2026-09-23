#pragma once

#include "ops.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sdf {

using gl::vec3;
using gl::vec4;

// Appearance parameters for one material id, laid out as the shared
// sdf_material_albedo() expects. k[3] and k[4] (grain origin and axis) belong to the
// body, not the material, and are filled in at shading time.
struct Material {
	std::string name;
	vec4 k[5];
	float specular = 0.05f; // strength of the key-light highlight
	float shininess = 24.0f;
};

class MaterialTable {
public:
	std::uint16_t add(const Material &m);
	const Material &operator[](std::uint16_t id) const { return materials_[id]; }
	std::size_t size() const { return materials_.size(); }

	// Albedo of material `id` at body-local point p, for a part whose grain runs along
	// `grain_axis` through `grain_origin`.
	vec3 albedo(float id, vec3 p, vec3 grain_origin, vec3 grain_axis) const;

	// Ash, oak, walnut, putty, brass, steel, granite, cork, abrasive paper — in the order of
	// the ids below.
	static MaterialTable standard();

private:
	std::vector<Material> materials_;
};

namespace mat {
enum : std::uint16_t { Ash = 0, Oak, Walnut, Putty, Brass, Steel, Granite, Cork, Abrasive };
}

Material wood(const std::string &name, vec3 earlywood, vec3 latewood, float ring_spacing_mm, float figure,
		float fibre_contrast);
Material metal(const std::string &name, vec3 colour, float variation, float specular, float shininess);
Material stone(const std::string &name, vec3 colour, vec3 vein, float vein_scale_mm, float vein_strength);
Material plain(const std::string &name, vec3 colour);

} // namespace sdf
