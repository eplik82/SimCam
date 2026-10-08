package main

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"sync"
	"sync/atomic"

	"github.com/fogleman/gg"
	"github.com/golang/freetype/truetype"
	"golang.org/x/image/font"
	"golang.org/x/image/font/gofont/gobold"
	"golang.org/x/image/font/gofont/goregular"
)

const (
	tileSize = 256
	bufferPx = 24 // lõikamise varu plaadi ümber (joonte laius, otsad)
	parZoom  = 10 // kuni selle suumini jagatakse töö lõimedele
)

type label struct {
	text string
	x, y float64 // pikslites suumil z
	size float64
	bold bool
	col  color.RGBA
}

type faceKey struct {
	size float64
	bold bool
}

type renderer struct {
	data       *mapData
	minZ, maxZ int
	out        string
	bounds     bbox
	labels     map[uint64][]*label
	sem        chan struct{}
	done       atomic.Int64
	failed     atomic.Int64
	emptyPNG   []byte
	bg         color.RGBA
	regular    *truetype.Font
	bold       *truetype.Font
	faces      sync.Pool
	errMu      sync.Mutex
	firstErr   error
}

func newRenderer(d *mapData, minZ, maxZ int, out string, bounds bbox, threads int) *renderer {
	r := &renderer{data: d, minZ: minZ, maxZ: maxZ, out: out, bounds: bounds,
		sem: make(chan struct{}, threads), labels: map[uint64][]*label{}, bg: colLand}
	if d.hasCoast {
		r.bg = colSea
	}
	r.regular, _ = truetype.Parse(goregular.TTF)
	r.bold, _ = truetype.Parse(gobold.TTF)
	r.faces.New = func() any { return map[faceKey]font.Face{} }
	img := image.NewRGBA(image.Rect(0, 0, tileSize, tileSize))
	for i := 0; i < len(img.Pix); i += 4 {
		img.Pix[i], img.Pix[i+1], img.Pix[i+2], img.Pix[i+3] = r.bg.R, r.bg.G, r.bg.B, 255
	}
	r.emptyPNG = encodePNG(img)
	return r
}

func (r *renderer) face(cache map[faceKey]font.Face, k faceKey) font.Face {
	if f, ok := cache[k]; ok {
		return f
	}
	ttf := r.regular
	if k.bold {
		ttf = r.bold
	}
	f := truetype.NewFace(ttf, &truetype.Options{Size: k.size, Hinting: font.HintingFull})
	cache[k] = f
	return f
}

func tileKey(z, x, y int) uint64 { return uint64(z)<<58 | uint64(x)<<29 | uint64(y) }

func countTiles(b bbox, minZ, maxZ int) int64 {
	var n int64
	for z := minZ; z <= maxZ; z++ {
		x0, y0, x1, y1 := tileRange(b, z)
		n += int64(x1-x0+1) * int64(y1-y0+1)
	}
	return n
}

