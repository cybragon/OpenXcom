/*
 * Copyright 2010-2026 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "HiResResample.h"
#include "Options.h"
#include "Scalers/xbrz.h"
#include <algorithm>
#include <cmath>

namespace OpenXcom
{
namespace HiResResample
{

namespace
{

/// Weights of one axis: output cell o covers source [a + o*step, a + (o+1)*step).
struct Taps
{
	std::vector<int> first, count;
	std::vector<float> w;
	std::vector<size_t> offs;
};

Taps makeTaps(double a, double len, int outN, int srcN, int m)
{
	Taps t;
	const double step = len / outN;
	const bool up = step < 1.0 - 1e-9;
	for (int o = 0; o < outN; ++o)
	{
		t.first.push_back(0);
		t.count.push_back(0);
		t.offs.push_back(t.w.size());
		std::vector<std::pair<int, double>> taps;
		if (!up)
		{
			// area average: exact overlap of the output cell with each source pixel
			const double s0 = a + o * step, s1 = s0 + step;
			for (int i = std::max(0, (int)std::floor(s0)); i < std::min(srcN, (int)std::ceil(s1 - 1e-9)); ++i)
			{
				const double ov = std::min(s1, i + 1.0) - std::max(s0, (double)i);
				if (ov > 1e-9)
					taps.push_back({ i, ov });
			}
		}
		else
		{
			const double c = a + (o + 0.5) * step - 0.5; // source pixel centre coordinate
			if (m == NEAREST)
			{
				taps.push_back({ std::min(srcN - 1, std::max(0, (int)std::floor(c + 0.5))), 1.0 });
			}
			else
			{
				// bilinear, clamped to the picture (edge pixels are not faded into nothing)
				const int i0 = (int)std::floor(c);
				const double f = c - i0;
				const int a0 = std::min(srcN - 1, std::max(0, i0)), a1 = std::min(srcN - 1, std::max(0, i0 + 1));
				if (a0 == a1) taps.push_back({ a0, 1.0 });
				else { taps.push_back({ a0, 1.0 - f }); taps.push_back({ a1, f }); }
			}
		}
		if (taps.empty())
			taps.push_back({ std::min(srcN - 1, std::max(0, (int)std::floor(a + (o + 0.5) * step))), 1.0 });
		double sum = 0;
		for (auto &tp : taps) sum += tp.second;
		t.first.back() = taps.front().first;
		t.count.back() = taps.back().first - taps.front().first + 1;
		const size_t base = t.w.size();
		t.w.resize(base + t.count.back(), 0.0f);
		for (auto &tp : taps) t.w[base + tp.first - t.first.back()] += (float)(tp.second / sum);
	}
	return t;
}

void separable(const Uint32 *src, int w, int h, double rx, double ry, double rw, double rh, int outW, int outH, int m, std::vector<Uint32> &out)
{
	const Taps tx = makeTaps(rx, rw, outW, w, m), ty = makeTaps(ry, rh, outH, h, m);
	int y0 = h, y1 = 0;
	for (int o = 0; o < outH; ++o) { y0 = std::min(y0, ty.first[o]); y1 = std::max(y1, ty.first[o] + ty.count[o]); }
	const int rows = std::max(0, y1 - y0);
	std::vector<float> tmp((size_t)rows * outW * 4, 0.0f);
	for (int y = y0; y < y1; ++y)
	{
		const Uint32 *srow = src + (size_t)y * w;
		float *row = &tmp[(size_t)(y - y0) * outW * 4];
		for (int o = 0; o < outW; ++o)
		{
			float r = 0, g = 0, b = 0, a = 0;
			const float *wt = &tx.w[tx.offs[o]];
			for (int k = 0; k < tx.count[o]; ++k)
			{
				if (!wt[k]) continue;
				const Uint32 px = srow[tx.first[o] + k];
				const float pa = (px >> 24) / 255.0f * wt[k];
				a += pa;
				r += ((px >> 16) & 0xFF) * pa;
				g += ((px >> 8) & 0xFF) * pa;
				b += (px & 0xFF) * pa;
			}
			row[o * 4 + 0] = r; row[o * 4 + 1] = g; row[o * 4 + 2] = b; row[o * 4 + 3] = a;
		}
	}
	out.assign((size_t)outW * outH, 0);
	for (int o = 0; o < outH; ++o)
	{
		const float *wt = &ty.w[ty.offs[o]];
		for (int x = 0; x < outW; ++x)
		{
			float r = 0, g = 0, b = 0, a = 0;
			for (int k = 0; k < ty.count[o]; ++k)
			{
				if (!wt[k]) continue;
				const float *s = &tmp[((size_t)(ty.first[o] + k - y0) * outW + x) * 4];
				r += s[0] * wt[k]; g += s[1] * wt[k]; b += s[2] * wt[k]; a += s[3] * wt[k];
			}
			const unsigned A = (unsigned)std::min(255L, std::lround(a * 255.0f));
			if (!A)
				continue;
			auto ch = [&](float v) { return (Uint32)std::min((long)A, std::max(0L, std::lround(v))); };
			out[(size_t)o * outW + x] = ((Uint32)A << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
		}
	}
}

}

int mode()
{
	return std::min(2, std::max(0, Options::oxceHiResUpscale));
}

Uint32 premultiply(Uint32 p)
{
	const unsigned a = p >> 24;
	if (a == 255) return p;
	if (!a) return 0;
	auto ch = [&](int sh) { return (Uint32)((((p >> sh) & 0xFF) * a + 127) / 255); };
	return (p & 0xFF000000u) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void blendOver(Uint32 &d, Uint32 s)
{
	const unsigned a = s >> 24;
	if (!a)
		return;
	if (a == 255)
	{
		d = s;
		return;
	}
	const unsigned da = d >> 24, ia = 255 - a;
	if (da == 255)
	{
		auto ch = [&](int sh) { return (Uint32)std::min(255u, ((s >> sh) & 0xFF) + (((d >> sh) & 0xFF) * ia + 127) / 255); };
		d = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
		return;
	}
	// straight destination with alpha: out = s + d*da*(1-a), un-premultiplied by the result alpha
	const unsigned oa = a + (da * ia + 127) / 255;
	if (!oa) { d = 0; return; }
	auto ch = [&](int sh)
	{
		const unsigned v = ((s >> sh) & 0xFF) * 255 + ((d >> sh) & 0xFF) * da * ia / 255;
		return (Uint32)std::min(255u, (v + oa / 2) / oa);
	};
	d = ((Uint32)oa << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void resample(const Uint32 *src, int w, int h, double rx, double ry, double rw, double rh, int outW, int outH, int m, std::vector<Uint32> &out)
{
	out.clear();
	if (!src || w <= 0 || h <= 0 || outW <= 0 || outH <= 0 || rw <= 0 || rh <= 0)
		return;
	const bool intRect = rx == std::floor(rx) && ry == std::floor(ry) && rw == std::floor(rw) && rh == std::floor(rh);
	if (intRect && (int)rw == outW && (int)rh == outH && rx >= 0 && ry >= 0 && rx + rw <= w && ry + rh <= h)
	{
		// exact size: copy
		out.resize((size_t)outW * outH);
		for (int y = 0; y < outH; ++y)
			for (int x = 0; x < outW; ++x)
				out[(size_t)y * outW + x] = premultiply(src[(size_t)(y + (int)ry) * w + x + (int)rx]);
		return;
	}
	const double scale = std::max(outW / rw, outH / rh);
	if (m == PIXEL && scale > 1.0 + 1e-9)
	{
		// pixel art: xBRZ (alpha aware) by the next integer factor on the source rect, then area-average down
		const int k = std::min(6, std::max(2, (int)std::ceil(scale - 1e-9)));
		const int ix0 = std::max(0, (int)std::floor(rx)), iy0 = std::max(0, (int)std::floor(ry));
		const int ix1 = std::min(w, (int)std::ceil(rx + rw - 1e-9)), iy1 = std::min(h, (int)std::ceil(ry + rh - 1e-9));
		const int cw = ix1 - ix0, ch = iy1 - iy0;
		if (cw > 0 && ch > 0 && (size_t)cw * ch * k * k <= (size_t)32 * 1024 * 1024)
		{
			std::vector<Uint32> crop((size_t)cw * ch), big((size_t)cw * k * ch * k);
			for (int y = 0; y < ch; ++y)
				std::copy(src + (size_t)(iy0 + y) * w + ix0, src + (size_t)(iy0 + y) * w + ix1, &crop[(size_t)y * cw]);
			xbrz::scale(k, crop.data(), big.data(), cw, ch, xbrz::ColorFormat::ARGB);
			separable(big.data(), cw * k, ch * k, (rx - ix0) * k, (ry - iy0) * k, rw * k, rh * k, outW, outH, SMOOTH, out);
			return;
		}
	}
	separable(src, w, h, rx, ry, rw, rh, outW, outH, m == PIXEL ? SMOOTH : m, out);
}

}
}
