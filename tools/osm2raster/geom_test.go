package main

import (
	"math"
	"testing"
)

// area tagastab rõngaste nonzero-täite pindala ruudustiku proovidega.
func filledArea(rings [][]pt, b bbox) float64 {
	const n = 200
	hit := 0
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			p := pt{b.MinX + (float64(i)+0.5)*(b.MaxX-b.MinX)/n, b.MinY + (float64(j)+0.5)*(b.MaxY-b.MinY)/n}
			w := 0
			for _, r := range rings {
				for k := range r {
					a, c := r[k], r[(k+1)%len(r)]
					if a.Y <= p.Y && c.Y > p.Y && (c.X-a.X)*(p.Y-a.Y)-(p.X-a.X)*(c.Y-a.Y) > 0 {
						w++
					} else if a.Y > p.Y && c.Y <= p.Y && (c.X-a.X)*(p.Y-a.Y)-(p.X-a.X)*(c.Y-a.Y) < 0 {
						w--
					}
				}
			}
			if w != 0 {
				hit++
			}
		}
	}
	return float64(hit) / (n * n)
}

func TestCoastRings(t *testing.T) {
	b := bbox{0, 0, 1, 1} // y kasvab lõunasse
	cases := []struct {
		name string
		open [][]pt
		want float64
	}{
		// Maa on joonest vasakul: lääne suunas liikudes on maa lõunas.
		{"lõunapool maa", [][]pt{{{0.9, 0.5}, {0.1, 0.5}}}, 0.5},
		{"põhjapool maa", [][]pt{{{0.1, 0.5}, {0.9, 0.5}}}, 0.5},
		// Poolsaar läänest: maa x<0.3, rannajoon lõunast põhja.
		{"läänes maa", [][]pt{{{0.3, 0.95}, {0.3, 0.05}}}, 0.3},
		// Kaks lahtist ahelat: maa idas ja läänes, väin keskel.
		{"väin", [][]pt{{{0.3, 0.95}, {0.3, 0.05}}, {{0.7, 0.05}, {0.7, 0.95}}}, 0.6},
		// Nurk: maa kirdes.
		{"nurk", [][]pt{{{0.5, 0.05}, {0.5, 0.5}, {0.95, 0.5}}}, 0.25},
	}
	for _, c := range cases {
		got := filledArea(coastRings(nil, c.open, b), b)
		if math.Abs(got-c.want) > 0.03 {
			t.Errorf("%s: pindala %.3f, oodati %.3f", c.name, got, c.want)
		}
	}
}

func TestJoinWays(t *testing.T) {
	closed, open := joinWays([][]int64{{3, 4}, {1, 2, 3}, {4, 1}}, false)
	if len(closed) != 1 || len(open) != 0 || len(closed[0]) != 5 {
		t.Fatalf("closed=%v open=%v", closed, open)
	}
	closed, _ = joinWays([][]int64{{1, 2}, {3, 2}, {3, 1}}, true)
	if len(closed) != 1 {
		t.Fatalf("pööramisega ei sulgunud: %v", closed)
	}
	_, open = joinWays([][]int64{{1, 2}, {3, 2}}, false)
	if len(open) != 2 {
		t.Fatalf("ilma pööramiseta ei tohi liita: %v", open)
	}
}

func TestClip(t *testing.T) {
	b := bbox{0, 0, 1, 1}
	r := clipRing([]pt{{-1, -1}, {2, -1}, {2, 2}, {-1, 2}}, b)
	if a := filledArea([][]pt{r}, b); a < 0.99 {
		t.Errorf("ruudu lõikamine: %v", a)
	}
	l := clipLine([]pt{{-1, 0.5}, {0.5, 0.5}, {0.5, 2}, {2, 2}}, b)
	if len(l) != 1 || l[0][0] != (pt{0, 0.5}) || l[0][len(l[0])-1] != (pt{0.5, 1}) {
		t.Errorf("joone lõikamine: %v", l)
	}
}
