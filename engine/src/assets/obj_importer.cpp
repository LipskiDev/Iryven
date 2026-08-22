#include "model_importers.h"

#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace Iryven::Importers {
namespace {

float ParseFloat(std::string_view value, const std::filesystem::path& path, std::size_t line) {
	float result = 0.0f;
	const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
	if (error != std::errc{} || end != value.data() + value.size())
		throw std::runtime_error("Invalid number in '" + path.string() + "' at line " + std::to_string(line));
	return result;
}

std::size_t ParseObjIndex(std::string_view token, std::size_t count,
	const std::filesystem::path& path, std::size_t line) {
	int index = 0;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), index);
	if (error != std::errc{} || end != token.data() + token.size() || index == 0)
		throw std::runtime_error("Invalid OBJ face index in '" + path.string() + "' at line " + std::to_string(line));
	const auto resolved = index > 0 ? static_cast<long long>(index - 1) : static_cast<long long>(count) + index;
	if (resolved < 0 || resolved >= static_cast<long long>(count))
		throw std::runtime_error("OBJ face index out of range in '" + path.string() + "' at line " + std::to_string(line));
	return static_cast<std::size_t>(resolved);
}

struct ObjReference {
	std::size_t position;
	std::optional<std::size_t> normal;
};

ObjReference ParseObjReference(std::string_view token, std::size_t positionCount,
	std::size_t normalCount, const std::filesystem::path& path, std::size_t line) {
	const auto firstSlash = token.find('/');
	ObjReference result{ .position = ParseObjIndex(token.substr(0, firstSlash), positionCount, path, line) };
	if (firstSlash == std::string_view::npos) return result;
	const auto secondSlash = token.find('/', firstSlash + 1);
	if (secondSlash == std::string_view::npos || secondSlash + 1 == token.size()) return result;
	result.normal = ParseObjIndex(token.substr(secondSlash + 1), normalCount, path, line);
	return result;
}

} // namespace

ModelHandle ImportObj(const std::filesystem::path& path) {
	std::ifstream input(path);
	if (!input) throw std::runtime_error("Could not open model '" + path.string() + "'");

	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<Vertex> vertices;
	std::vector<bool> hasExplicitNormal;
	std::vector<std::uint32_t> indices;
	std::map<std::pair<std::size_t, std::optional<std::size_t>>, std::uint32_t> vertexLookup;
	std::string lineText;
	std::size_t lineNumber = 0;
	while (std::getline(input, lineText)) {
		++lineNumber;
		std::istringstream line(lineText);
		std::string kind;
		line >> kind;
		if (kind.empty() || kind[0] == '#') continue;
		if (kind == "v") {
			std::string x, y, z;
			if (!(line >> x >> y >> z))
				throw std::runtime_error("Invalid OBJ vertex in '" + path.string() + "' at line " + std::to_string(lineNumber));
			positions.emplace_back(ParseFloat(x, path, lineNumber), ParseFloat(y, path, lineNumber), ParseFloat(z, path, lineNumber));
		} else if (kind == "vn") {
			std::string x, y, z;
			if (!(line >> x >> y >> z))
				throw std::runtime_error("Invalid OBJ normal in '" + path.string() + "' at line " + std::to_string(lineNumber));
			const glm::vec3 normal{ ParseFloat(x, path, lineNumber), ParseFloat(y, path, lineNumber), ParseFloat(z, path, lineNumber) };
			if (glm::dot(normal, normal) == 0.0f)
				throw std::runtime_error("Zero-length OBJ normal in '" + path.string() + "' at line " + std::to_string(lineNumber));
			normals.push_back(glm::normalize(normal));
		} else if (kind == "f") {
			std::vector<std::uint32_t> face;
			std::string token;
			while (line >> token) {
				const ObjReference reference = ParseObjReference(token, positions.size(), normals.size(), path, lineNumber);
				const auto key = std::make_pair(reference.position, reference.normal);
				auto [entry, inserted] = vertexLookup.try_emplace(key, static_cast<std::uint32_t>(vertices.size()));
				if (inserted) {
					vertices.push_back(Vertex{
						.position = positions[reference.position],
						.normal = reference.normal ? normals[*reference.normal] : glm::vec3{ 0.0f },
					});
					hasExplicitNormal.push_back(reference.normal.has_value());
				}
				face.push_back(entry->second);
			}
			if (face.size() < 3)
				throw std::runtime_error("OBJ face has fewer than three vertices in '" + path.string() + "' at line " + std::to_string(lineNumber));
			for (std::size_t i = 1; i + 1 < face.size(); ++i)
				indices.insert(indices.end(), { face[0], face[i], face[i + 1] });
		}
	}
	if (vertices.empty() || indices.empty())
		throw std::runtime_error("Model '" + path.string() + "' contains no renderable geometry");

	for (std::size_t i = 0; i < indices.size(); i += 3) {
		const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
		const glm::vec3 faceNormal = glm::cross(vertices[b].position - vertices[a].position, vertices[c].position - vertices[a].position);
		if (!hasExplicitNormal[a]) vertices[a].normal += faceNormal;
		if (!hasExplicitNormal[b]) vertices[b].normal += faceNormal;
		if (!hasExplicitNormal[c]) vertices[c].normal += faceNormal;
	}
	for (std::size_t i = 0; i < vertices.size(); ++i) {
		if (hasExplicitNormal[i]) continue;
		vertices[i].normal = glm::dot(vertices[i].normal, vertices[i].normal) > 0.0f
			? glm::normalize(vertices[i].normal) : glm::vec3{ 0.0f, 1.0f, 0.0f };
	}

	glm::vec3 boundsMin = vertices.front().position;
	glm::vec3 boundsMax = boundsMin;
	for (const Vertex& vertex : vertices) {
		boundsMin = glm::min(boundsMin, vertex.position);
		boundsMax = glm::max(boundsMax, vertex.position);
	}
	const glm::vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
	float boundsRadius = 0.0f;
	for (const Vertex& vertex : vertices)
		boundsRadius = glm::max(boundsRadius, glm::length(vertex.position - boundsCenter));
	const auto indexCount = static_cast<std::uint32_t>(indices.size());

	return std::make_shared<const Model>(Model{
		.source = path,
		.vertices = std::move(vertices),
		.indices = std::move(indices),
		.meshes = { Mesh{
			.name = path.stem().string(),
			.primitives = { MeshPrimitive{
				.firstIndex = 0,
				.indexCount = indexCount,
				.bounds = { boundsMin, boundsMax },
				.boundingSphere = { boundsCenter, boundsRadius },
			} },
			.bounds = { boundsMin, boundsMax },
		} },
		.nodes = { ModelNode{ .name = path.stem().string(), .meshIndex = 0 } },
		.sceneRoots = { 0 },
	});
}

} // namespace Iryven::Importers
