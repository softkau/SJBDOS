#if 0
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <array>
#include <algorithm>
#include "../syscall.h"

template <class T>
struct Vector3D {
	T x, y, z;
	Vector3D<T> operator+(const Vector3D& rhs) const {
		return { x + rhs.x, y + rhs.y, z + rhs.z };
	}
	Vector3D<T>& operator+=(const Vector3D& rhs) {
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		return *this;
	}
};

template <class T>
struct Vector2D {
	T x, y;
};

void DrawSurface_opt(uint64_t layer_id, int surf);
bool Sleep(unsigned long ms);

/* Vertices
	  5-----1
	 /:    /|
	4-+---0 |
	| 7...|.3
	|;    |/
	6-----2
*/

const std::array<Vector3D<double>, 8> kVertex {{
	{1,1,1}, {1,1,-1}, {1,-1,1}, {1,-1,-1},
	{-1,1,1}, {-1,1,-1}, {-1,-1,1}, {-1,-1,-1}
}};
const std::array<std::array<int, 4>, 6> kSurface {{
	{0,4,6,2}, {1,3,7,5}, {0,2,3,1}, {0,1,5,4}, {4,5,7,6}, {6,7,3,2}
}};
const std::array<uint32_t, kSurface.size()> kColor {
	0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff
};
// red: 0, green: 1, yellow: 2, blue: 3, magenta: 4, cyan: 5

constexpr int kScale = 50, kMargin = 10;
constexpr int kCanvasSize = 3 * kScale + kMargin;

using Mat4 = std::array<std::array<double, 4>, 4>;

void Multiply(Mat4& dst, const Mat4& a, const Mat4& b);
void Multiply(Vector3D<double>& dst, const Mat4& a, const Vector3D<double>& v);
void Rotate(Mat4& dst, const Mat4& a, const Vector3D<double>& axis, double radian);
Vector3D<double> Normalize(const Vector3D<double>& v) {
	double n = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
	return { v.x / n, v.y / n, v.z / n };
}

std::array<Vector3D<double>, kVertex.size()> rotated;
std::array<double, kSurface.size()> z_buffer;
std::array<Vector2D<int>, kVertex.size()> scr;

extern "C" void main(int argc, char** argv) {
	auto [layer_id, err] = SyscallOpenWindow(kCanvasSize + 8, kCanvasSize + 28, 10, 10, "cube");
	if (err) {
		exit(err);
	}
	SyscallWinFillRect(layer_id, 4, 24, kCanvasSize, kCanvasSize, 0x000000);

	constexpr Mat4 identity {{
		{kScale*0.8,0,0,0},
		{0,kScale*0.8,0,0},
		{0,0,kScale*0.8,0},
		{0,0,0,1}
	}};

	Vector3D<double> axis = { 0.1, 0.2, 0.3 };
	axis = Normalize(axis);

	Mat4 rotation, tmp;
	int deg = 0;
	while (true) {
		deg += 10;
		if (deg == 360) deg = 0;

		// 회전 시키기
		Rotate(tmp, identity, Vector3D<double>{ 0.0, 1.0, 0.0 }, deg * M_PI /  90.0);
		Rotate(rotation, tmp, Vector3D<double>{ 1.0, 0.0, 0.0 }, deg * M_PI / 180.0);
		for (int i = 0; i < kVertex.size(); i++) {
			Multiply(rotated[i], rotation, kVertex[i]);
		}
		// z 값 구하기
		for (int i = 0; i < kSurface.size(); i++) {
			double z = 0;
			for (int j = 0; j < 4; j++) {
				z += rotated[kSurface[i][j]].z;
			}
			z_buffer[i] = z / 4.0;
		}
		// 투영하기
		for (int i = 0; i < kVertex.size(); i++) {
			scr[i].x = rotated[i].x + kCanvasSize / 2 + 4;
			scr[i].y = rotated[i].y + kCanvasSize / 2 + 24;
		}

		// z buffer 3위까지 추려낸다
		std::array<int, 6> surface_order { 0, 1, 2, 3, 4, 5 };
		std::sort(surface_order.begin(), surface_order.end(), [](int x, int y) { return z_buffer[x] > z_buffer[y]; });
		
		// 렌더링
		SyscallWinFillRect(layer_id | LAYER_NO_DRAW, 4, 24, kCanvasSize, kCanvasSize, 0x000000);
		for (int i = 0; i < 3; i++) {
			DrawSurface_opt(layer_id, surface_order[i]);
		}
		SyscallWinRedraw(layer_id);

		bool wake = Sleep(800);
		if (!wake) break;
	}

	SyscallCloseWindow(layer_id);
	exit(0);
}

