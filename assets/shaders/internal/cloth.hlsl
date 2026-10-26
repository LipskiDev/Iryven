// IRYVEN_SHADER_STAGES: COMPUTE
struct ClothParticle
{
  float4 positionAndInverseMass;
  float4 previousPosition;
  float4 normal;
};

ByteAddressBuffer inputParticles : register(t0, space0);
RWByteAddressBuffer outputParticles : register(u1, space0);
struct ClothSimulationConstants
{
  uint constantsResolutionX;
  uint constantsResolutionY;
  float constantsClothWidth;
  float constantsClothHeight;
  float constantsDeltaTime;
  float constantsStiffness;
  float constantsDamping;
  float constantsGravityScale;
  uint constantsPhase;
  uint constantsIteration;
  uint constantsSolverIterations;
  uint constantsParticleCount;
  float constantsTime;
  float constantsReserved;
  float2 constantsPadding;
};

[[vk::push_constant]]
ConstantBuffer<ClothSimulationConstants> clothSimulationConstants : register(b0, space2147483647);

static uint3 dispatchThreadId;
struct StageInput
{
  uint3 dispatchThreadId : SV_DispatchThreadID;
};

void Integrate(uint particleIndex)
{
  ClothParticle particle;
  particle.positionAndInverseMass = inputParticles.Load<float4>(particleIndex * 48 + 0);
  particle.previousPosition = inputParticles.Load<float4>(particleIndex * 48 + 16);
  particle.normal = inputParticles.Load<float4>(particleIndex * 48 + 32);
  float inverseMass = particle.positionAndInverseMass.w;
  if (inverseMass == 0.0f)
  {
    outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
    outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
    outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
    return;
  }
  float3 position = particle.positionAndInverseMass.xyz;
  float3 previousPosition = particle.previousPosition.xyz;
  float3 velocity = position - previousPosition;
  velocity *= (1.0f - clothSimulationConstants.constantsDamping);
  float3 acceleration = float3(0.0f, -9.81f, 0.0f) * clothSimulationConstants.constantsGravityScale;
  float deltaTime = clothSimulationConstants.constantsDeltaTime;
  float3 newPosition = (position + velocity) + ((acceleration * deltaTime) * deltaTime);
  particle.previousPosition.xyz = position;
  particle.positionAndInverseMass.xyz = newPosition;
  outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
  outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
  outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
}

float3 ConstraintCorrection(float3 position, float3 neighborPosition, float restLength)
{
  float3 delta = neighborPosition - position;
  float constraintDistance = length(delta);
  if (constraintDistance < 1e-06f)
  {
    return 0.0f.xxx;
  }
  float error = constraintDistance - restLength;
  return ((normalize(delta) * error) * 0.5f) * clothSimulationConstants.constantsStiffness;
}

void SolveConstraints(uint particleIndex)
{
  ClothParticle particle;
  particle.positionAndInverseMass = outputParticles.Load<float4>(particleIndex * 48 + 0);
  particle.previousPosition = outputParticles.Load<float4>(particleIndex * 48 + 16);
  particle.normal = outputParticles.Load<float4>(particleIndex * 48 + 32);
  float inverseMass = particle.positionAndInverseMass.w;
  if (inverseMass == 0.0f)
  {
    return;
  }
  float3 position = particle.positionAndInverseMass.xyz;
  uint particleColumn = particleIndex % clothSimulationConstants.constantsResolutionX;
  uint particleRow = particleIndex / clothSimulationConstants.constantsResolutionX;
  float horizontalRestLength =
      clothSimulationConstants.constantsClothWidth / float(clothSimulationConstants.constantsResolutionX - 1u);
  float verticalRestLength =
      clothSimulationConstants.constantsClothHeight / float(clothSimulationConstants.constantsResolutionY - 1u);
  float3 totalCorrection = 0.0f.xxx;
  if (particleColumn > 0u)
  {
    uint leftNeighborIndex = particleIndex - 1u;
    ClothParticle leftNeighbor;
    leftNeighbor.positionAndInverseMass = outputParticles.Load<float4>(leftNeighborIndex * 48 + 0);
    leftNeighbor.previousPosition = outputParticles.Load<float4>(leftNeighborIndex * 48 + 16);
    leftNeighbor.normal = outputParticles.Load<float4>(leftNeighborIndex * 48 + 32);
    totalCorrection += ConstraintCorrection(position, leftNeighbor.positionAndInverseMass.xyz, horizontalRestLength);
  }
  if ((particleColumn + 1u) < clothSimulationConstants.constantsResolutionX)
  {
    uint rightNeighborIndex = particleIndex + 1u;
    ClothParticle rightNeighbor;
    rightNeighbor.positionAndInverseMass = outputParticles.Load<float4>(rightNeighborIndex * 48 + 0);
    rightNeighbor.previousPosition = outputParticles.Load<float4>(rightNeighborIndex * 48 + 16);
    rightNeighbor.normal = outputParticles.Load<float4>(rightNeighborIndex * 48 + 32);
    totalCorrection += ConstraintCorrection(position, rightNeighbor.positionAndInverseMass.xyz, horizontalRestLength);
  }
  if (particleRow > 0u)
  {
    uint upperNeighborIndex = particleIndex - clothSimulationConstants.constantsResolutionX;
    ClothParticle upperNeighbor;
    upperNeighbor.positionAndInverseMass = outputParticles.Load<float4>(upperNeighborIndex * 48 + 0);
    upperNeighbor.previousPosition = outputParticles.Load<float4>(upperNeighborIndex * 48 + 16);
    upperNeighbor.normal = outputParticles.Load<float4>(upperNeighborIndex * 48 + 32);
    totalCorrection += ConstraintCorrection(position, upperNeighbor.positionAndInverseMass.xyz, verticalRestLength);
  }
  if ((particleRow + 1u) < clothSimulationConstants.constantsResolutionY)
  {
    uint lowerNeighborIndex = particleIndex + clothSimulationConstants.constantsResolutionX;
    ClothParticle lowerNeighbor;
    lowerNeighbor.positionAndInverseMass = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 0);
    lowerNeighbor.previousPosition = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 16);
    lowerNeighbor.normal = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 32);
    totalCorrection += ConstraintCorrection(position, lowerNeighbor.positionAndInverseMass.xyz, verticalRestLength);
  }
  position += totalCorrection;
  outputParticles.Store<float>(particleIndex * 48 + 0, position.x);
  outputParticles.Store<float>(particleIndex * 48 + 4, position.y);
  outputParticles.Store<float>(particleIndex * 48 + 8, position.z);
}

