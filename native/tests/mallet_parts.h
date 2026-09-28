#pragma once

#include "plans/part.h"

// The mallet's parts (game/plans/mallet.json), for the tests.
namespace mallet_parts {

using sdf::plans::Face;
using sdf::plans::Feature;
using sdf::plans::Part;

inline Part head() {
	Part p;
	p.id = "head";
	p.size = {110, 70, 55};
	Feature mortise;
	mortise.kind = Feature::Kind::Hole;
	mortise.name = "mortise";
	mortise.face = Face::Side;
	mortise.along = {40, 70};
	mortise.across = {29, 41};
	mortise.through = true;
	p.features.push_back(mortise);
	return p;
}

inline Part handle() {
	Part p;
	p.id = "handle";
	p.size = {300, 35, 28};
	Feature tenon;
	tenon.kind = Feature::Kind::Tenon;
	tenon.name = "tenon";
	tenon.length = 58;
	tenon.y = {2.5f, 32.5f};
	tenon.z = {8, 20};
	p.features.push_back(tenon);
	Feature kerf;
	kerf.kind = Feature::Kind::Kerf;
	kerf.name = "wedge kerf";
	kerf.depth = 38;
	kerf.axis = 1;
	kerf.at = 17.5f;
	kerf.width = 0.8f;
	p.features.push_back(kerf);
	return p;
}

inline Part wedge() {
	Part p;
	p.id = "wedge";
	p.size = {45, 12, 5};
	Feature taper;
	taper.kind = Feature::Kind::Taper;
	taper.name = "taper";
	taper.face = Face::Back;
	taper.from = 5;
	taper.to = 1;
	p.features.push_back(taper);
	return p;
}

} // namespace mallet_parts
