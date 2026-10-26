#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct ClothVertex {
  glm::vec3 position;
  glm::vec2 uv;
};

class ClothDemo {
 public:
  std::vector<ClothVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<unsigned char> pixels;
  glm::vec4 baseColor{1.0f};
  std::uint32_t width = 128, height = 128;

  ClothDemo() {
    for (std::uint32_t y = 0; y <= resolution_; ++y) {
      for (std::uint32_t x = 0; x <= resolution_; ++x) {
        const glm::vec2 uv = glm::vec2(x, y) / float(resolution_);
        vertices.push_back(
            {{(uv.x - 0.5f) * 2.6f, 1.3f - uv.y * 2.6f, 0.0f}, uv});
        previous_.push_back(vertices.back().position);
        if (x < resolution_ && y < resolution_) {
          const auto i = y * (resolution_ + 1) + x;
          indices.insert(indices.end(),
                         {i, i + 1, i + resolution_ + 1, i + 1,
                          i + resolution_ + 2, i + resolution_ + 1});
        }
      }
    }
    pixels.resize(width * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const bool light = ((x / 16 + y / 16) % 2) == 0;
        const auto i = (y * width + x) * 4;
        pixels[i] = light ? 220 : 35;
        pixels[i + 1] = light ? 180 : 90;
        pixels[i + 2] = light ? 105 : 125;
        pixels[i + 3] = 255;
      }
    }
    for (std::uint32_t y = 0; y <= resolution_; ++y) {
      for (std::uint32_t x = 0; x <= resolution_; ++x) {
        const auto i = y * (resolution_ + 1) + x;
        if (x < resolution_)
          AddConstraint(i, i + 1);
        if (y < resolution_)
          AddConstraint(i, i + resolution_ + 1);
        if (x < resolution_ && y < resolution_) {
          AddConstraint(i, i + resolution_ + 2);
          AddConstraint(i + 1, i + resolution_ + 1);
        }
        if (x + 2 <= resolution_)
          AddConstraint(i, i + 2);
        if (y + 2 <= resolution_)
          AddConstraint(i, i + 2 * (resolution_ + 1));
      }
    }
  }

  void Advance(float elapsed) {
    accumulator_ += elapsed;
    constexpr float step = 1.0f / 120.0f;
    while (accumulator_ >= step) {
      accumulator_ -= step;
      time_ += step;
      for (std::uint32_t i = 0; i < vertices.size(); ++i) {
        if (IsPinned(i))
          continue;
        auto &position = vertices[i].position;
        const glm::vec3 velocity = (position - previous_[i]) * 0.995f;
        previous_[i] = position;
        const float gust =
            2.0f +
            1.8f * std::sin(time_ * 1.7f + position.x * 2.0f + position.y);
        position += velocity + glm::vec3(0.4f * std::sin(time_), -9.81f, gust) *
                                   step * step;
      }
      for (int iteration = 0; iteration < 12; ++iteration) {
        for (const auto &constraint : constraints_) {
          auto &a = vertices[constraint.a].position;
          auto &b = vertices[constraint.b].position;
          const glm::vec3 delta = b - a;
          const float length = glm::length(delta);
          if (length < 0.000001f)
            continue;
          const float weightA = IsPinned(constraint.a) ? 0.0f : 1.0f;
          const float weightB = IsPinned(constraint.b) ? 0.0f : 1.0f;
          if (weightA + weightB == 0.0f)
            continue;
          const glm::vec3 correction = delta * ((length - constraint.length) /
                                                length / (weightA + weightB));
          a += correction * weightA;
          b -= correction * weightB;
        }
      }
    }
  }

 private:
  struct Constraint {
    std::uint32_t a, b;
    float length;
  };
  static constexpr std::uint32_t resolution_ = 32;
  std::vector<glm::vec3> previous_;
  std::vector<Constraint> constraints_;
  float accumulator_ = 0.0f;
  float time_ = 0.0f;

  static bool IsPinned(std::uint32_t i) { return i == 0 || i == resolution_; }
  void AddConstraint(std::uint32_t a, std::uint32_t b) {
    constraints_.push_back(
        {a, b, glm::distance(vertices[a].position, vertices[b].position)});
  }
};