// rasterization 최적화 알고리즘: https://fgiesen.wordpress.com/2013/02/10/optimizing-the-basic-rasterizer/
// todo: sse 명령어로 4배 빠르게 해보자
void DrawTriangle_opt(uint64_t layer_id, int scr_a, int scr_b, int scr_c, uint32_t color) {
	const auto& v0 = scr[scr_a];
	const auto& v1 = scr[scr_b];
	const auto& v2 = scr[scr_c];

	// bounding box
	auto [xmin, xmax] = std::minmax({ v0.x, v1.x, v2.x });
	auto [ymin, ymax] = std::minmax({ v0.y, v1.y, v2.y });
	
	// clipping
	xmin = std::max(xmin,  4); xmax = std::min(xmax, kCanvasSize +  4);
	ymin = std::max(ymin, 24); ymax = std::min(ymax, kCanvasSize + 24);

	const int a01 = v0.y - v1.y, b01 = v1.x - v0.x;
	const int a12 = v1.y - v2.y, b12 = v2.x - v1.x;
	const int a20 = v2.y - v0.y, b20 = v0.x - v2.x;
	
	// barycentric coordinates at minX/minY corner
	Vector2D<int> p = { xmin, ymin };
	int w0_row = a12 * p.x + b12 * p.y + v1.x*v2.y - v1.y*v2.x;
	int w1_row = a20 * p.x + b20 * p.y + v2.x*v0.y - v2.y*v0.x;
	int w2_row = a01 * p.x + b01 * p.y + v0.x*v1.y - v0.y*v1.x;

	// rasterize
	for (p.y = ymin; p.y <= ymax; p.y++) {
		int w0 = w0_row;
		int w1 = w1_row;
		int w2 = w2_row;
		auto inside = [](int w0, int w1, int w2) -> bool { return (w0 | w1 | w2) >= 0; };
		
		for (p.x = xmin; p.x <= xmax && !inside(w0, w1, w2); p.x++) {
			w0 += a12;
			w1 += a20;
			w2 += a01;
		} int start = p.x;
		for (; p.x <= xmax && inside(w0, w1, w2); p.x++) {
			w0 += a12;
			w1 += a20;
			w2 += a01;
		} int end = inside(w0, w1, w2) ? p.x : p.x - 1;
		
		SyscallWinFillRect(layer_id | LAYER_NO_DRAW, start, p.y, end - start + 1, 1, color);

		w0_row += b12;
		w1_row += b20;
		w2_row += b01;
	}
}

void DrawSurface_opt(uint64_t layer_id, int surf) {
	const auto& vid = kSurface[surf];
	const auto color = kColor[surf];

	DrawTriangle_opt(layer_id, vid[0], vid[1], vid[2], color);
	DrawTriangle_opt(layer_id, vid[0], vid[2], vid[3], color);
}

bool Sleep(unsigned long ms) {
	static unsigned long prv_timeout = 0;
	if (prv_timeout == 0) {
		auto timeout = SyscallCreateTimer(TIMER_ONESHOT_REL, 1, ms);
		prv_timeout = timeout.value;
	}
	else {
		prv_timeout += ms;
		auto timeout = SyscallCreateTimer(TIMER_ONESHOT_ABS, 1, prv_timeout);
	}
	SyscallCreateTimer(TIMER_ONESHOT_REL, 1, ms);
	
	AppEvent e;
	while (true) {
		SyscallReadEvent(&e, 1);
		if (e.type == AppEvent::kTimerTimeout) {
			return true;
		}
		else if (e.type == AppEvent::kQuit) {
			return false;
		}
	}
}

