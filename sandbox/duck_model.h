#pragma once

#define CGLTF_IMPLEMENTATION
#include "../external/meshoptimizer/extern/cgltf.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../external/velos/external/stb/stb_image.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

struct DuckVertex {
  glm::vec3 position;
  glm::vec2 uv;
};

struct DuckModel {
  std::vector<DuckVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<unsigned char> pixels;
  glm::vec4 baseColor{1.0f};
  uint32_t width = 0, height = 0;
};

inline DuckModel LoadDuckModel() {
  constexpr const char *path = "assets/models/Duck.glb";
  cgltf_options options{};
  cgltf_data *parsed = nullptr;
  if (cgltf_parse_file(&options, path, &parsed) != cgltf_result_success)
    throw std::runtime_error(
        "Cannot parse assets/models/Duck.glb; run Sandbox from the repository "
        "root");
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
  if (cgltf_load_buffers(&options, data.get(), path) != cgltf_result_success ||
      cgltf_validate(data.get()) != cgltf_result_success)
    throw std::runtime_error("Invalid Duck.glb buffers");
  DuckModel result;
  const cgltf_material *material = nullptr;
  glm::vec3 minimum(std::numeric_limits<float>::max()),
      maximum(std::numeric_limits<float>::lowest());
  if (!data->scene)
    throw std::runtime_error("Duck.glb has no default scene");
  const auto visit = [&](const auto &self, const cgltf_node *node) -> void {
    if (node->mesh) {
      glm::mat4 world;
      cgltf_node_transform_world(node, &world[0][0]);
      for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
        const auto &primitive = node->mesh->primitives[p];
        if (primitive.type != cgltf_primitive_type_triangles ||
            !primitive.indices || !primitive.material)
          throw std::runtime_error(
              "Duck requires indexed triangles and a material");
        if (material && material != primitive.material)
          throw std::runtime_error("Sandbox duck loader expects one material");
        material = primitive.material;
        const cgltf_accessor *positions = nullptr, *uvs = nullptr;
        const int texcoord =
            material->pbr_metallic_roughness.base_color_texture.texcoord;
        for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
          const auto &attribute = primitive.attributes[a];
          if (attribute.type == cgltf_attribute_type_position)
            positions = attribute.data;
          if (attribute.type == cgltf_attribute_type_texcoord &&
              attribute.index == texcoord)
            uvs = attribute.data;
        }
        if (!positions || !uvs || positions->count != uvs->count)
          throw std::runtime_error(
              "Duck requires matching positions and base-color UVs");
        const auto first = static_cast<uint32_t>(result.vertices.size());
        for (cgltf_size i = 0; i < positions->count; ++i) {
          DuckVertex vertex{};
          if (!cgltf_accessor_read_float(positions, i, &vertex.position.x, 3) ||
              !cgltf_accessor_read_float(uvs, i, &vertex.uv.x, 2))
            throw std::runtime_error("Cannot decode Duck vertex attributes");
          vertex.position = glm::vec3(world * glm::vec4(vertex.position, 1.0f));
          minimum = glm::min(minimum, vertex.position);
          maximum = glm::max(maximum, vertex.position);
          result.vertices.push_back(vertex);
        }
        for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
          const auto index = cgltf_accessor_read_index(primitive.indices, i);
          if (index >= positions->count)
            throw std::runtime_error("Duck index is out of range");
          result.indices.push_back(first + static_cast<uint32_t>(index));
        }
      }
    }
    for (cgltf_size i = 0; i < node->children_count; ++i)
      self(self, node->children[i]);
  };
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
    visit(visit, data->scene->nodes[i]);
  if (result.vertices.empty() || result.indices.empty() || !material)
    throw std::runtime_error("Duck scene contains no drawable mesh");
  const glm::vec3 center = (minimum + maximum) * 0.5f;
  const auto extent = maximum - minimum;
  const float scale = 2.0f / std::max({extent.x, extent.y, extent.z});
  for (auto &vertex : result.vertices)
    vertex.position = (vertex.position - center) * scale;
  const auto &pbr = material->pbr_metallic_roughness;
  result.baseColor =
      glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                pbr.base_color_factor[2], pbr.base_color_factor[3]);
  const auto *texture = pbr.base_color_texture.texture;
  const auto *image = texture ? texture->image : nullptr;
  if (!image || !image->buffer_view)
    throw std::runtime_error("Duck base-color texture must be embedded");
  const auto *encoded = cgltf_buffer_view_data(image->buffer_view);
  int width = 0, height = 0, channels = 0;
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
      stbi_load_from_memory(encoded, static_cast<int>(image->buffer_view->size),
                            &width, &height, &channels, 4),
      stbi_image_free);
  if (!pixels)
    throw std::runtime_error("Cannot decode Duck base-color texture");
  result.width = static_cast<uint32_t>(width);
  result.height = static_cast<uint32_t>(height);
  result.pixels.assign(pixels.get(),
                       pixels.get() + static_cast<size_t>(width) * height * 4);
  return result;
}
