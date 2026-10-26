// IRYVEN_SHADER_STAGES: COMPUTE

struct ReductionData
{
  uint2 reductionSourceExtent;
  uint2 reductionDestinationExtent;
  uint reductionDestinationMip;
  uint reductionPadding0;
  uint reductionPadding1;
  uint reductionPadding2;
};

[[vk::push_constant]]
ConstantBuffer<ReductionData> reductionData : register(b0, space2147483647);

Texture2D<float4> sourceDepth : register(t0, space0);
SamplerState sourceDepthSampler : register(s0, space0);
RWTexture2D<float> destinationDepth : register(u1, space0);

static uint3 dispatchThreadId;
struct StageInput
{
  uint3 dispatchThreadId : SV_DispatchThreadID;
};

float ReduceDepth(int2 destinationPixel)
{
  if (reductionData.reductionDestinationMip == 0u)
  {
    return sourceDepth.Load(int3(destinationPixel, 0)).x;
  }
  int2 sourceExtent = int2(reductionData.reductionSourceExtent);
  int2 destinationSize = int2(reductionData.reductionDestinationExtent);
  int2 sourceMinimumPixel = (destinationPixel * sourceExtent) / destinationSize;
  int2 sourceMaximumPixel =
      ((((destinationPixel + int2(1, 1)) * sourceExtent) + destinationSize) - int2(1, 1)) / destinationSize;
  float maximumDepth = 0.0f;
  for (int sourcePixelY = sourceMinimumPixel.y; sourcePixelY < sourceMaximumPixel.y; sourcePixelY++)
  {
    for (int sourcePixelX = sourceMinimumPixel.x; sourcePixelX < sourceMaximumPixel.x; sourcePixelX++)
    {
      maximumDepth = max(maximumDepth, sourceDepth.Load(int3(int2(sourcePixelX, sourcePixelY), 0)).x);
    }
  }
  return maximumDepth;
}

void RunCompute()
{
  int2 pixel = int2(dispatchThreadId.xy);
  int2 destinationExtent = int2(reductionData.reductionDestinationExtent);
  if (any(bool2(pixel.x >= destinationExtent.x, pixel.y >= destinationExtent.y)))
  {
    return;
  }
  destinationDepth[pixel] = ReduceDepth(pixel).x;
}

[numthreads(8, 8, 1)] void main(StageInput stageInput)
{
  dispatchThreadId = stageInput.dispatchThreadId;
  RunCompute();
}