void RecalculateNormal(uint particleIndex)
{
  ClothParticle particle;
  particle.positionAndInverseMass = outputParticles.Load<float4>(particleIndex * 48 + 0);
  particle.previousPosition = outputParticles.Load<float4>(particleIndex * 48 + 16);
  particle.normal = outputParticles.Load<float4>(particleIndex * 48 + 32);
  uint particleColumn = particleIndex % clothSimulationConstants.constantsResolutionX;
  uint particleRow = particleIndex / clothSimulationConstants.constantsResolutionX;
  float3 particlePosition = particle.positionAndInverseMass.xyz;
  float3 normalSum = 0.0f.xxx;
  bool hasLeft = particleColumn > 0u;
  bool hasRight = (particleColumn + 1u) < clothSimulationConstants.constantsResolutionX;
  bool hasUp = particleRow > 0u;
  bool hasDown = (particleRow + 1u) < clothSimulationConstants.constantsResolutionY;
  float3 left;
  if (hasLeft)
  {
    left = outputParticles.Load<float4>((particleIndex - 1u) * 48 + 0).xyz;
  }
  float3 right;
  if (hasRight)
  {
    right = outputParticles.Load<float4>((particleIndex + 1u) * 48 + 0).xyz;
  }
  float3 up;
  if (hasUp)
  {
    up = outputParticles.Load<float4>((particleIndex - clothSimulationConstants.constantsResolutionX) * 48 + 0).xyz;
  }
  float3 down;
  if (hasDown)
  {
    down = outputParticles.Load<float4>((particleIndex + clothSimulationConstants.constantsResolutionX) * 48 + 0).xyz;
  }
  if (hasUp && hasLeft)
  {
    normalSum += cross(up - particlePosition, left - particlePosition);
  }
  if (hasRight && hasUp)
  {
    normalSum += cross(right - particlePosition, up - particlePosition);
  }
  if (hasDown && hasRight)
  {
    normalSum += cross(down - particlePosition, right - particlePosition);
  }
  if (hasLeft && hasDown)
  {
    normalSum += cross(left - particlePosition, down - particlePosition);
  }
  float normalLengthSquared = dot(normalSum, normalSum);
  if (normalLengthSquared > 1e-12f)
  {
    particle.normal = float4(normalize(normalSum), 0.0f);
  }
  else
  {
    particle.normal = float4(0.0f, 0.0f, 1.0f, 0.0f);
  }
  outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
  outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
  outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
}

void RunCompute()
{
  uint particleIndex = dispatchThreadId.x;
  if (particleIndex >= clothSimulationConstants.constantsParticleCount)
  {
    return;
  }
  switch (clothSimulationConstants.constantsPhase)
  {
    case 0u:
    {
      Integrate(particleIndex);
      break;
    }
    case 1u:
    {
      SolveConstraints(particleIndex);
      break;
    }
    case 2u:
    {
      RecalculateNormal(particleIndex);
      break;
    }
  }
}

[numthreads(64, 1, 1)] void main(StageInput stageInput)
{
  dispatchThreadId = stageInput.dispatchThreadId;
  RunCompute();
}
