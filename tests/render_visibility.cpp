#include <iryven/iryven.h>

#include <cassert>
#include <memory>

void RunMeshRendererVisibilityTest()
{
	Iryven::World world;
	auto entity = world.CreateEntity("Visibility Test Mesh");
	entity.Add<Iryven::Transform>();
	entity.Add<Iryven::MeshRenderer>(std::make_shared<const Iryven::MeshData>());

	assert(world.ExtractRenderScene().objects.size() == 1);
	entity.Get<Iryven::MeshRenderer>().hidden = true;
	assert(world.ExtractRenderScene().objects.empty());
	entity.Get<Iryven::MeshRenderer>().hidden = false;
	assert(world.ExtractRenderScene().objects.size() == 1);
}
