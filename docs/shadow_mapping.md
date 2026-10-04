# Shadow mapping scaffold

This scaffold follows the point-light shadow pipeline described in Chapter 8
of *Mastering Graphics Programming with Vulkan*, adapted to Iryven's existing
`GL_EXT_mesh_shader` renderer.

## Pipeline

1. `shadow_instance_cull.comp` tests every point-light/object pair using their
   bounding spheres and writes per-light meshlet-instance candidates.
2. `shadow_command_build.comp` emits six commands for every light, one for each
   cubemap face.
3. `shadow.task` performs fine meshlet-to-face culling and compacts surviving
   meshlets into a task payload.
4. `shadow.mesh` transforms meshlets with the selected face matrix and writes
   primitives to `lightIndex * 6 + faceIndex` in a layered depth image.
5. `shadow_sampling.glsl` supplies the cubemap-array depth comparison used by
   deferred and forward lighting.
6. `SparseShadowAllocator` is the CPU planning seam for the chapter's optional
   sparse-residency optimization. Fully resident cubemaps should be brought up
   first.

## Buffer contracts

All GPU records use 16-byte strides where a CPU counterpart is expected.
`ShadowDrawCommand` stores the three fields of
`VkDrawMeshTasksIndirectCommandEXT`, followed by a packed light/face value.
Because Vulkan consumes a 12-byte indirect command, production code should
either use a separate metadata buffer or issue commands with a 16-byte stride
after the RHI exposes the operation.

The visible candidate buffer is partitioned into equal per-light ranges. Its
capacity must be chosen from the scene's total meshlet-instance count and
clamped in the culling shader to prevent overflow.

## RHI work required before activation

- Add `DrawMeshTasksIndirect` and preferably `DrawMeshTasksIndirectCount` to
  `ICommandList` and the Vulkan backend.
- Extend the single-cubemap support to cube arrays when multiple point lights
  are enabled. Layered dynamic rendering and six-layer frame-graph textures are
  already supported; the multi-light path still needs `CubeArray` allocation
  and sampling views.
- Add descriptor bindings for shadow matrices, light positions, object
  transforms, meshlet buffers, candidate buffers, and the sampled shadow map.
- Add a shadow index/flags field to the packed GPU light record. A sentinel
  value should identify lights that do not cast shadows.
- Insert storage-write to indirect-read and depth-write to sampled-read
  dependencies in the frame graph.

## Integration order

Bring up a single fully resident point-light cubemap first, then enable the
compute-generated command path, multiple lights, filtering/bias controls, and
finally sparse residency. Sparse binding also requires a queue with
`VK_QUEUE_SPARSE_BINDING_BIT` and explicit semaphore synchronization before
the image is rendered or sampled.
