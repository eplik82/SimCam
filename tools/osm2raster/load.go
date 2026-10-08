package main

import (
	"context"
	"fmt"
	"os"
	"runtime"
	"sort"
	"strconv"

	"github.com/paulmach/osm"
	"github.com/paulmach/osm/osmpbf"
)

type feature struct {
	cls   *class
	rings [][]pt // alad: rõngad; jooned: murdjooned
	bb    bbox
}

type place struct {
	name  string
	p     pt
	style placeStyle
	pop   int
}

type mapData struct {
	features []*feature // sorteeritud cls.order järgi
	places   []place
	bounds   bbox // faili päises olev ala (või andmete ulatus)
	hasCoast bool
}

type relInfo struct {
	cls   *class
	ways  []int64
	lines bool // piir: liikmed joontena
}

func scan(path string, setup func(*osmpbf.Scanner), fn func(osm.Object)) (*osmpbf.Header, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	s := osmpbf.New(context.Background(), f, runtime.NumCPU())
	defer s.Close()
	setup(s)
	h, err := s.Header()
	if err != nil {
		return nil, err
	}
	for s.Scan() {
		fn(s.Object())
	}
	return h, s.Err()
}

func wayIDs(w *osm.Way) []int64 {
	ids := make([]int64, len(w.Nodes))
	for i, n := range w.Nodes {
		ids[i] = int64(n.ID)
	}
	return ids
}

