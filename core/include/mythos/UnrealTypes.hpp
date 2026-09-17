// ============================================================================
// Unreal Engine 5 (LWC) value types used by the read-only model.
// Layouts mirror the verified offsets in Offsets.hpp and are pinned with
// static_assert so a compiler change can never silently shift them.
// ============================================================================
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mythos {

struct Vec2d {
	double x = 0.0;
	double y = 0.0;

	[[nodiscard]] bool finite() const { return std::isfinite(x) && std::isfinite(y); }
};

struct FVector {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	[[nodiscard]] double dot(const FVector& other) const {
		return x * other.x + y * other.y + z * other.z;
	}

	[[nodiscard]] double length() const {
		return std::sqrt(x * x + y * y + z * z);
	}

	[[nodiscard]] double distance(const FVector& other) const {
		return (*this - other).length();
	}

	[[nodiscard]] bool finite() const {
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}

	FVector operator+(const FVector& other) const { return { x + other.x, y + other.y, z + other.z }; }
	FVector operator-(const FVector& other) const { return { x - other.x, y - other.y, z - other.z }; }
	FVector operator*(double scalar) const { return { x * scalar, y * scalar, z * scalar }; }
	FVector operator/(double scalar) const { return { x / scalar, y / scalar, z / scalar }; }
};

struct FRotator {
	double pitch = 0.0; // rotation around Y (degrees)
	double yaw = 0.0;   // rotation around Z (degrees)
	double roll = 0.0;  // rotation around X (degrees)

	[[nodiscard]] bool finite() const {
		return std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll);
	}
};

struct FQuat {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double w = 1.0;
};

// UE5 FTransform: FQuat, FVector, pad, FVector, pad == 0x60 bytes.
struct FTransform {
	FQuat   rotation;
	FVector translation;
	uint8_t pad0[8] = {};
	FVector scale{ 1.0, 1.0, 1.0 };
	uint8_t pad1[8] = {};

	[[nodiscard]] FVector transformPosition(const FVector& point) const {
		const double x2 = rotation.x + rotation.x;
		const double y2 = rotation.y + rotation.y;
		const double z2 = rotation.z + rotation.z;
		const double xx2 = rotation.x * x2;
		const double yy2 = rotation.y * y2;
		const double zz2 = rotation.z * z2;
		const double xy2 = rotation.x * y2;
		const double xz2 = rotation.x * z2;
		const double yz2 = rotation.y * z2;
		const double wx2 = rotation.w * x2;
		const double wy2 = rotation.w * y2;
		const double wz2 = rotation.w * z2;

		const double sx = point.x * scale.x;
		const double sy = point.y * scale.y;
		const double sz = point.z * scale.z;

		return FVector{
			sx * (1.0 - (yy2 + zz2)) + sy * (xy2 - wz2) + sz * (xz2 + wy2) + translation.x,
			sx * (xy2 + wz2) + sy * (1.0 - (xx2 + zz2)) + sz * (yz2 - wx2) + translation.y,
			sx * (xz2 - wy2) + sy * (yz2 + wx2) + sz * (1.0 - (xx2 + yy2)) + translation.z,
		};
	}
};

static_assert(sizeof(FQuat) == 0x20, "FQuat must be four doubles");
static_assert(sizeof(FVector) == 24, "UE5 FVector is three doubles");
static_assert(sizeof(FTransform) == 0x60, "FTransform must be 0x60 bytes (UE5 LWC)");
static_assert(offsetof(FTransform, translation) == 0x20, "Translation must be at 0x20");
static_assert(offsetof(FTransform, scale) == 0x40, "Scale3D must be at 0x40");

// UE FName: comparison index (and number) resolved through the FNamePool.
struct FNameRef {
	uint32_t comparisonIndex = 0;
	uint32_t number = 0;
};
static_assert(sizeof(FNameRef) == 8, "FName is 8 bytes");

[[nodiscard]] FQuat QuatMultiply(const FQuat& a, const FQuat& b);

// Rejects transforms with non-finite, non-unit quaternion, degenerate scale or
// absurd world coordinates.
[[nodiscard]] bool TransformLooksSane(const FTransform& transform);

// Screen-space helpers used by skeleton/box math.
[[nodiscard]] inline bool IsOnScreen(const Vec2d& point, double width, double height) {
	return point.x >= 0.0 && point.y >= 0.0 && point.x <= width && point.y <= height;
}

// Height cap for pose reads; matches the legacy renderer budget.
inline constexpr int kMaxDrawBones = 256;

// World coordinate sanity cap (centimetres).
inline constexpr double kMaxWorldCoordCm = 1.0e7;

// Maximum distance (cm) between a pawn root and a skeletal mesh for the mesh to
// be considered part of the pawn.
inline constexpr double kMaxMeshOffsetCm = 500.0;

} // namespace mythos
