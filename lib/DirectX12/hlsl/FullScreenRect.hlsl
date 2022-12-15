static float2 positions[] = {
	float2(0.0f, 0.0f),
	float2(2.0f, 0.0f),
	float2(0.0f, 2.0f),
};

float4 main(uint idx: SV_VertexID) : SV_Position{
	return float4(positions[idx], 0.0f, 1.0f);
}