// placeLabels paigutab kohanimed iga suumi jaoks kogu kaardi ulatuses korraga,
// et nimed ei kattuks ega lõikuks plaatide piiril erinevalt.
func (r *renderer) placeLabels() {
	pl := append([]place(nil), r.data.places...)
	sort.SliceStable(pl, func(i, j int) bool {
		if pl[i].style.rank != pl[j].style.rank {
			return pl[i].style.rank < pl[j].style.rank
		}
		if pl[i].pop != pl[j].pop {
			return pl[i].pop > pl[j].pop
		}
		return pl[i].name < pl[j].name
	})
	cache := map[faceKey]font.Face{}
	widths := make([]float64, len(pl))
	for i, p := range pl {
		f := r.face(cache, faceKey{p.style.size, p.style.bold})
		widths[i] = float64(font.MeasureString(f, p.name)) / 64
	}
	const cell = 128.0
	for z := r.minZ; z <= r.maxZ; z++ {
		scale := tileSize * math.Exp2(float64(z))
		grid := map[[2]int][]bbox{}
		for i, p := range pl {
			if p.style.minZoom > z {
				continue
			}
			x, y := p.p.X*scale, p.p.Y*scale
			hw, hh := widths[i]/2+3, p.style.size/2+3
			b := bbox{x - hw, y - hh, x + hw, y + hh}
			cx0, cy0 := int(math.Floor(b.MinX/cell)), int(math.Floor(b.MinY/cell))
			cx1, cy1 := int(math.Floor(b.MaxX/cell)), int(math.Floor(b.MaxY/cell))
			hit := false
			for cx := cx0; cx <= cx1 && !hit; cx++ {
				for cy := cy0; cy <= cy1 && !hit; cy++ {
					for _, o := range grid[[2]int{cx, cy}] {
						if o.intersects(b) {
							hit = true
							break
						}
					}
				}
			}
			if hit {
				continue
			}
			for cx := cx0; cx <= cx1; cx++ {
				for cy := cy0; cy <= cy1; cy++ {
					grid[[2]int{cx, cy}] = append(grid[[2]int{cx, cy}], b)
				}
			}
			l := &label{p.name, x, y, p.style.size, p.style.bold, p.style.col}
			for tx := int(b.MinX / tileSize); tx <= int(b.MaxX/tileSize); tx++ {
				for ty := int(b.MinY / tileSize); ty <= int(b.MaxY/tileSize); ty++ {
					k := tileKey(z, tx, ty)
					r.labels[k] = append(r.labels[k], l)
				}
			}
		}
	}
}

// overlaps on range rangest väljaspool olevaid plaate välistav kontroll
// (sama reegel mis tileRange-il).
func overlaps(t, b bbox) bool {
	return t.MinX <= b.MaxX && b.MinX < t.MaxX && t.MinY <= b.MaxY && b.MinY < t.MaxY
}

func clipFeatures(feats []*feature, b bbox) []*feature {
	out := make([]*feature, 0, len(feats))
	for _, f := range feats {
		if !f.bb.intersects(b) {
			continue
		}
		if b.contains(f.bb) {
			out = append(out, f)
			continue
		}
		var rings [][]pt
		for _, ring := range f.rings {
			if f.cls.area {
				if c := clipRing(ring, b); c != nil {
					rings = append(rings, c)
				}
			} else {
				rings = append(rings, clipLine(ring, b)...)
			}
		}
		if len(rings) > 0 {
			out = append(out, &feature{cls: f.cls, rings: rings, bb: ringsBBox(rings)})
		}
	}
	return out
}

// tile joonistab plaadi z/x/y ja seejärel rekursiivselt selle alamplaadid;
// iga tase saab ainult enda alasse lõigatud geomeetria. release vabastab
// lõimekoha, kui plaat jääb alamplaatide järel ootama.
func (r *renderer) tile(z, x, y int, feats []*feature, release func()) {
	if z >= r.minZ {
		r.draw(z, x, y, feats)
	}
	if z >= r.maxZ {
		return
	}
	var wg sync.WaitGroup
	for i := 0; i < 4; i++ {
		cx, cy := 2*x+i%2, 2*y+i/2
		cb := tileBounds(z+1, cx, cy)
		if !overlaps(cb, r.bounds) {
			continue
		}
		child := clipFeatures(feats, cb.grow((cb.MaxX-cb.MinX)*bufferPx/tileSize))
		// Vaba lõime olemasolul alamplaat paralleelselt, muidu samas lõimes.
		select {
		case r.sem <- struct{}{}:
			wg.Add(1)
			go func() {
				var once sync.Once
				rel := func() { once.Do(func() { <-r.sem }) }
				defer wg.Done()
				defer rel()
				r.tile(z+1, cx, cy, child, rel)
			}()
		default:
			r.tile(z+1, cx, cy, child, release)
		}
	}
	release()
	wg.Wait()
}

