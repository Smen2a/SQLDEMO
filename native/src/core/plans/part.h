#pragma once

#include "body/body.h"

#include <cstdint>
#include <string>
#include <vector>

// Plans: what a part of an object is meant to be, and the lines that lay it out on the wood.
// A part is a rectangular blank with features cut into it (a mortise, a tenon, a kerf, a
// rebate, a chamfer, a taper). Each feature is both lines to lay out (where a woodworker
// would knife or gauge them) and material the part's intended solid lacks.
//
// Part space (millimetres): the blank is [0,L] x [0,W] x [0,T], its reference corner at the
// origin where the three reference faces meet: the reference end (x = 0), the face edge
// (y = 0) and the face side (z = 0). x runs along the grain.
namespace sdf::plans {

enum class Face : std::uint8_t { Side, Back, Edge, OtherEdge, End, FarEnd };

// Its outward normal, in part space.
vec3 outward(Face f);
// The axis square to it (0: x, 1: y, 2: z), and where it lies along that axis on a blank of
// `size`.
int axis_of(Face f);
float position(Face f, vec3 size);

struct Feature {
	enum class Kind : std::uint8_t { Hole, Tenon, Kerf, Rebate, Chamfer, Taper };
	Kind kind = Kind::Hole;
	std::string name;
	// Hole: the face it is cut into, its x range and its range across the face (y on the
	// side and back, z on the edges), and how deep (through: the whole way).
	// Rebate: the face it is cut from and `other` (the face at the edge it runs along); its
	// width on `face` and depth on `other` from the edge between them; its x range.
	// Chamfer: the edge between `face` and `other`, its width on both, its x range.
	// Taper: the face it is taken off (side or back), the thickness at the reference end
	// (`from`) and at the far end (`to`).
	Face face = Face::Side;
	Face other = Face::Edge;
	vec2 along{0.0f, 0.0f}; // an x range; {0, 0}: the whole length
	vec2 across{0.0f, 0.0f};
	float depth = 0.0f; // Hole (not through), Rebate, Kerf (from the end)
	bool through = false;
	float width = 0.0f; // Rebate, Chamfer, Kerf (its kerf: 0.8)
	// Tenon, Kerf: at the far end (else the reference end). Tenon: its length and section.
	bool far = false;
	float length = 0.0f;
	vec2 y{0.0f, 0.0f}, z{0.0f, 0.0f};
	// Kerf: square to y (1) or z (2), at.
	int axis = 1;
	float at = 0.0f;
	// Taper.
	float from = 0.0f, to = 0.0f;
};

struct Part {
	std::string id, name;
	vec3 size{0.0f}; // L, W, T
	std::vector<Feature> features;
};

// A line laid out on a face of the blank. `as` is how it holds the tools once knifed or
// gauged in (see game/workshop/layout.gd hold()):
//   Knife  a wall with the waste on the side the stroke is on;
//   Gauge  gauged `distance` mm from the edge towards `toward`: the waste between;
//   Guide  a pencil line only (a taper's slope): nothing holds to it.
struct PlanLine {
	enum class As : std::uint8_t { Knife, Gauge, Guide };
	As as = As::Knife;
	vec3 face{0.0f};   // the face's outward normal
	vec3 origin{0.0f}; // from origin along dir, length mm
	vec3 dir{1.0f, 0.0f, 0.0f};
	float length = 0.0f;
	vec3 toward{0.0f}; // in the face, square to the line (Gauge: towards the edge it is gauged from)
	float distance = 0.0f;
	std::string feature; // the feature it lays out ("" for the part's size)
};

struct PartLines {
	std::vector<PlanLine> lines;
	int later = 0; // lines on faces the stock does not have yet (it is bigger that way)
};

// The lines that lay `part` out on a piece of stock `stock` mm (its size along the part's
// axes, at least the part's), the part flush with the stock at the reference corner: the
// part's size (knifed to length round the stock, gauged to width and thickness from its far
// faces), then its features, on the faces the stock has there.
PartLines part_lines(const Part &part, vec3 stock);

// The part as it is meant to be (part space): the blank, its arrises eased by a millimetre
// as stock is, less its features. `owners`, if given, gets each edit's feature (its index).
// Cuts run `past` mm beyond the blank's faces: a millimetre is enough to cut, while a
// distance to the drawing's surface from inside a cut is exact only where the cut's box runs
// well past it (checking).
Body part_solid(const Part &part, std::uint16_t material, std::vector<int> *owners = nullptr, float past = 1.0f);

} // namespace sdf::plans
