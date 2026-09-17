#include "mythos/BoxMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mythos {

BoxRect ComputeCapsuleBox(const Vec2d& top, const Vec2d& bottom,
                          double radius, double halfHeight) {
	BoxRect box;
	if (!top.finite() || !bottom.finite()) return box;
	if (!(halfHeight > 0.0) || !(radius > 0.0)) return box;

	const double height = bottom.y - top.y;
	if (!(height > 1.0)) return box;

	const double width = height * (radius / halfHeight);
	const double centerX = (top.x + bottom.x) * 0.5;

	box.left = centerX - width * 0.5;
	box.right = centerX + width * 0.5;
	box.top = top.y;
	box.bottom = bottom.y;
	box.valid = std::isfinite(box.left) && std::isfinite(box.right) &&
	            std::isfinite(box.top) && std::isfinite(box.bottom);
	return box;
}

BoxRect ComputePoseBox(const std::vector<Vec2d>& projectedPoints, int totalBones) {
	BoxRect box;
	if (totalBones < 4 || projectedPoints.size() < 4) return box;

	const size_t visible = projectedPoints.size();
	if (visible < static_cast<size_t>(totalBones) * 3 / 5) return box;

	double minY = (std::numeric_limits<double>::max)();
	double maxY = (std::numeric_limits<double>::lowest)();
	double sumX = 0.0;
	for (const Vec2d& point : projectedPoints) {
		if (!point.finite()) return BoxRect{};
		minY = (std::min)(minY, point.y);
		maxY = (std::max)(maxY, point.y);
		sumX += point.x;
	}

	const double height = maxY - minY;
	if (!(height > 2.0)) return box;

	const double centerX = sumX / static_cast<double>(visible);
	const double padY = height * 0.05;
	const double top = minY - padY;
	const double bottom = maxY + padY;
	const double width = (bottom - top) / 2.6;

	box.left = centerX - width * 0.5;
	box.right = centerX + width * 0.5;
	box.top = top;
	box.bottom = bottom;
	box.valid = true;
	return box;
}

void ScaleBox(BoxRect& box, double scale) {
	if (!box.valid || scale <= 0.0 || scale == 1.0) return;
	const double halfWidth = box.width() * 0.5 * scale;
	const double halfHeight = box.height() * 0.5 * scale;
	const double centerX = box.centerX();
	const double centerY = box.centerY();
	box.left = centerX - halfWidth;
	box.right = centerX + halfWidth;
	box.top = centerY - halfHeight;
	box.bottom = centerY + halfHeight;
}

Color4 HealthColor(float percent) {
	percent = (std::max)(0.0f, (std::min)(100.0f, percent));
	if (percent > 75.0f) return Color4{ 0.0f, 1.0f, 0.0f, 1.0f };
	if (percent > 50.0f) return Color4{ 80.0f / 255.0f, 190.0f / 255.0f, 0.0f, 1.0f };
	if (percent > 25.0f) return Color4{ 1.0f, 165.0f / 255.0f, 0.0f, 1.0f };
	return Color4{ 1.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f };
}

} // namespace mythos
