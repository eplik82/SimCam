package main

import (
	"math"
	"sort"
)

// pt on punkt Web Mercatori maailmakoordinaatides: x, y ∈ [0, 1],
// y kasvab lõuna suunas (nagu plaatidel).
type pt struct{ X, Y float64 }

type bbox struct{ MinX, MinY, MaxX, MaxY float64 }

func emptyBBox() bbox {
	return bbox{math.Inf(1), math.Inf(1), math.Inf(-1), math.Inf(-1)}
}

func (b *bbox) extend(p pt) {
	b.MinX = math.Min(b.MinX, p.X)
	b.MinY = math.Min(b.MinY, p.Y)
	b.MaxX = math.Max(b.MaxX, p.X)
	b.MaxY = math.Max(b.MaxY, p.Y)
}

func (b bbox) valid() bool { return b.MinX <= b.MaxX && b.MinY <= b.MaxY }

func (b bbox) intersects(o bbox) bool {
	return b.MinX <= o.MaxX && o.MinX <= b.MaxX && b.MinY <= o.MaxY && o.MinY <= b.MaxY
}

func (b bbox) contains(o bbox) bool {
	return o.MinX >= b.MinX && o.MaxX <= b.MaxX && o.MinY >= b.MinY && o.MaxY <= b.MaxY
}

func (b bbox) grow(d float64) bbox { return bbox{b.MinX - d, b.MinY - d, b.MaxX + d, b.MaxY + d} }

func ringsBBox(rings [][]pt) bbox {
	b := emptyBBox()
	for _, r := range rings {
		for _, p := range r {
			b.extend(p)
		}
	}
	return b
}

func project(lon, lat float64) pt {
	lat = math.Max(-85.0511, math.Min(85.0511, lat))
	s := math.Sin(lat * math.Pi / 180)
	return pt{(lon + 180) / 360, 0.5 - math.Log((1+s)/(1-s))/(4*math.Pi)}
}

// tileBounds tagastab plaadi z/x/y ulatuse maailmakoordinaatides.
func tileBounds(z, x, y int) bbox {
	n := math.Exp2(float64(z))
	return bbox{float64(x) / n, float64(y) / n, float64(x+1) / n, float64(y+1) / n}
}

// tileRange tagastab plaadid, mis lõikuvad alaga b suumil z.
func tileRange(b bbox, z int) (x0, y0, x1, y1 int) {
	n := math.Exp2(float64(z))
	clamp := func(v float64) int { return int(math.Max(0, math.Min(n-1, math.Floor(v*n)))) }
	return clamp(b.MinX), clamp(b.MinY), clamp(b.MaxX), clamp(b.MaxY)
}

// --- Joonte ühendamine ---

// joinWays liidab joonte otspunktide kaudu ahelateks. reverse=false korral
// suunda ei pöörata (rannajoon: maa vasakul, vesi paremal).
func joinWays(ways [][]int64, reverse bool) (closed, open [][]int64) {
	byEnd := map[int64][]int{}
	for i, w := range ways {
		if len(w) < 2 {
			continue
		}
		byEnd[w[0]] = append(byEnd[w[0]], i)
		byEnd[w[len(w)-1]] = append(byEnd[w[len(w)-1]], i)
	}
	used := make([]bool, len(ways))
	take := func(node int64, wantStart bool) []int64 {
		for _, j := range byEnd[node] {
			if used[j] {
				continue
			}
			w := ways[j]
			switch {
			case wantStart && w[0] == node, !wantStart && w[len(w)-1] == node:
				used[j] = true
				return w
			case reverse:
				used[j] = true
				r := make([]int64, len(w))
				for k := range w {
					r[k] = w[len(w)-1-k]
				}
				return r
			}
		}
		return nil
	}
	for i, w := range ways {
		if used[i] || len(w) < 2 {
			continue
		}
		used[i] = true
		chain := append([]int64(nil), w...)
		for chain[0] != chain[len(chain)-1] {
			if next := take(chain[len(chain)-1], true); next != nil {
				chain = append(chain, next[1:]...)
				continue
			}
			if prev := take(chain[0], false); prev != nil {
				chain = append(append([]int64(nil), prev[:len(prev)-1]...), chain...)
				continue
			}
			break
		}
		if len(chain) >= 4 && chain[0] == chain[len(chain)-1] {
			closed = append(closed, chain)
		} else {
			open = append(open, chain)
		}
	}
	return
}

// --- Rannajoon → maismaa polügoonid ---

