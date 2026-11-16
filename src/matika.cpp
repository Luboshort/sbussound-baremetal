//
// matika.cpp
//
#include "matika.h"

#define LN2	0.69314718055994530942

double Exp (double x)
{
	if (x > 700.0) x = 700.0;
	if (x < -700.0) return 0.0;
	// x = k*ln2 + r, |r| <= ln2/2
	int k = (int) (x / LN2 + (x >= 0 ? 0.5 : -0.5));
	double r = x - k * LN2;
	double fClen = 1.0, fSoucet = 1.0;
	for (int i = 1; i < 20; i++)
	{
		fClen *= r / i;
		fSoucet += fClen;
	}
	// krat 2^k
	while (k > 0) { fSoucet *= 2.0; k--; }
	while (k < 0) { fSoucet *= 0.5; k++; }
	return fSoucet;
}

double Ln (double x)
{
	if (x <= 0.0) return -1e300;
	int e = 0;
	while (x >= 2.0) { x *= 0.5; e++; }
	while (x < 1.0) { x *= 2.0; e--; }
	// ln(x) = 2 atanh(y), y = (x-1)/(x+1), |y| < 1/3
	double y = (x - 1.0) / (x + 1.0), y2 = y * y;
	double fClen = y, fSoucet = 0.0;
	for (int i = 1; i < 60; i += 2)
	{
		fSoucet += fClen / i;
		fClen *= y2;
	}
	return 2.0 * fSoucet + e * LN2;
}

double Pow (double a, double b)
{
	return Exp (b * Ln (a));
}

double Sin (double x)
{
	// redukce do -pi..pi
	double n = (double) (long long) (x / (2.0 * PI));
	x -= n * 2.0 * PI;
	if (x > PI) x -= 2.0 * PI;
	if (x < -PI) x += 2.0 * PI;
	double x2 = x * x, fClen = x, fSoucet = x;
	for (int i = 1; i < 14; i++)
	{
		fClen *= -x2 / ((2 * i) * (2 * i + 1));
		fSoucet += fClen;
	}
	return fSoucet;
}

double Cos (double x)
{
	return Sin (x + PI / 2.0);
}
