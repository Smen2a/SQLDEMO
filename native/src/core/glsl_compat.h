#pragma once

// A GLSL-shaped subset of C++, just large enough to compile the files in shared/.
// Those files are also included verbatim by Godot shaders, so the CPU evaluator and
// the GPU raymarcher run the same formulas. Anything the shared files need must exist
// here with GLSL semantics; anything GLSL lacks (overloading on user functions,
// swizzles, references) must not be used there.

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace sdf::gl {

using uint = std::uint32_t;

struct vec2 {
	float x = 0.0f, y = 0.0f;
	vec2() = default;
	explicit vec2(float s) : x(s), y(s) {}
	vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct vec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	vec3() = default;
	explicit vec3(float s) : x(s), y(s), z(s) {}
	vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
	vec3(vec2 v, float z_) : x(v.x), y(v.y), z(z_) {}
};

struct vec4 {
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
	vec4() = default;
	explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
	vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
	vec4(vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
	vec4(float x_, vec3 v) : x(x_), y(v.x), z(v.y), w(v.z) {}
};

// --- component-wise operators --------------------------------------------------------

#define SDF_VEC_OPS(V, APPLY)                                                              \
	inline V operator+(V a, V b) { return APPLY(a, b, +); }                                \
	inline V operator-(V a, V b) { return APPLY(a, b, -); }                                \
	inline V operator*(V a, V b) { return APPLY(a, b, *); }                                \
	inline V operator/(V a, V b) { return APPLY(a, b, /); }                                \
	inline V operator+(V a, float s) { return a + V(s); }                                  \
	inline V operator-(V a, float s) { return a - V(s); }                                  \
	inline V operator*(V a, float s) { return a * V(s); }                                  \
	inline V operator/(V a, float s) { return a / V(s); }                                  \
	inline V operator+(float s, V a) { return V(s) + a; }                                  \
	inline V operator-(float s, V a) { return V(s) - a; }                                  \
	inline V operator*(float s, V a) { return V(s) * a; }                                  \
	inline V operator/(float s, V a) { return V(s) / a; }                                  \
	inline V &operator+=(V &a, V b) { return a = a + b; }                                  \
	inline V &operator-=(V &a, V b) { return a = a - b; }                                  \
	inline V &operator*=(V &a, V b) { return a = a * b; }                                  \
	inline V &operator*=(V &a, float s) { return a = a * s; }                              \
	inline V &operator/=(V &a, float s) { return a = a / s; }

#define SDF_APPLY2(a, b, op) vec2(a.x op b.x, a.y op b.y)
#define SDF_APPLY3(a, b, op) vec3(a.x op b.x, a.y op b.y, a.z op b.z)
#define SDF_APPLY4(a, b, op) vec4(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w)
SDF_VEC_OPS(vec2, SDF_APPLY2)
SDF_VEC_OPS(vec3, SDF_APPLY3)
SDF_VEC_OPS(vec4, SDF_APPLY4)
#undef SDF_VEC_OPS
#undef SDF_APPLY2
#undef SDF_APPLY3
#undef SDF_APPLY4

inline vec2 operator-(vec2 a) { return vec2(-a.x, -a.y); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec4 operator-(vec4 a) { return vec4(-a.x, -a.y, -a.z, -a.w); }

// --- scalar built-ins ------------------------------------------------------------------
// Templated so that mixed float/double/int arguments (C++ literals are double) resolve
// the way GLSL does: integer arguments stay integer, anything else becomes float.

template <class T>
using if_num = std::enable_if_t<std::is_arithmetic_v<T>, int>;
template <class A, class B>
using num_t = std::conditional_t<std::is_integral_v<A> && std::is_integral_v<B>, int, float>;

template <class A, class B, if_num<A> = 0, if_num<B> = 0>
inline num_t<A, B> min(A a, B b) {
	using R = num_t<A, B>;
	return R(a) < R(b) ? R(a) : R(b);
}
template <class A, class B, if_num<A> = 0, if_num<B> = 0>
inline num_t<A, B> max(A a, B b) {
	using R = num_t<A, B>;
	return R(a) > R(b) ? R(a) : R(b);
}
template <class X, class L, class H, if_num<X> = 0, if_num<L> = 0, if_num<H> = 0>
inline float clamp(X x, L lo, H hi) { return min(max(float(x), float(lo)), float(hi)); }
template <class X, if_num<X> = 0>
inline num_t<X, X> abs(X x) { return x < 0 ? num_t<X, X>(-x) : num_t<X, X>(x); }
template <class X, if_num<X> = 0>
inline float sign(X x) { return x > 0 ? 1.0f : (x < 0 ? -1.0f : 0.0f); }

#define SDF_UNARY(name, expr)                                                              \
	template <class X, if_num<X> = 0>                                                      \
	inline float name(X x_) { const float x = float(x_); return expr; }
SDF_UNARY(sqrt, std::sqrt(x))
SDF_UNARY(inversesqrt, 1.0f / std::sqrt(x))
SDF_UNARY(floor, std::floor(x))
SDF_UNARY(ceil, std::ceil(x))
SDF_UNARY(fract, x - std::floor(x))
SDF_UNARY(exp, std::exp(x))
SDF_UNARY(exp2, std::exp2(x))
SDF_UNARY(log, std::log(x))
SDF_UNARY(log2, std::log2(x))
SDF_UNARY(sin, std::sin(x))
SDF_UNARY(cos, std::cos(x))
SDF_UNARY(tan, std::tan(x))
SDF_UNARY(asin, std::asin(x))
SDF_UNARY(acos, std::acos(x))
SDF_UNARY(radians, x * 0.017453292519943295f)
#undef SDF_UNARY

template <class A, class B, if_num<A> = 0, if_num<B> = 0>
inline float pow(A a, B b) { return std::pow(float(a), float(b)); }
template <class A, class B, if_num<A> = 0, if_num<B> = 0>
inline float atan(A y, B x) { return std::atan2(float(y), float(x)); }
template <class A, class B, if_num<A> = 0, if_num<B> = 0>
inline float mod(A a, B b) { return float(a) - float(b) * std::floor(float(a) / float(b)); }
template <class A, class B, class T, if_num<A> = 0, if_num<B> = 0, if_num<T> = 0>
inline float mix(A a, B b, T t) { return float(a) + (float(b) - float(a)) * float(t); }
template <class E, class X, if_num<E> = 0, if_num<X> = 0>
inline float step(E edge, X x) { return float(x) < float(edge) ? 0.0f : 1.0f; }
template <class A, class B, class X, if_num<A> = 0, if_num<B> = 0, if_num<X> = 0>
inline float smoothstep(A e0, B e1, X x) {
	const float t = clamp((float(x) - float(e0)) / (float(e1) - float(e0)), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// --- vector built-ins --------------------------------------------------------------------

inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float length(vec2 a) { return std::sqrt(dot(a, a)); }
inline float length(vec3 a) { return std::sqrt(dot(a, a)); }
inline float length(vec4 a) { return std::sqrt(dot(a, a)); }
inline float distance(vec3 a, vec3 b) { return length(a - b); }
inline vec2 normalize(vec2 a) { return a / length(a); }
inline vec3 normalize(vec3 a) { return a / length(a); }
inline vec3 cross(vec3 a, vec3 b) {
	return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

#define SDF_MAP2(f, a) vec2(f(a.x), f(a.y))
#define SDF_MAP3(f, a) vec3(f(a.x), f(a.y), f(a.z))
#define SDF_MAP4(f, a) vec4(f(a.x), f(a.y), f(a.z), f(a.w))
#define SDF_VEC_UNARY(name)                                                                \
	inline vec2 name(vec2 a) { return SDF_MAP2(name, a); }                                 \
	inline vec3 name(vec3 a) { return SDF_MAP3(name, a); }                                 \
	inline vec4 name(vec4 a) { return SDF_MAP4(name, a); }
SDF_VEC_UNARY(abs)
SDF_VEC_UNARY(sign)
SDF_VEC_UNARY(floor)
SDF_VEC_UNARY(fract)
SDF_VEC_UNARY(sqrt)
SDF_VEC_UNARY(sin)
SDF_VEC_UNARY(cos)
#undef SDF_VEC_UNARY

#define SDF_VEC_BINARY(name)                                                               \
	inline vec2 name(vec2 a, vec2 b) { return vec2(name(a.x, b.x), name(a.y, b.y)); }      \
	inline vec3 name(vec3 a, vec3 b) {                                                     \
		return vec3(name(a.x, b.x), name(a.y, b.y), name(a.z, b.z));                       \
	}                                                                                      \
	inline vec4 name(vec4 a, vec4 b) {                                                     \
		return vec4(name(a.x, b.x), name(a.y, b.y), name(a.z, b.z), name(a.w, b.w));       \
	}                                                                                      \
	inline vec2 name(vec2 a, float s) { return name(a, vec2(s)); }                         \
	inline vec3 name(vec3 a, float s) { return name(a, vec3(s)); }                         \
	inline vec4 name(vec4 a, float s) { return name(a, vec4(s)); }
SDF_VEC_BINARY(min)
SDF_VEC_BINARY(max)
SDF_VEC_BINARY(mod)
#undef SDF_VEC_BINARY

inline vec2 clamp(vec2 x, float lo, float hi) { return min(max(x, lo), hi); }
inline vec3 clamp(vec3 x, float lo, float hi) { return min(max(x, lo), hi); }
inline vec2 mix(vec2 a, vec2 b, float t) { return a + (b - a) * t; }
inline vec3 mix(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline vec4 mix(vec4 a, vec4 b, float t) { return a + (b - a) * t; }
inline vec3 mix(vec3 a, vec3 b, vec3 t) { return a + (b - a) * t; }
#undef SDF_MAP2
#undef SDF_MAP3
#undef SDF_MAP4

} // namespace sdf::gl

// Shared-file conventions. Godot shaders define these differently before their includes.
#define SDF_FN inline
#define OUT(T) T &
#define INOUT(T) T &