void Multiply(Mat4& dst, const Mat4& a, const Mat4& b) {
	memset(&dst[0][0], 0, sizeof(Mat4));

	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int m = 0; m < 4; m++) {
				dst[i][j] += a[i][m] * b[m][j];
			}
}

void Multiply(Vector3D<double>& dst, const Mat4& a, const Vector3D<double>& v) {
	memset(&dst, 0, sizeof(Vector3D<double>));

	double* d = &dst.x;
	const double* k = &v.x;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			d[i] += a[i][j] * k[j];
		}
}

void Rotate(Mat4& dst, const Mat4& a, const Vector3D<double>& axis, double radian) {
	const auto& [rx, ry, rz] = axis;
	const auto s = sin(radian);
	const auto c = cos(radian);
	Mat4 rmat = {{
		{    c + rx*rx*(1 - c), rx*ry*(1 - c) - rz*s, rx*rz*(1 - c) + ry*s, 0},
		{ ry*rx*(1 - c) + rz*s,    c + ry*ry*(1 - c), ry*rz*(1 - c) - rx*s, 0},
		{ rz*rx*(1 - c) - ry*s, rz*ry*(1 - c) + rx*s,    c + rz*rz*(1 - c), 0},
		{                    0,                    0,                    0, 1}
	}};
	Multiply(dst, rmat, a);
}
#else
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include "../syscall.h"

using namespace std;

template <class T>
struct Vector3D {
  T x, y, z;
};

template <class T>
struct Vector2D {
  T x, y;
};

void DrawObj(uint64_t layer_id);
void DrawSurface(uint64_t layer_id, int sur);
bool Sleep(unsigned long ms);

const int kScale = 50, kMargin = 10;
const int kCanvasSize = 3 * kScale + kMargin;
const array<Vector3D<int>, 8> kCube{{
  { 1,  1,  1}, { 1,  1, -1}, { 1, -1,  1}, { 1, -1, -1},
  {-1,  1,  1}, {-1,  1, -1}, {-1, -1,  1}, {-1, -1, -1}
}};
const array<array<int, 4>, 6> kSurface{{
 {0,4,6,2}, {1,3,7,5}, {0,2,3,1}, {0,1,5,4}, {4,5,7,6}, {6,7,3,2}
}};
const array<uint32_t, kSurface.size()> kColor{
  0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff
};

array<Vector3D<double>, kCube.size()> vert;
array<double, kSurface.size()> centerz4;
array<Vector2D<int>, kCube.size()> scr;

extern "C" void main(int argc, char** argv) {
  auto [layer_id, err_openwin]
	= SyscallOpenWindow(kCanvasSize + 8, kCanvasSize + 28, 10, 10, "cube");
  if (err_openwin) {
	exit(err_openwin);
  }

  int thx = 0, thy = 0, thz = 0;
  const double to_rad = 3.14159265358979323 / 0x8000;
  for (;;) {
	// 立方体を X, Y, Z 軸回りに回転
	thx = (thx + 182) & 0xffff;
	thy = (thy + 273) & 0xffff;
	thz = (thz + 364) & 0xffff;
	const double xp = cos(thx * to_rad), xa = sin(thx * to_rad);
	const double yp = cos(thy * to_rad), ya = sin(thy * to_rad);
	const double zp = cos(thz * to_rad), za = sin(thz * to_rad);
	for (int i = 0; i < kCube.size(); i++) {
	  const auto cv = kCube[i];
	  const double zt = kScale*cv.z * xp + kScale*cv.y * xa;
	  const double yt = kScale*cv.y * xp - kScale*cv.z * xa;
	  const double xt = kScale*cv.x * yp + zt          * ya;
	  vert[i].z       = zt          * yp - kScale*cv.x * ya;
	  vert[i].x       = xt          * zp - yt          * za;
	  vert[i].y       = yt          * zp + xt          * za;
	}

	// 面中心の Z 座標（を 4 倍した値）を 6 面について計算
	for (int sur = 0; sur < kSurface.size(); ++sur) {
	  centerz4[sur] = 0;
	  for (int i = 0; i < kSurface[sur].size(); ++i) {
		centerz4[sur] += vert[kSurface[sur][i]].z;
	  }
	}

	// 画面を一旦クリアし，立方体を描画
	SyscallWinFillRectangle(layer_id | LAYER_NO_REDRAW,
							4, 24, kCanvasSize, kCanvasSize, 0);
	DrawObj(layer_id | LAYER_NO_REDRAW);
	SyscallWinRedraw(layer_id);
	if (Sleep(50)) {
	  break;
	}
  }

  SyscallCloseWindow(layer_id);
  exit(0);
}

