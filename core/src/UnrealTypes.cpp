#include "mythos/UnrealTypes.hpp"

namespace mythos {

FQuat QuatMultiply(const FQuat& a, const FQuat& b) {
	FQuat result;
	result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return result;
}

bool TransformLooksSane(const FTransform& transform) {
	const FQuat& q = transform.rotation;
	const double quaternionNorm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	if (!std::isfinite(quaternionNorm)) return false;
	if (quaternionNorm < 0.5 || quaternionNorm > 2.0) return false;

	const double sx = std::fabs(transform.scale.x);
	const double sy = std::fabs(transform.scale.y);
	const double sz = std::fabs(transform.scale.z);
	if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz)) return false;
	if (sx < 1e-4 || sy < 1e-4 || sz < 1e-4) return false;
	if (sx > 100.0 || sy > 100.0 || sz > 100.0) return false;

	const FVector& t = transform.translation;
	if (!t.finite()) return false;
	if (std::fabs(t.x) > kMaxWorldCoordCm) return false;
	if (std::fabs(t.y) > kMaxWorldCoordCm) return false;
	if (std::fabs(t.z) > kMaxWorldCoordCm) return false;
	return true;
}

} // namespace mythos
