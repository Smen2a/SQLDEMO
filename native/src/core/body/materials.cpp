#include "body/materials.h"

namespace sdf {

std::uint16_t MaterialTable::add(const Material &m) {
	materials_.push_back(m);
	return std::uint16_t(materials_.size() - 1);
}

vec3 MaterialTable::albedo(float id, vec3 p, vec3 grain_origin, vec3 grain_axis) const {
	const Material &m = materials_[std::size_t(id)];
	return gl::sdf_material_albedo(p, m.k[0], m.k[1], m.k[2], vec4(grain_origin, 0), vec4(grain_axis, 0));
}

Material wood(const std::string &name, vec3 earlywood, vec3 latewood, float ring_spacing_mm, float figure,
		float fibre_contrast) {
	Material m;
	m.name = name;
	m.k[0] = vec4(float(gl::SDF_MAT_WOOD), ring_spacing_mm, figure, fibre_contrast);
	m.k[1] = vec4(earlywood, 0);
	m.k[2] = vec4(latewood, 0);
	m.specular = 0.06f;
	m.shininess = 18.0f;
	return m;
}

Material metal(const std::string &name, vec3 colour, float variation, float specular, float shininess) {
	Material m;
	m.name = name;
	m.k[0] = vec4(float(gl::SDF_MAT_METAL), variation, 0, 0);
	m.k[1] = vec4(colour, 0);
	m.specular = specular;
	m.shininess = shininess;
	return m;
}

Material stone(const std::string &name, vec3 colour, vec3 vein, float vein_scale_mm, float vein_strength) {
	Material m;
	m.name = name;
	m.k[0] = vec4(float(gl::SDF_MAT_STONE), vein_scale_mm, vein_strength, 0);
	m.k[1] = vec4(colour, 0);
	m.k[2] = vec4(vein, 0);
	m.specular = 0.08f;
	m.shininess = 30.0f;
	return m;
}

Material plain(const std::string &name, vec3 colour) {
	Material m;
	m.name = name;
	m.k[0] = vec4(float(gl::SDF_MAT_PLAIN), 0, 0, 0);
	m.k[1] = vec4(colour, 0);
	return m;
}

MaterialTable MaterialTable::standard() {
	MaterialTable t;
	// Densities in g/cm^3: seasoned timber, cast and wrought metal, stone.
	auto with_density = [](Material m, float density) {
		m.density = density;
		return m;
	};
	t.add(with_density(wood("ash", {0.86f, 0.77f, 0.61f}, {0.70f, 0.56f, 0.39f}, 3.2f, 0.35f, 0.22f), 0.67f));
	t.add(with_density(wood("oak", {0.78f, 0.63f, 0.43f}, {0.58f, 0.43f, 0.27f}, 2.4f, 0.25f, 0.30f), 0.75f));
	t.add(with_density(wood("walnut", {0.44f, 0.31f, 0.21f}, {0.28f, 0.18f, 0.12f}, 2.8f, 0.30f, 0.25f), 0.64f));
	t.add(with_density(plain("putty", {0.86f, 0.84f, 0.79f}), 1.7f));
	t.add(with_density(metal("brass", {0.80f, 0.63f, 0.28f}, 0.18f, 0.55f, 40.0f), 8.5f));
	t.add(with_density(metal("steel", {0.56f, 0.58f, 0.60f}, 0.12f, 0.45f, 60.0f), 7.85f));
	t.add(with_density(stone("granite", {0.55f, 0.53f, 0.50f}, {0.26f, 0.25f, 0.24f}, 6.0f, 0.55f), 2.7f));
	// Speckled like stone, at the scale of cork granules and abrasive grit.
	Material cork = stone("cork", {0.66f, 0.50f, 0.34f}, {0.42f, 0.30f, 0.19f}, 0.6f, 0.35f);
	cork.specular = 0.02f;
	t.add(with_density(cork, 0.24f));
	Material abrasive = stone("abrasive", {0.36f, 0.20f, 0.13f}, {0.12f, 0.08f, 0.06f}, 0.12f, 0.45f);
	abrasive.specular = 0.03f;
	t.add(with_density(abrasive, 2.0f));
	return t;
}

} // namespace sdf
