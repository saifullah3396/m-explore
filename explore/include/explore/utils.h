#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <vector>

#pragma once

namespace explore
{
cv::Rect pointsToBBox(const std::vector<std::vector<float>>& points)
{
  std::vector<cv::Point2f> contour, contour_poly;
  for (const auto& p : points) {
    auto x = p[0] < 0 ? std::floor(p[0]) : std::ceil(p[0]);
    auto y = p[1] < 0 ? std::floor(p[1]) : std::ceil(p[1]);
    contour.push_back(cv::Point2f(x, y));
  }

  cv::approxPolyDP(cv::Mat(contour), contour_poly, 3, true);
  return cv::boundingRect(cv::Mat(contour_poly));
}

}  // namespace explore