#version 130
// GLSL port of hqx (hq2x / hq3x / hq4x, src/Engine/Scalers) for the hi-res battlefield scale-mask pass: per output pixel
// the covered pixels of the F-times virtual image are looked up from the pattern table (lut) and averaged like HiResResample.
uniform sampler2D sTex; uniform sampler2D clsTex; uniform sampler2D colTex; uniform sampler2D lut;
uniform vec2 outSize; uniform vec2 sSize; uniform vec2 kps; uniform vec2 ph; uniform float cls; uniform int K;
ivec4 Wn[25];   // 5x5 window of the source grid: rgb 0..255, a = 1 when the cell has a sample
ivec3 Wc[10];   // 3x3 neighbourhood w[1..9] after the local hole fill
int pattern;
ivec4 fetchPx(ivec2 g) {
	ivec2 gmin = ivec2(floor(-ph + 1e-6)), gmax = ivec2(floor((outSize - 1.0) * kps - ph + 1e-6));
	g = clamp(g, gmin, gmax);
	vec4 v = texelFetch(sTex, clamp(g + 1, ivec2(0), ivec2(sSize) - 1), 0);
	return ivec4(ivec3(v.rgb * 255.0 + 0.5), v.a > 0.5 ? 1 : 0);
}
// cells without a sample take the first sampled neighbour (left, right, up, down, then the diagonals)
ivec3 filledCell(int x, int y) {
	ivec4 p = Wn[(y + 2) * 5 + x + 2];
	if (p.a > 0) return p.rgb;
	const ivec2 nb[8] = ivec2[8](ivec2(-1,0), ivec2(1,0), ivec2(0,-1), ivec2(0,1), ivec2(-1,-1), ivec2(1,-1), ivec2(-1,1), ivec2(1,1));
	for (int n = 0; n < 8; ++n) {
		ivec4 q = Wn[(y + nb[n].y + 2) * 5 + x + nb[n].x + 2];
		if (q.a > 0) return q.rgb;
	}
	return ivec3(0);
}
int q1000(int n) { return n >= 0 ? n / 1000 : -((-n) / 1000); }
ivec3 yuv(ivec3 c) {   // RGBtoYUV of hqx (init.cpp), truncating like the table
	return ivec3(q1000(299 * c.r + 587 * c.g + 114 * c.b), 128 + q1000(-169 * c.r - 331 * c.g + 500 * c.b), 128 + q1000(500 * c.r - 419 * c.g - 81 * c.b));
}
bool diffc(ivec3 a, ivec3 b) {
	ivec3 d = abs(yuv(a) - yuv(b));
	return d.x > 48 || d.y > 7 || d.z > 6;
}
void prepare(ivec2 g) {
	for (int y = -2; y <= 2; ++y)
		for (int x = -2; x <= 2; ++x)
			Wn[(y + 2) * 5 + x + 2] = fetchPx(g + ivec2(x, y));
	for (int n = 1; n <= 9; ++n) Wc[n] = filledCell((n - 1) % 3 - 1, (n - 1) / 3 - 1);
	pattern = 0;
	int flag = 1;
	for (int n = 1; n <= 9; ++n) {
		if (n == 5) continue;
		if (Wc[n] != Wc[5] && diffc(Wc[5], Wc[n])) pattern |= flag;
		flag <<= 1;
	}
}
ivec3 interp(int kind, ivec3 c1, ivec3 c2, ivec3 c3) {
	if (kind == 0) return c1;
	if (kind == 1) return (c1 * 3 + c2) >> 2;
	if (kind == 2) return (c1 * 2 + c2 + c3) >> 2;
	if (kind == 3) return (c1 * 7 + c2) >> 3;
	if (kind == 4) return (c1 * 2 + (c2 + c3) * 7) >> 4;
	if (kind == 5) return (c1 + c2) >> 1;
	if (kind == 6) return (c1 * 5 + c2 * 2 + c3) >> 3;
	if (kind == 7) return (c1 * 6 + c2 + c3) >> 3;
	if (kind == 8) return (c1 * 5 + c2 * 3) >> 3;
	if (kind == 9) return (c1 * 2 + (c2 + c3) * 3) >> 3;
	return (c1 * 14 + c2 + c3) >> 4;
}
ivec3 hqxPixel(int i, int j) {
	int row = (K == 2 ? 0 : K == 3 ? 4 : 13) + i * K + j;
	ivec4 e = ivec4(texelFetch(lut, ivec2(pattern * 2, row), 0) * 255.0 + 0.5);
	int cond = e.x >> 4;
	if (cond != 0) {
		bool d = cond == 1 ? diffc(Wc[2], Wc[6]) : cond == 2 ? diffc(Wc[6], Wc[8]) : cond == 3 ? diffc(Wc[8], Wc[4]) : diffc(Wc[4], Wc[2]);
		if (!d) e = ivec4(texelFetch(lut, ivec2(pattern * 2 + 1, row), 0) * 255.0 + 0.5);
	}
	return interp(e.x & 15, Wc[e.y], Wc[e.z], Wc[e.w]);
}
@TAPS@
void main() {
	vec2 o = vec2(floor(gl_FragCoord.x), outSize.y - 1.0 - floor(gl_FragCoord.y));
	vec2 t = vec2((o.x + 0.5) / outSize.x, 1.0 - (o.y + 0.5) / outSize.y);
	if (abs(texture(clsTex, t).r - cls) > 0.5 / 255.0) discard;
	float k = float(K);
	int ix[4], iy[4]; float wx[4], wy[4];
	int nx = axisTaps(o.x, kps.x, ph.x, k, ix, wx), ny = axisTaps(o.y, kps.y, ph.y, k, iy, wy);
	vec3 s = vec3(0.0);
	float sw = 0.0;
	ivec2 cur = ivec2(-1000000);
	for (int a = 0; a < ny; ++a)
		for (int b = 0; b < nx; ++b) {
			float w = wx[b] * wy[a];
			ivec2 g = ivec2(fdiv(ix[b], K), fdiv(iy[a], K));
			if (g != cur) { prepare(g); cur = g; }
			s += vec3(hqxPixel(iy[a] - g.y * K, ix[b] - g.x * K)) * w;
			sw += w;
		}
	int A = min(255, int(floor(sw * 255.0 + 0.5)));
	if (A <= 0) discard;
	ivec3 q = ivec3(min(vec3(A), max(vec3(0.0), floor(s + 0.5))));
	ivec3 c = min(ivec3(255), (q * 255 + A / 2) / A);
	gl_FragColor = vec4(vec3(c) / 255.0, texture(colTex, t).a);
}
