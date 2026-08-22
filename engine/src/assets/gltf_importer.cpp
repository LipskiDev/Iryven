#include "model_importers.h"

#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>

#include <stb_image.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

namespace Iryven::Importers {

	ModelHandle ImportGltf(const std::filesystem::path& path) {
		constexpr fastgltf::Extensions extensions =
			fastgltf::Extensions::KHR_mesh_quantization;
		constexpr fastgltf::Options options =
			fastgltf::Options::LoadExternalBuffers |
			fastgltf::Options::GenerateMeshIndices;

		auto file = fastgltf::MappedGltfFile::FromPath(path);
		if (!file) {
			throw std::runtime_error(
				"Could not open glTF model '" + path.string() + "': " +
				std::string(fastgltf::getErrorMessage(file.error())));
		}

		fastgltf::Parser parser(extensions);
		auto loaded = parser.loadGltf(file.get(), path.parent_path(), options);
		if (!loaded) {
			throw std::runtime_error(
				"Could not parse glTF model '" + path.string() + "': " +
				std::string(fastgltf::getErrorMessage(loaded.error())));
		}
		fastgltf::Asset& asset = loaded.get();
		Model result;
		result.source = path;

		const auto requireU32 = [&path](std::size_t value, std::string_view what) {
			if (value > std::numeric_limits<std::uint32_t>::max())
				throw std::runtime_error("glTF " + std::string(what) + " exceeds 32-bit limits in '" + path.string() + "'");
			return static_cast<std::uint32_t>(value);
		};

		const auto convertFilter = [](fastgltf::Filter filter) {
			switch (filter) {
			case fastgltf::Filter::Nearest: return TextureFilter::Nearest;
			case fastgltf::Filter::Linear: return TextureFilter::Linear;
			case fastgltf::Filter::NearestMipMapNearest: return TextureFilter::NearestMipmapNearest;
			case fastgltf::Filter::LinearMipMapNearest: return TextureFilter::LinearMipmapNearest;
			case fastgltf::Filter::NearestMipMapLinear: return TextureFilter::NearestMipmapLinear;
			case fastgltf::Filter::LinearMipMapLinear: return TextureFilter::LinearMipmapLinear;
			}
			return TextureFilter::Linear;
		};
		const auto convertWrap = [](fastgltf::Wrap wrap) {
			switch (wrap) {
			case fastgltf::Wrap::ClampToEdge: return TextureWrap::ClampToEdge;
			case fastgltf::Wrap::MirroredRepeat: return TextureWrap::MirroredRepeat;
			case fastgltf::Wrap::Repeat: return TextureWrap::Repeat;
			}
			return TextureWrap::Repeat;
		};
		result.textureRegistry.samplers.reserve(asset.samplers.size() + 1);
		for (const fastgltf::Sampler& source : asset.samplers) {
			const fastgltf::Filter minFilter = source.minFilter.value_or(
				fastgltf::Filter::LinearMipMapLinear);
			result.textureRegistry.samplers.push_back(TextureSampler{
				.minFilter = convertFilter(minFilter),
				.magFilter = convertFilter(source.magFilter.value_or(fastgltf::Filter::Linear)),
				.wrapU = convertWrap(source.wrapS),
				.wrapV = convertWrap(source.wrapT),
				.generateMipmaps = minFilter != fastgltf::Filter::Nearest &&
					minFilter != fastgltf::Filter::Linear,
			});
		}
		const std::uint32_t defaultSamplerIndex = requireU32(
			result.textureRegistry.samplers.size(), "default sampler index");
		result.textureRegistry.samplers.emplace_back();

		const auto decodeBytes = [&path](
			const std::byte* encoded, std::size_t encodedSize,
			std::filesystem::path source, TextureColorSpace colorSpace) -> TextureHandle {
			if (!encoded || encodedSize == 0 ||
				encodedSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
				throw std::runtime_error("glTF image has an invalid encoded size in '" + path.string() + "'");
			int width = 0;
			int height = 0;
			int channels = 0;
			stbi_uc* decoded = stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(encoded), static_cast<int>(encodedSize),
				&width, &height, &channels, STBI_rgb_alpha);
			if (!decoded)
				throw std::runtime_error("Could not decode glTF image in '" + path.string() + "': " +
					(stbi_failure_reason() ? stbi_failure_reason() : "unknown image error"));
			if (width <= 0 || height <= 0) {
				stbi_image_free(decoded);
				throw std::runtime_error("glTF image has invalid dimensions in '" + path.string() + "'");
			}
			const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
			std::vector<std::uint8_t> pixels(decoded, decoded + byteCount);
			stbi_image_free(decoded);
			return std::make_shared<const Texture>(Texture{
				.source = std::move(source),
				.width = static_cast<std::uint32_t>(width),
				.height = static_cast<std::uint32_t>(height),
				.format = TextureFormat::RGBA8,
				.colorSpace = colorSpace,
				.pixels = std::move(pixels),
			});
		};

		const auto decodeImage = [&](std::size_t imageIndex, TextureColorSpace colorSpace) -> TextureHandle {
			if (imageIndex >= asset.images.size())
				throw std::runtime_error("glTF texture image index is out of range in '" + path.string() + "'");
			const std::filesystem::path syntheticSource = path.string() +
				"#image-" + std::to_string(imageIndex);
			return std::visit(fastgltf::visitor{
				[&](const fastgltf::sources::Array& bytes) -> TextureHandle {
					return decodeBytes(bytes.bytes.data(), bytes.bytes.size(), syntheticSource, colorSpace);
				},
				[&](const fastgltf::sources::Vector& bytes) -> TextureHandle {
					return decodeBytes(bytes.bytes.data(), bytes.bytes.size(), syntheticSource, colorSpace);
				},
				[&](const fastgltf::sources::ByteView& bytes) -> TextureHandle {
					return decodeBytes(bytes.bytes.data(), bytes.bytes.size(), syntheticSource, colorSpace);
				},
				[&](const fastgltf::sources::BufferView& view) -> TextureHandle {
					const auto bytes = fastgltf::DefaultBufferDataAdapter{}(asset, view.bufferViewIndex);
					return decodeBytes(bytes.data(), bytes.size(), syntheticSource, colorSpace);
				},
				[&](const fastgltf::sources::URI& uri) -> TextureHandle {
					if (!uri.uri.isLocalPath() || uri.fileByteOffset != 0)
						throw std::runtime_error("Unsupported glTF image URI in '" + path.string() + "'");
					const std::string relativePath(uri.uri.path().begin(), uri.uri.path().end());
					const std::filesystem::path imagePath = path.parent_path() / relativePath;
					int width = 0;
					int height = 0;
					int channels = 0;
					stbi_uc* decoded = stbi_load(imagePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
					if (!decoded)
						throw std::runtime_error("Could not decode glTF image '" + imagePath.string() + "'");
					const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
					std::vector<std::uint8_t> pixels(decoded, decoded + byteCount);
					stbi_image_free(decoded);
					return std::make_shared<const Texture>(Texture{
						.source = imagePath,
						.width = static_cast<std::uint32_t>(width),
						.height = static_cast<std::uint32_t>(height),
						.format = TextureFormat::RGBA8,
						.colorSpace = colorSpace,
						.pixels = std::move(pixels),
					});
				},
				[&](const auto&) -> TextureHandle {
					throw std::runtime_error("Unsupported glTF image storage in '" + path.string() + "'");
				}
			}, asset.images[imageIndex].data);
		};

		std::map<std::pair<std::size_t, TextureColorSpace>, std::uint32_t> registeredTextures;
		const auto registerTexture = [&](std::size_t textureIndex, TextureColorSpace colorSpace) {
			const auto key = std::pair{ textureIndex, colorSpace };
			if (const auto found = registeredTextures.find(key); found != registeredTextures.end())
				return found->second;
			if (textureIndex >= asset.textures.size())
				throw std::runtime_error("glTF material texture index is out of range in '" + path.string() + "'");
			const fastgltf::Texture& source = asset.textures[textureIndex];
			if (!source.imageIndex)
				throw std::runtime_error("Unsupported glTF texture image extension in '" + path.string() + "'");
			const std::uint32_t samplerIndex = source.samplerIndex
				? requireU32(*source.samplerIndex, "sampler index") : defaultSamplerIndex;
			if (samplerIndex >= result.textureRegistry.samplers.size())
				throw std::runtime_error("glTF sampler index is out of range in '" + path.string() + "'");
			const std::uint32_t slot = requireU32(
				result.textureRegistry.textures.size(), "texture registry index");
			result.textureRegistry.textures.push_back(RegisteredTexture{
				.texture = decodeImage(*source.imageIndex, colorSpace),
				.samplerIndex = samplerIndex,
			});
			registeredTextures.emplace(key, slot);
			return slot;
		};

		result.materials.reserve(asset.materials.size());
		for (const fastgltf::Material& source : asset.materials) {
			auto material = std::make_shared<Material>();
			material->name = std::string(source.name);
			const auto& color = source.pbrData.baseColorFactor;
			material->baseColor = Color(color[0], color[1], color[2], color[3]);
			material->metallic = source.pbrData.metallicFactor;
			material->roughness = source.pbrData.roughnessFactor;
			material->emissive = Color(
				source.emissiveFactor[0],
				source.emissiveFactor[1],
				source.emissiveFactor[2],
				1.0f);

			if (source.pbrData.baseColorTexture) {
				material->baseColorTexture = registerTexture(
					source.pbrData.baseColorTexture->textureIndex, TextureColorSpace::SRGB);
				material->baseColorTexCoord = requireU32(
					source.pbrData.baseColorTexture->texCoordIndex, "base color texcoord index");
			}

			if (source.pbrData.metallicRoughnessTexture) {
				material->metallicRoughnessTexture = registerTexture(
					source.pbrData.metallicRoughnessTexture->textureIndex, TextureColorSpace::Linear);
				material->metallicRoughnessTexCoord = requireU32(
					source.pbrData.metallicRoughnessTexture->texCoordIndex,
					"metallic-roughness texcoord index");
			}

			if (source.normalTexture) {
				material->normalTexture = registerTexture(
					source.normalTexture->textureIndex, TextureColorSpace::Linear);
				material->normalTexCoord = requireU32(
					source.normalTexture->texCoordIndex, "normal texture index"
				);
				material->normalScale = source.normalTexture->scale;
			}

			if (source.occlusionTexture) {
				material->occlusionTexture = registerTexture(
					source.occlusionTexture->textureIndex, TextureColorSpace::Linear);
				material->occlusionTexCoord = requireU32(
					source.occlusionTexture->texCoordIndex, "occlusion texture index"
				);
				material->occlusionStrength = source.occlusionTexture->strength;
			}

			if (source.emissiveTexture) {
				material->emissiveTexture = registerTexture(
					source.emissiveTexture->textureIndex, TextureColorSpace::SRGB);
				material->emissiveTexCoord = requireU32(
					source.emissiveTexture->texCoordIndex, "emissive texture index"
				);
			}

			result.materials.push_back(std::move(material));
		}

		result.meshes.reserve(asset.meshes.size());
		for (const fastgltf::Mesh& sourceMesh : asset.meshes) {
			Mesh mesh;
			mesh.name = std::string(sourceMesh.name);
			bool hasMeshBounds = false;

			for (const fastgltf::Primitive& sourcePrimitive : sourceMesh.primitives) {
				if (sourcePrimitive.type != fastgltf::PrimitiveType::Triangles)
					throw std::runtime_error("Only triangle glTF primitives are supported in '" + path.string() + "'");
				const auto positionAttribute = sourcePrimitive.findAttribute("POSITION");
				if (positionAttribute == sourcePrimitive.attributes.end())
					throw std::runtime_error("glTF primitive has no POSITION attribute in '" + path.string() + "'");

				const fastgltf::Accessor& positions = asset.accessors[positionAttribute->accessorIndex];
				const std::uint32_t vertexCount = requireU32(positions.count, "primitive vertex count");
				if (vertexCount == 0)
					throw std::runtime_error("glTF primitive has no vertices in '" + path.string() + "'");
				const std::uint32_t baseVertex = requireU32(result.vertices.size(), "vertex offset");
				if (baseVertex > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
					throw std::runtime_error("glTF vertex offset exceeds signed draw limits in '" + path.string() + "'");
				result.vertices.resize(result.vertices.size() + vertexCount);
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positions,
					[&](const fastgltf::math::fvec3& value, std::size_t index) {
						result.vertices[baseVertex + index].position = { value.x(), value.y(), value.z() };
					});

				const auto loadAttribute = [&](std::string_view name, auto elementTag, auto assign) {
					const auto attribute = sourcePrimitive.findAttribute(name);
					if (attribute == sourcePrimitive.attributes.end()) return false;
					const fastgltf::Accessor& accessor = asset.accessors[attribute->accessorIndex];
					if (accessor.count != vertexCount)
						throw std::runtime_error("Mismatched glTF vertex attribute counts in '" + path.string() + "'");
					using Element = decltype(elementTag);
					fastgltf::iterateAccessorWithIndex<Element>(asset, accessor,
						[&](const Element& value, std::size_t index) { assign(result.vertices[baseVertex + index], value); });
					return true;
				};

				const bool hasNormals = loadAttribute("NORMAL", fastgltf::math::fvec3{},
					[](Vertex& vertex, const auto& value) { vertex.normal = { value.x(), value.y(), value.z() }; });
				loadAttribute("TEXCOORD_0", fastgltf::math::fvec2{},
					[](Vertex& vertex, const auto& value) { vertex.texCoord = { value.x(), value.y() }; });
				loadAttribute("TANGENT", fastgltf::math::fvec4{},
					[](Vertex& vertex, const auto& value) { vertex.tangent = { value.x(), value.y(), value.z(), value.w() }; });

				const auto colorAttribute = sourcePrimitive.findAttribute("COLOR_0");
				if (colorAttribute != sourcePrimitive.attributes.end()) {
					const fastgltf::Accessor& colors = asset.accessors[colorAttribute->accessorIndex];
					if (colors.count != vertexCount)
						throw std::runtime_error("Mismatched glTF COLOR_0 count in '" + path.string() + "'");
					if (colors.type == fastgltf::AccessorType::Vec3) {
						fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, colors,
							[&](const auto& value, std::size_t index) { result.vertices[baseVertex + index].color = { value.x(), value.y(), value.z(), 1.0f }; });
					} else {
						fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, colors,
							[&](const auto& value, std::size_t index) { result.vertices[baseVertex + index].color = { value.x(), value.y(), value.z(), value.w() }; });
					}
				}

				if (!sourcePrimitive.indicesAccessor)
					throw std::runtime_error("fastgltf did not generate primitive indices for '" + path.string() + "'");
				const fastgltf::Accessor& sourceIndices = asset.accessors[*sourcePrimitive.indicesAccessor];
				const std::uint32_t firstIndex = requireU32(result.indices.size(), "index offset");
				const std::uint32_t indexCount = requireU32(sourceIndices.count, "primitive index count");
				if (indexCount % 3 != 0)
					throw std::runtime_error("Triangle glTF primitive has a non-triangular index count in '" + path.string() + "'");
				result.indices.resize(result.indices.size() + indexCount);
				fastgltf::copyFromAccessor<std::uint32_t>(asset, sourceIndices, result.indices.data() + firstIndex);
				for (std::uint32_t i = 0; i < indexCount; ++i)
					if (result.indices[firstIndex + i] >= vertexCount)
						throw std::runtime_error("glTF primitive index is out of range in '" + path.string() + "'");

				if (!hasNormals) {
					for (std::uint32_t i = 0; i < indexCount; i += 3) {
						Vertex& a = result.vertices[baseVertex + result.indices[firstIndex + i]];
						Vertex& b = result.vertices[baseVertex + result.indices[firstIndex + i + 1]];
						Vertex& c = result.vertices[baseVertex + result.indices[firstIndex + i + 2]];
						const glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
						a.normal += normal; b.normal += normal; c.normal += normal;
					}
					for (std::uint32_t i = 0; i < vertexCount; ++i) {
						glm::vec3& normal = result.vertices[baseVertex + i].normal;
						normal = glm::dot(normal, normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
					}
				}

				glm::vec3 boundsMin = result.vertices[baseVertex].position;
				glm::vec3 boundsMax = boundsMin;
				for (std::uint32_t i = 1; i < vertexCount; ++i) {
					boundsMin = glm::min(boundsMin, result.vertices[baseVertex + i].position);
					boundsMax = glm::max(boundsMax, result.vertices[baseVertex + i].position);
				}
				const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
				float radius = 0.0f;
				for (std::uint32_t i = 0; i < vertexCount; ++i)
					radius = glm::max(radius, glm::length(result.vertices[baseVertex + i].position - center));

				mesh.primitives.push_back(MeshPrimitive{
					.firstIndex = firstIndex,
					.indexCount = indexCount,
					.vertexOffset = static_cast<std::int32_t>(baseVertex),
					.materialIndex = sourcePrimitive.materialIndex
						? requireU32(*sourcePrimitive.materialIndex, "material index") : InvalidModelIndex,
					.bounds = { boundsMin, boundsMax },
					.boundingSphere = { center, radius },
				});
				if (!hasMeshBounds) { mesh.bounds = { boundsMin, boundsMax }; hasMeshBounds = true; }
				else {
					mesh.bounds.minimum = glm::min(mesh.bounds.minimum, boundsMin);
					mesh.bounds.maximum = glm::max(mesh.bounds.maximum, boundsMax);
				}
			}
			result.meshes.push_back(std::move(mesh));
		}

		result.nodes.reserve(asset.nodes.size());
		for (const fastgltf::Node& source : asset.nodes) {
			const auto matrix = fastgltf::getLocalTransformMatrix(source);
			glm::mat4 local(1.0f);
			for (glm::length_t column = 0; column < 4; ++column)
				for (glm::length_t row = 0; row < 4; ++row)
					local[column][row] = matrix[column][row];
			ModelNode node{
				.name = std::string(source.name),
				.localTransform = local,
				.meshIndex = source.meshIndex ? requireU32(*source.meshIndex, "mesh index") : InvalidModelIndex,
			};
			node.children.reserve(source.children.size());
			for (std::size_t child : source.children) node.children.push_back(requireU32(child, "node index"));
			result.nodes.push_back(std::move(node));
		}

		if (!asset.scenes.empty()) {
			const std::size_t sceneIndex = asset.defaultScene.value_or(0);
			if (sceneIndex >= asset.scenes.size())
				throw std::runtime_error("glTF default scene is out of range in '" + path.string() + "'");
			for (std::size_t root : asset.scenes[sceneIndex].nodeIndices)
				result.sceneRoots.push_back(requireU32(root, "scene root"));
		}
		if (result.sceneRoots.empty() && !result.nodes.empty()) {
			std::vector<bool> isChild(result.nodes.size(), false);
			for (const ModelNode& node : result.nodes)
				for (std::uint32_t child : node.children) if (child < isChild.size()) isChild[child] = true;
			for (std::uint32_t i = 0; i < result.nodes.size(); ++i) if (!isChild[i]) result.sceneRoots.push_back(i);
		}

		if (!result.IsValid())
			throw std::runtime_error("glTF model contains no valid renderable scene: '" + path.string() + "'");
		return std::make_shared<const Model>(std::move(result));
	}


} // namespace Iryven::Importers
