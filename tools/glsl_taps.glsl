// Weights of one axis (as HiResResample::makeTaps): output cell o covers [B0, B1) of the F-times virtual image. A step >= 1
// averages the covered virtual pixels by overlap, a smaller step interpolates linearly (centre aligned).
int axisTaps(float o, float kpsA, float phA, float F, out int idx[4], out float wt[4]) {
	float B0 = (o * kpsA - phA) * F, step = kpsA * F, B1 = B0 + step;
	int n = 0;
	if (step >= 1.0 - 1e-6) {
		int i0 = int(floor(B0)), i1 = int(ceil(B1 - 1e-6));
		float sum = 0.0;
		for (int i = i0; i < i1 && n < 4; ++i) {
			float ov = min(B1, float(i + 1)) - max(B0, float(i));
			if (ov > 1e-6) { idx[n] = i; wt[n] = ov; sum += ov; ++n; }
		}
		for (int q = 0; q < n; ++q) wt[q] /= sum;
	} else {
		float c = B0 + 0.5 * step - 0.5;
		int i0 = int(floor(c));
		float fr = c - float(i0);
		idx[0] = i0; wt[0] = 1.0 - fr; idx[1] = i0 + 1; wt[1] = fr; n = 2;
	}
	return n;
}
int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
