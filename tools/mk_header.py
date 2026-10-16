ops=open('ops.glsl').read()
hdr=open('/workspace/oxce/src/src/Engine/Scalers/xbrz.h').read().split('#ifndef')[0]
body=r'''
#version 130
// GLSL port of the xBRZ scaler (src/Engine/Scalers/xbrz.cpp, ARGB variant, default ScalerCfg),
// evaluated per output pixel: the pixel's footprint on the k-times virtual image is area-averaged
// exactly like HiResResample (xbrz::scale by k = ceil(scale), then separable area average).
uniform sampler2D sTex; uniform sampler2D clsTex; uniform sampler2D colTex;
uniform vec2 outSize; uniform vec2 sSize; uniform vec2 kps; uniform vec2 ph; uniform float cls; uniform int K;
@OPS@
const int ROTP[36] = int[36](0,1,2,3,4,5,6,7,8, 6,3,0,7,4,1,8,5,2, 8,7,6,5,4,3,2,1,0, 2,5,8,1,4,7,0,3,6);
ivec4 Wn[25];   // 5x5 window of source pixels (r,g,b,a 0..255), index (y+2)*5 + x+2
int bI;         // blend info of the centre pixel (4 x 2 bit)
ivec4 fetchPx(ivec2 g) {
	// like the CPU window: the grid ends (edge pixels repeat) at the cells of the first and last output pixel
	ivec2 gmin = ivec2(floor(-ph + 1e-6)), gmax = ivec2(floor((outSize - 1.0) * kps - ph + 1e-6));
	g = clamp(g, gmin, gmax);
	vec4 v = texelFetch(sTex, clamp(g + 1, ivec2(0), ivec2(sSize) - 1), 0);
	return ivec4(v * 255.0 + 0.5);
}
ivec4 W(int x, int y) { return Wn[(y + 2) * 5 + x + 2]; }
bool same(ivec4 p, ivec4 q) { return all(equal(p, q)); }
// ColorDistanceARGB with the quantised YCbCr distance of DistYCbCrBuffer
float dist(ivec4 p, ivec4 q) {
	float a1 = float(p.a) / 255.0, a2 = float(q.a) / 255.0;
	ivec3 e = (p.rgb - q.rgb + 255) / 2;
	vec3 v = vec3(e * 2 - 255);
	const float kb = 0.0593, kr = 0.2627, kg = 1.0 - 0.0593 - 0.2627;
	float y = kr * v.r + kg * v.g + kb * v.b;
	float cb = (0.5 / (1.0 - kb)) * (v.b - y);
	float cr = (0.5 / (1.0 - kr)) * (v.r - y);
	float d = sqrt(y * y + cb * cb + cr * cr);
	return a1 < a2 ? a1 * d + 255.0 * (a2 - a1) : a2 * d + 255.0 * (a1 - a2);
}
bool eqc(ivec4 p, ivec4 q) { return dist(p, q) < 30.0; }
// preProcessCorners for the 4x4 kernel whose F is at (ox, oy): result (blend_f, blend_g, blend_j, blend_k)
ivec4 pre(int ox, int oy) {
	ivec4 A = W(ox-1,oy-1), B = W(ox,oy-1), C = W(ox+1,oy-1), D = W(ox+2,oy-1);
	ivec4 E = W(ox-1,oy), F = W(ox,oy), G = W(ox+1,oy), H = W(ox+2,oy);
	ivec4 I = W(ox-1,oy+1), J = W(ox,oy+1), Kk = W(ox+1,oy+1), L = W(ox+2,oy+1);
	ivec4 M = W(ox-1,oy+2), N = W(ox,oy+2), O = W(ox+1,oy+2);
	ivec4 r = ivec4(0);
	if ((same(F, G) && same(J, Kk)) || (same(F, J) && same(G, Kk))) return r;
	float jg = dist(I, F) + dist(F, C) + dist(N, Kk) + dist(Kk, H) + 4.0 * dist(J, G);
	float fk = dist(E, J) + dist(J, O) + dist(B, G) + dist(G, L) + 4.0 * dist(F, Kk);
	if (jg < fk) {
		int t = 3.6 * jg < fk ? 2 : 1;
		if (!same(F, G) && !same(F, J)) r.x = t;
		if (!same(Kk, J) && !same(Kk, G)) r.w = t;
	} else if (fk < jg) {
		int t = 3.6 * fk < jg ? 2 : 1;
		if (!same(J, F) && !same(J, Kk)) r.z = t;
		if (!same(G, F) && !same(G, Kk)) r.y = t;
	}
	return r;
}
void prepare(ivec2 g) {
	for (int y = -2; y <= 2; ++y)
		for (int x = -2; x <= 2; ++x)
			Wn[(y + 2) * 5 + x + 2] = fetchPx(g + ivec2(x, y));
	int br = pre(0, 0).x, tr = pre(0, -1).z, tl = pre(-1, -1).w, bl = pre(-1, 0).y;
	bI = tl | (tr << 2) | (br << 4) | (bl << 6);
}
ivec4 grad(ivec4 f, ivec4 b, int m, int n) {
	int wf = f.a * m, wb = b.a * (n - m), s = wf + wb;
	if (s == 0) return ivec4(0);
	return ivec4((f.rgb * wf + b.rgb * wb) / s, s / n);
}
// colour of the virtual pixel (row i, column j) of the K x K block of the centre source pixel
ivec4 xbrzPixel(int i, int j) {
	ivec4 val = W(0, 0);
	if (bI == 0) return val;
	ivec4 k3[9] = ivec4[9](W(-1,-1), W(0,-1), W(1,-1), W(-1,0), W(0,0), W(1,0), W(-1,1), W(0,1), W(1,1));
	for (int r = 0; r < 4; ++r) {
		int bl = r == 0 ? bI : r == 1 ? ((bI << 2) | (bI >> 6)) & 255 : r == 2 ? ((bI << 4) | (bI >> 4)) & 255 : ((bI << 6) | (bI >> 2)) & 255;
		int bR = (bl >> 4) & 3, tR = (bl >> 2) & 3, tL = bl & 3, bL = (bl >> 6) & 3;
		if (bR < 1) continue;
		ivec2 ij = ivec2(i, j);
		for (int q = 0; q < r; ++q) ij = ivec2(ij.y, K - 1 - ij.x); // output position in the unrotated op lists
		ivec4 a = k3[ROTP[r*9+0]], b = k3[ROTP[r*9+1]], c = k3[ROTP[r*9+2]], d = k3[ROTP[r*9+3]], e = k3[ROTP[r*9+4]];
		ivec4 f = k3[ROTP[r*9+5]], g = k3[ROTP[r*9+6]], h = k3[ROTP[r*9+7]], ii = k3[ROTP[r*9+8]];
		bool line = true;
		if (bR >= 2) line = true;
		else if (tR != 0 && !eqc(e, g)) line = false;
		else if (bL != 0 && !eqc(e, c)) line = false;
		else if (!eqc(e, ii) && eqc(g, h) && eqc(h, ii) && eqc(ii, f) && eqc(f, c)) line = false;
		ivec4 px = dist(e, f) <= dist(e, h) ? f : h;
		int type = 4;
		if (line) {
			float fg = dist(f, g), hc = dist(h, c);
			bool shallow = 2.2 * fg <= hc && !same(e, g) && !same(d, g);
			bool steep = 2.2 * hc <= fg && !same(e, c) && !same(b, c);
			type = shallow ? (steep ? 2 : 0) : (steep ? 1 : 3);
		}
		ivec2 rg = OPR[(K - 2) * 5 + type];
		for (int n = 0; n < rg.y; ++n) {
			ivec4 op = OPS[rg.x + n];
			if (op.x == ij.x && op.y == ij.y) val = op.w == 0 ? px : grad(px, val, op.z, op.w);
		}
	}
	return val;
}
@TAPS@
void main() {
	vec2 o = vec2(floor(gl_FragCoord.x), outSize.y - 1.0 - floor(gl_FragCoord.y));
	vec2 t = vec2((o.x + 0.5) / outSize.x, 1.0 - (o.y + 0.5) / outSize.y);
	if (abs(texture(clsTex, t).r - cls) > 0.5 / 255.0) discard;
	float k = float(K);
	int ix[4], iy[4]; float wx[4], wy[4];
	int nx = axisTaps(o.x, kps.x, ph.x, k, ix, wx), ny = axisTaps(o.y, kps.y, ph.y, k, iy, wy);
	float tr = 0.0, tg = 0.0, tb = 0.0, ta = 0.0;
	ivec2 cur = ivec2(-1000000);
	for (int a = 0; a < ny; ++a)
		for (int b = 0; b < nx; ++b) {
			float w = wx[b] * wy[a];
			ivec2 g = ivec2(fdiv(ix[b], K), fdiv(iy[a], K));
			if (g != cur) { prepare(g); cur = g; }
			ivec4 p = xbrzPixel(iy[a] - g.y * K, ix[b] - g.x * K);
			float pa = float(p.a) / 255.0 * w;
			ta += pa; tr += float(p.r) * pa; tg += float(p.g) * pa; tb += float(p.b) * pa;
		}
	int A = min(255, int(floor(ta * 255.0 + 0.5)));
	if (A <= 0) discard;
	ivec3 q = ivec3(min(vec3(A), max(vec3(0.0), floor(vec3(tr, tg, tb) + 0.5))));
	ivec3 c = min(ivec3(255), (q * 255 + A / 2) / A);
	gl_FragColor = vec4(vec3(c) / 255.0, texture(colTex, t).a);
}
'''.replace('@OPS@',ops).replace('@TAPS@',open('taps.glsl').read())
# raw string (split in chunks below the MSVC literal limit is not needed with mingw/gcc)
out=hdr+'''// GLSL xBRZ fragment program for the hi-res battlefield scale-mask pass (generated op tables from xbrz.cpp
// by tools/gen_xbrz_ops.py; the algorithm is a direct port of the CPU code in Scalers/xbrz.cpp).
#ifndef OPENXCOM_HIRESXBRZGL_H
#define OPENXCOM_HIRESXBRZGL_H
namespace OpenXcom
{
namespace
{
const char *const SM_FS_UP_XBRZ = R"GLSL(''' + body.lstrip('\n') + ''')GLSL";
}
}
#endif
'''
open('/workspace/oxce/src/src/Engine/HiResXbrzGL.h','w').write(out)