void DrawObj(uint64_t layer_id) {
  // オブジェクト座標 vert を スクリーン座標 scr に変換（画面奥が Z+）
  for (int i = 0; i < kCube.size(); i++) {
	const double t = 6*kScale / (vert[i].z + 8*kScale);
	scr[i].x = (vert[i].x * t) + kCanvasSize/2;
	scr[i].y = (vert[i].y * t) + kCanvasSize/2;
  }

  for (;;) {
	// 奥にある（= Z 座標が大きい）オブジェクトから順に描画
	double* const zmax = max_element(centerz4.begin(), centerz4.end());
	if (*zmax == numeric_limits<double>::lowest()) {
	  break;
	}
	const int sur = zmax - centerz4.begin();
	centerz4[sur] = numeric_limits<double>::lowest();

	// 法線ベクトルがこっちを向いてる面だけ描画
	const auto v0 = vert[kSurface[sur][0]],
			   v1 = vert[kSurface[sur][1]],
			   v2 = vert[kSurface[sur][2]];
	const auto e0x = v1.x - v0.x, e0y = v1.y - v0.y, // v0 --> v1
			   e1x = v2.x - v1.x, e1y = v2.y - v1.y; // v1 --> v2
	if (e0x * e1y <= e0y * e1x) {
	  DrawSurface(layer_id, sur);
	}
  }
}

void DrawSurface(uint64_t layer_id, int sur) {
  const auto& surface = kSurface[sur]; // 描画する面
  int ymin = kCanvasSize, ymax = 0; // 画面の描画範囲 [ymin, ymax]
  int y2x_up[kCanvasSize], y2x_down[kCanvasSize]; // Y, X 座標の組
  for (int i = 0; i < surface.size(); i++) {
	const auto p0 = scr[surface[(i + 3) % 4]], p1 = scr[surface[i]];
	ymin = min(ymin, p1.y);
	ymax = max(ymax, p1.y);
	if (p0.y == p1.y) {
	  continue;
	}

	int* y2x;
	int x0, y0, y1, dx;
	if (p0.y < p1.y) { // p0 --> p1 は上る方向
	  y2x = y2x_up;
	  x0 = p0.x; y0 = p0.y; y1 = p1.y; dx = p1.x - p0.x;
	} else { // p0 --> p1 は下る方向
	  y2x = y2x_down;
	  x0 = p1.x; y0 = p1.y; y1 = p0.y; dx = p0.x - p1.x;
	}

	const double m = static_cast<double>(dx) / (y1 - y0);
	const auto roundish = dx >= 0 ? static_cast<double(*)(double)>(floor)
								  : static_cast<double(*)(double)>(ceil);
	for (int y = y0; y <= y1; y++) {
	  y2x[y] = roundish(m * (y - y0) + x0);
	}
  }

  for (int y = ymin; y <= ymax; y++) {
	int p0x = min(y2x_up[y], y2x_down[y]);
	int p1x = max(y2x_up[y], y2x_down[y]);
	SyscallWinFillRectangle(layer_id, 4 + p0x, 24 + y, p1x - p0x + 1, 1, kColor[sur]);
  }
}

bool Sleep(unsigned long ms) {
  static unsigned long prev_timeout = 0;
  if (prev_timeout == 0) {
	const auto timeout = SyscallCreateTimer(TIMER_ONESHOT_REL, 1, ms);
	prev_timeout = timeout.value;
  } else {
	prev_timeout += ms;
	SyscallCreateTimer(TIMER_ONESHOT_ABS, 1, prev_timeout);
  }

  AppEvent events[1];
  for (;;) {
	SyscallReadEvent(events, 1);
	if (events[0].type == AppEvent::kTimerTimeout) {
	  return false;
	} else if (events[0].type == AppEvent::kQuit) {
	  return true;
	}
  }
}

#endif