func addPath(dc *gg.Context, rings [][]pt, scale, ox, oy float64, closed bool) {
	for _, ring := range rings {
		var lx, ly float64
		n := 0
		for i, p := range ring {
			px, py := p.X*scale-ox, p.Y*scale-oy
			if n > 0 && i != len(ring)-1 && math.Abs(px-lx) < 0.4 && math.Abs(py-ly) < 0.4 {
				continue
			}
			if n == 0 {
				dc.MoveTo(px, py)
			} else {
				dc.LineTo(px, py)
			}
			lx, ly = px, py
			n++
		}
		if closed {
			dc.ClosePath()
		}
	}
}

func (r *renderer) draw(z, x, y int, feats []*feature) {
	defer r.done.Add(1)
	path := filepath.Join(r.out, strconv.Itoa(z), strconv.Itoa(x), strconv.Itoa(y)+".png")
	if st, err := os.Stat(path); err == nil && st.Size() > 0 {
		return // jätkamine: plaat on juba olemas
	}
	scale := tileSize * math.Exp2(float64(z))
	ox, oy := float64(x)*tileSize, float64(y)*tileSize
	labels := r.labels[tileKey(z, x, y)]

	dc := gg.NewContext(tileSize, tileSize)
	dc.SetColor(r.bg)
	dc.Clear()
	dc.SetLineCap(gg.LineCapRound)
	dc.SetLineJoin(gg.LineJoinRound)
	drawn := false

	for _, f := range feats {
		c := f.cls
		if !c.area || c.minZoom > z || ((f.bb.MaxX-f.bb.MinX)*scale < 1 && (f.bb.MaxY-f.bb.MinY)*scale < 1) {
			continue
		}
		addPath(dc, f.rings, scale, ox, oy, true)
		if c.nonzero {
			dc.SetFillRuleWinding()
		} else {
			dc.SetFillRuleEvenOdd()
		}
		dc.SetColor(c.fill)
		if c.stroke.A > 0 && z >= 15 {
			dc.FillPreserve()
			dc.SetColor(c.stroke)
			dc.SetLineWidth(0.8)
			dc.Stroke()
		} else {
			dc.Fill()
		}
		drawn = true
	}
	for pass := 0; pass < 2; pass++ {
		for _, f := range feats {
			c := f.cls
			if c.area || c.minZoom > z {
				continue
			}
			w := c.width(z)
			if pass == 0 {
				if c.stroke.A == 0 || z < c.casingZ {
					continue
				}
				dc.SetColor(c.stroke)
				w += 1.5
				if z >= 15 {
					w += 1
				}
			} else {
				dc.SetColor(c.fill)
			}
			dc.SetLineWidth(w)
			if pass == 1 && len(c.dash) > 0 {
				dc.SetDash(c.dash...)
			} else {
				dc.SetDash()
			}
			addPath(dc, f.rings, scale, ox, oy, false)
			dc.Stroke()
			drawn = true
		}
	}
	dc.SetDash()

	if len(labels) > 0 {
		cache := r.faces.Get().(map[faceKey]font.Face)
		for _, l := range labels {
			dc.SetFontFace(r.face(cache, faceKey{l.size, l.bold}))
			lx, ly := l.x-ox, l.y-oy
			dc.SetColor(color.RGBA{255, 255, 255, 220})
			for _, d := range [][2]float64{{-1.5, 0}, {1.5, 0}, {0, -1.5}, {0, 1.5}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}} {
				dc.DrawStringAnchored(l.text, lx+d[0], ly+d[1], 0.5, 0.35)
			}
			dc.SetColor(l.col)
			dc.DrawStringAnchored(l.text, lx, ly, 0.5, 0.35)
		}
		r.faces.Put(cache)
		drawn = true
	}

	data := r.emptyPNG
	if drawn {
		data = encodePNG(dc.Image())
	}
	if err := writeFile(path, data); err != nil {
		r.failed.Add(1)
		r.errMu.Lock()
		if r.firstErr == nil {
			r.firstErr = err
		}
		r.errMu.Unlock()
	}
}

func encodePNG(img image.Image) []byte {
	var b bytes.Buffer
	(&png.Encoder{CompressionLevel: png.BestSpeed}).Encode(&b, img)
	return b.Bytes()
}

func writeFile(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, path)
}