func load(path string, maxZoom int) (*mapData, error) {
	// 1) Relatsioonid: multipolügoonid ja halduspiirid.
	var rels []relInfo
	relWays := map[int64][]int64{}
	_, err := scan(path, func(s *osmpbf.Scanner) { s.SkipNodes, s.SkipWays = true, true }, func(o osm.Object) {
		r, ok := o.(*osm.Relation)
		if !ok {
			return
		}
		var ri relInfo
		switch r.Tags.Find("type") {
		case "multipolygon":
			ri.cls = areaClass(r.Tags)
		case "boundary":
			if r.Tags.Find("boundary") == "administrative" {
				switch r.Tags.Find("admin_level") {
				case "2":
					ri.cls, ri.lines = clsBound2, true
				case "6":
					ri.cls, ri.lines = clsBound6, true
				}
			}
		}
		if ri.cls == nil || ri.cls.minZoom > maxZoom {
			return
		}
		for _, m := range r.Members {
			if m.Type == osm.TypeWay {
				ri.ways = append(ri.ways, m.Ref)
				relWays[m.Ref] = nil
			}
		}
		rels = append(rels, ri)
	})
	if err != nil {
		return nil, err
	}
	fmt.Printf("  relatsioone: %d\n", len(rels))

	// 2) Jooned.
	type wayFeat struct {
		cls   *class
		nodes []int64
	}
	var ways []wayFeat
	var coast [][]int64
	var needed []int64
	_, err = scan(path, func(s *osmpbf.Scanner) { s.SkipNodes, s.SkipRelations = true, true }, func(o osm.Object) {
		w, ok := o.(*osm.Way)
		if !ok || len(w.Nodes) < 2 {
			return
		}
		var ids []int64
		get := func() []int64 {
			if ids == nil {
				ids = wayIDs(w)
				needed = append(needed, ids...)
			}
			return ids
		}
		if _, ok := relWays[int64(w.ID)]; ok {
			relWays[int64(w.ID)] = get()
		}
		if w.Tags.Find("natural") == "coastline" {
			coast = append(coast, get())
			return
		}
		closed := w.Nodes[0].ID == w.Nodes[len(w.Nodes)-1].ID
		var c *class
		if closed && w.Tags.Find("area") != "no" && (w.Tags.Find("highway") == "" || w.Tags.Find("area") == "yes") {
			c = areaClass(w.Tags)
		}
		if c == nil {
			c = lineClass(w.Tags)
		}
		if c == nil || c.minZoom > maxZoom {
			return
		}
		ways = append(ways, wayFeat{c, get()})
	})
	if err != nil {
		return nil, err
	}
	needed = sortedUnique(needed)
	fmt.Printf("  jooni: %d, rannajoone lõike: %d, sõlmi vaja: %d\n", len(ways), len(coast), len(needed))

	// 3) Sõlmed: koordinaadid ja kohanimed.
	coords := make([]pt, len(needed))
	have := make([]bool, len(needed))
	data := &mapData{bounds: emptyBBox()}
	dataBB := emptyBBox()
	h, err := scan(path, func(s *osmpbf.Scanner) { s.SkipWays, s.SkipRelations = true, true }, func(o osm.Object) {
		n, ok := o.(*osm.Node)
		if !ok {
			return
		}
		p := project(n.Lon, n.Lat)
		dataBB.extend(p)
		id := int64(n.ID)
		if i := sort.Search(len(needed), func(i int) bool { return needed[i] >= id }); i < len(needed) && needed[i] == id {
			coords[i], have[i] = p, true
		}
		if st, ok := placeStyles[n.Tags.Find("place")]; ok && st.minZoom <= maxZoom {
			if name := n.Tags.Find("name"); name != "" {
				pop, _ := strconv.Atoi(n.Tags.Find("population"))
				data.places = append(data.places, place{name: name, p: p, style: st, pop: pop})
			}
		}
	})
	if err != nil {
		return nil, err
	}
	if h != nil && h.Bounds != nil {
		a := project(h.Bounds.MinLon, h.Bounds.MaxLat)
		b := project(h.Bounds.MaxLon, h.Bounds.MinLat)
		data.bounds = bbox{a.X, a.Y, b.X, b.Y}
	} else {
		data.bounds = dataBB
	}
	if !data.bounds.valid() {
		return nil, fmt.Errorf("failis pole andmeid")
	}

	resolve := func(ids []int64) []pt {
		out := make([]pt, 0, len(ids))
		for _, id := range ids {
			if i := sort.Search(len(needed), func(i int) bool { return needed[i] >= id }); i < len(needed) && needed[i] == id && have[i] {
				out = append(out, coords[i])
			}
		}
		return out
	}
	add := func(c *class, rings [][]pt) {
		if len(rings) == 0 {
			return
		}
		data.features = append(data.features, &feature{cls: c, rings: rings, bb: ringsBBox(rings)})
	}

	for _, w := range ways {
		if p := resolve(w.nodes); len(p) >= 2 {
			add(w.cls, [][]pt{p})
		}
	}
	for _, r := range rels {
		var members [][]int64
		for _, id := range r.ways {
			if n := relWays[id]; len(n) >= 2 {
				members = append(members, n)
			}
		}
		var rings [][]pt
		if r.lines {
			for _, m := range members {
				if p := resolve(m); len(p) >= 2 {
					rings = append(rings, p)
				}
			}
		} else {
			closed, _ := joinWays(members, true)
			for _, c := range closed {
				if p := resolve(c); len(p) >= 4 {
					rings = append(rings, p)
				}
			}
		}
		add(r.cls, rings)
	}

	// Rannajoon → maismaa; meri on taust.
	if len(coast) > 0 {
		closedIDs, openIDs := joinWays(coast, false)
		var closed, open [][]pt
		cb := data.bounds
		for _, c := range closedIDs {
			if p := resolve(c); len(p) >= 4 {
				closed = append(closed, p)
			}
		}
		for _, c := range openIDs {
			if p := resolve(c); len(p) >= 2 {
				open = append(open, p)
				for _, q := range p {
					cb.extend(q)
				}
			}
		}
		cb = cb.grow((cb.MaxX - cb.MinX) * 0.01)
		add(clsLand, coastRings(closed, open, cb))
		data.hasCoast = true
		fmt.Printf("  rannajoon: %d saart/suletud rõngast, %d lahtist ahelat\n", len(closed), len(open))
	}

	sort.SliceStable(data.features, func(i, j int) bool { return data.features[i].cls.order < data.features[j].cls.order })
	fmt.Printf("  objekte: %d, kohanimesid: %d\n", len(data.features), len(data.places))
	return data, nil
}