// coastRings ehitab rannajoonest maismaa rõngad. Suletud ahelad (saared)
// jäävad samaks; lahtised ahelad (väljavõtte servas katkenud) suletakse
// mööda ala b serva, liikudes vastupäeva (OSM: maa on joonest vasakul).
func coastRings(closed, open [][]pt, b bbox) [][]pt {
	rings := append([][]pt(nil), closed...)
	if len(open) == 0 {
		return rings
	}
	w, h := b.MaxX-b.MinX, b.MaxY-b.MinY
	// Ümbermõõdu parameeter t ∈ [0,4): lõunaserv lääne→ida, idaserv lõuna→põhja,
	// põhjaserv ida→lääne, lääneserv põhja→lõuna (geograafiliselt vastupäeva).
	snap := func(p pt) (pt, float64) {
		d := []float64{b.MaxY - p.Y, b.MaxX - p.X, p.Y - b.MinY, p.X - b.MinX}
		e := 0
		for i := range d {
			if d[i] < d[e] {
				e = i
			}
		}
		switch e {
		case 0:
			return pt{p.X, b.MaxY}, (p.X - b.MinX) / w
		case 1:
			return pt{b.MaxX, p.Y}, 1 + (b.MaxY-p.Y)/h
		case 2:
			return pt{p.X, b.MinY}, 2 + (b.MaxX-p.X)/w
		default:
			return pt{b.MinX, p.Y}, 3 + (p.Y-b.MinY)/h
		}
	}
	corners := []pt{{b.MaxX, b.MaxY}, {b.MaxX, b.MinY}, {b.MinX, b.MinY}, {b.MinX, b.MaxY}} // t = 1, 2, 3, 4
	type chain struct {
		pts        []pt
		tStart, tE float64
	}
	chains := make([]chain, len(open))
	for i, c := range open {
		s, ts := snap(c[0])
		e, te := snap(c[len(c)-1])
		pts := append([]pt{s}, c...)
		chains[i] = chain{append(pts, e), ts, te}
	}
	fwd := func(from, to float64) float64 { return math.Mod(to-from+8, 4) }
	used := make([]bool, len(chains))
	for i := range chains {
		if used[i] {
			continue
		}
		used[i] = true
		ring := append([]pt(nil), chains[i].pts...)
		cur := i
		for steps := 0; steps <= len(chains); steps++ {
			// Järgmine ahela algus vastupäeva liikudes (ka ringi enda algus).
			best, bestD := -1, math.Inf(1)
			for j := range chains {
				if j != i && used[j] {
					continue
				}
				if d := fwd(chains[cur].tE, chains[j].tStart); d < bestD {
					best, bestD = j, d
				}
			}
			// Nurgad, millest serva pidi möödutakse.
			t := chains[cur].tE
			for k := 0; k < 4; k++ {
				c := math.Floor(t) + 1 + float64(k)
				if fwd(chains[cur].tE, c) >= bestD || fwd(chains[cur].tE, c) == 0 {
					break
				}
				ring = append(ring, corners[(int(c)-1)%4])
			}
			if best == i {
				break
			}
			used[best] = true
			ring = append(ring, chains[best].pts...)
			cur = best
		}
		rings = append(rings, ring)
	}
	return rings
}

// --- Lõikamine ristkülikuga ---

// clipRing lõikab polügooni rõnga ristkülikuga (Sutherland–Hodgman).
func clipRing(r []pt, b bbox) []pt {
	type edge struct {
		inside func(pt) bool
		cross  func(a, c pt) pt
	}
	ix := func(a, c pt, x float64) pt { return pt{x, a.Y + (c.Y-a.Y)*(x-a.X)/(c.X-a.X)} }
	iy := func(a, c pt, y float64) pt { return pt{a.X + (c.X-a.X)*(y-a.Y)/(c.Y-a.Y), y} }
	edges := []edge{
		{func(p pt) bool { return p.X >= b.MinX }, func(a, c pt) pt { return ix(a, c, b.MinX) }},
		{func(p pt) bool { return p.X <= b.MaxX }, func(a, c pt) pt { return ix(a, c, b.MaxX) }},
		{func(p pt) bool { return p.Y >= b.MinY }, func(a, c pt) pt { return iy(a, c, b.MinY) }},
		{func(p pt) bool { return p.Y <= b.MaxY }, func(a, c pt) pt { return iy(a, c, b.MaxY) }},
	}
	out := r
	for _, e := range edges {
		if len(out) == 0 {
			return nil
		}
		in := out
		out = make([]pt, 0, len(in)+4)
		prev := in[len(in)-1]
		for _, p := range in {
			pi, ci := e.inside(prev), e.inside(p)
			if ci {
				if !pi {
					out = append(out, e.cross(prev, p))
				}
				out = append(out, p)
			} else if pi {
				out = append(out, e.cross(prev, p))
			}
			prev = p
		}
	}
	if len(out) < 3 {
		return nil
	}
	return out
}

// clipLine lõikab murdjoone ristkülikuga (Liang–Barsky), tulemuseks 0..n tükki.
func clipLine(l []pt, b bbox) [][]pt {
	var res [][]pt
	var cur []pt
	for i := 1; i < len(l); i++ {
		a, c := l[i-1], l[i]
		t0, t1 := 0.0, 1.0
		dx, dy := c.X-a.X, c.Y-a.Y
		ok := true
		for _, q := range [][2]float64{{-dx, a.X - b.MinX}, {dx, b.MaxX - a.X}, {-dy, a.Y - b.MinY}, {dy, b.MaxY - a.Y}} {
			p, r := q[0], q[1]
			if p == 0 {
				if r < 0 {
					ok = false
					break
				}
				continue
			}
			t := r / p
			if p < 0 {
				t0 = math.Max(t0, t)
			} else {
				t1 = math.Min(t1, t)
			}
			if t0 > t1 {
				ok = false
				break
			}
		}
		if !ok {
			if len(cur) > 1 {
				res = append(res, cur)
			}
			cur = nil
			continue
		}
		s := pt{a.X + t0*dx, a.Y + t0*dy}
		e := pt{a.X + t1*dx, a.Y + t1*dy}
		if cur == nil || t0 > 0 {
			if len(cur) > 1 {
				res = append(res, cur)
			}
			cur = []pt{s}
		}
		cur = append(cur, e)
		if t1 < 1 {
			res = append(res, cur)
			cur = nil
		}
	}
	if len(cur) > 1 {
		res = append(res, cur)
	}
	return res
}

func sortedUnique(ids []int64) []int64 {
	sort.Slice(ids, func(i, j int) bool { return ids[i] < ids[j] })
	out := ids[:0]
	for i, v := range ids {
		if i == 0 || v != ids[i-1] {
			out = append(out, v)
		}
	}
	return out
}
