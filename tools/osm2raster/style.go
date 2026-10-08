package main

import (
	"image/color"
	"math"
	"strconv"

	"github.com/paulmach/osm"
)

// Värvid on võetud openstreetmap-carto stiilist (openstreetmap.org kaart).

type class struct {
	name    string
	area    bool
	fill    color.RGBA // ala täide või joone värv
	stroke  color.RGBA // ala äärejoon / joone ääris (casing); A=0 → puudub
	minZoom int
	order   int       // joonistamise järjekord (väiksem enne)
	w16     float64   // joone laius pikslites suumil 16
	wMin    float64   // väikseim laius
	fixed   bool      // laius ei sõltu suumist
	dash    []float64 // katkendjoon
	casingZ int       // ääris alates sellest suumist
	nonzero bool      // täitereegel nonzero (rannajoon), muidu even-odd
}

func hex(s string) color.RGBA {
	v, _ := strconv.ParseUint(s[1:], 16, 32)
	return color.RGBA{uint8(v >> 16), uint8(v >> 8), uint8(v), 255}
}

func (c *class) width(z int) float64 {
	if c.fixed {
		return c.w16
	}
	return math.Max(c.wMin, c.w16*math.Exp2(float64(z-16)/2))
}

var (
	colSea  = hex("#aad3df")
	colLand = hex("#f2efe9")
)

func area(name, fill string, minZoom, order int) *class {
	return &class{name: name, area: true, fill: hex(fill), minZoom: minZoom, order: order}
}

func line(name, col, casing string, minZoom, order int, w16, wMin float64, casingZ int, dash ...float64) *class {
	c := &class{name: name, fill: hex(col), minZoom: minZoom, order: order, w16: w16, wMin: wMin, casingZ: casingZ, dash: dash}
	if casing != "" {
		c.stroke = hex(casing)
	}
	return c
}

var (
	clsLand = &class{name: "land", area: true, fill: colLand, order: 0, nonzero: true}

	clsFarmland    = area("farmland", "#eef0d5", 9, 10)
	clsMeadow      = area("meadow", "#cdebb0", 10, 11)
	clsHeath       = area("heath", "#d6d99f", 10, 11)
	clsScrub       = area("scrub", "#c8d7ab", 10, 11)
	clsWetland     = area("wetland", "#d4e6d6", 9, 12)
	clsFarmyard    = area("farmyard", "#f5dcba", 11, 12)
	clsForest      = area("forest", "#add19e", 6, 14)
	clsResidential = area("residential", "#e0dfdf", 10, 15)
	clsCommercial  = area("commercial", "#f2dad9", 10, 16)
	clsRetail      = area("retail", "#ffd6d1", 10, 16)
	clsIndustrial  = area("industrial", "#ebdbe8", 10, 16)
	clsConstruct   = area("construction", "#c7c7b4", 11, 16)
	clsMilitary    = area("military", "#f3e3dd", 10, 16)
	clsAerodrome   = area("aerodrome", "#e9e7e2", 10, 16)
	clsQuarry      = area("quarry", "#c5c3c3", 11, 17)
	clsLandfill    = area("landfill", "#b6b592", 11, 17)
	clsAllotments  = area("allotments", "#c9e1bf", 12, 17)
	clsOrchard     = area("orchard", "#aedfa3", 12, 17)
	clsCemetery    = area("cemetery", "#aacbaf", 12, 17)
	clsPark        = area("park", "#c8facc", 11, 17)
	clsGolf        = area("golf", "#def6c0", 12, 17)
	clsPitch       = area("pitch", "#88e0be", 13, 18)
	clsBeach       = area("beach", "#fff1ba", 11, 18)
	clsRock        = area("rock", "#eee5dc", 11, 18)
	clsParking     = area("parking", "#eeeeee", 15, 19)
	clsApron       = area("apron", "#dadae0", 12, 19)
	clsWater       = area("water", "#aad3df", 5, 30)
	clsBuilding    = &class{name: "building", area: true, fill: hex("#d9d0c9"), stroke: hex("#c4b6ab"), minZoom: 14, order: 40}

	clsDitch    = line("ditch", "#aad3df", "", 14, 100, 1.2, 0.6, 99)
	clsStream   = line("stream", "#aad3df", "", 13, 101, 2, 0.8, 99)
	clsCanal    = line("canal", "#aad3df", "", 10, 102, 7, 0.8, 99)
	clsRiver    = line("river", "#aad3df", "", 8, 103, 9, 0.8, 99)
	clsRunway   = line("runway", "#bbbbcc", "", 11, 110, 30, 1, 99)
	clsTaxiway  = line("taxiway", "#bbbbcc", "", 13, 111, 8, 1, 99)
	clsPath     = line("path", "#fa8072", "", 14, 120, 1.5, 1, 99, 3, 2)
	clsCycleway = line("cycleway", "#0000ff", "", 14, 121, 1.5, 1, 99, 3, 2)
	clsTrack    = line("track", "#996600", "", 13, 122, 2, 1, 99, 5, 3)
	clsService  = line("service", "#ffffff", "#bbbbbb", 14, 130, 5, 1, 14)
	clsPedest   = line("pedestrian", "#dddde8", "#999999", 13, 131, 6, 1, 14)
	clsMinor    = line("minor", "#ffffff", "#bbbbbb", 12, 132, 8, 1, 13)
	clsTertiary = line("tertiary", "#ffffff", "#8f8f8f", 10, 133, 10, 1, 12)
	clsSecond   = line("secondary", "#f7fabf", "#707d05", 9, 134, 11, 1, 11)
	clsPrimary  = line("primary", "#fcd6a4", "#a06b00", 7, 135, 12, 1, 11)
	clsTrunk    = line("trunk", "#f9b29c", "#c84e2f", 5, 136, 13, 1, 11)
	clsMotorway = line("motorway", "#e892a2", "#dc2a67", 5, 137, 14, 1.2, 11)
	clsTram     = line("tram", "#444444", "", 13, 140, 1.5, 1, 99)
	clsRail     = line("rail", "#707070", "", 8, 141, 3, 0.8, 99)
	clsBound6   = &class{name: "boundary6", fill: hex("#a37da1"), minZoom: 9, order: 150, w16: 1, fixed: true, dash: []float64{4, 3}}
	clsBound2   = &class{name: "boundary2", fill: hex("#8d618b"), minZoom: 0, order: 151, w16: 2, fixed: true, dash: []float64{6, 3}}
)

func areaClass(t osm.Tags) *class {
	if b := t.Find("building"); b != "" && b != "no" {
		return clsBuilding
	}
	switch t.Find("natural") {
	case "water":
		return clsWater
	case "wood":
		return clsForest
	case "wetland":
		return clsWetland
	case "scrub":
		return clsScrub
	case "heath":
		return clsHeath
	case "grassland":
		return clsMeadow
	case "beach", "sand":
		return clsBeach
	case "bare_rock", "scree", "shingle":
		return clsRock
	}
	switch t.Find("waterway") {
	case "riverbank", "dock":
		return clsWater
	}
	switch t.Find("landuse") {
	case "forest":
		return clsForest
	case "farmland":
		return clsFarmland
	case "meadow", "grass", "village_green":
		return clsMeadow
	case "residential":
		return clsResidential
	case "commercial":
		return clsCommercial
	case "retail":
		return clsRetail
	case "industrial", "railway":
		return clsIndustrial
	case "construction", "brownfield", "greenfield":
		return clsConstruct
	case "allotments":
		return clsAllotments
	case "orchard", "vineyard", "plant_nursery":
		return clsOrchard
	case "cemetery":
		return clsCemetery
	case "reservoir", "basin":
		return clsWater
	case "quarry":
		return clsQuarry
	case "landfill":
		return clsLandfill
	case "military":
		return clsMilitary
	case "farmyard":
		return clsFarmyard
	case "recreation_ground":
		return clsPark
	}
	switch t.Find("leisure") {
	case "park", "garden", "playground", "dog_park":
		return clsPark
	case "pitch", "track", "stadium":
		return clsPitch
	case "golf_course":
		return clsGolf
	}
	switch t.Find("amenity") {
	case "parking":
		return clsParking
	case "grave_yard":
		return clsCemetery
	}
	switch t.Find("aeroway") {
	case "aerodrome":
		return clsAerodrome
	case "apron":
		return clsApron
	}
	return nil
}

func lineClass(t osm.Tags) *class {
	switch t.Find("highway") {
	case "motorway", "motorway_link":
		return clsMotorway
	case "trunk", "trunk_link":
		return clsTrunk
	case "primary", "primary_link":
		return clsPrimary
	case "secondary", "secondary_link":
		return clsSecond
	case "tertiary", "tertiary_link":
		return clsTertiary
	case "residential", "unclassified", "living_street", "road":
		return clsMinor
	case "pedestrian":
		return clsPedest
	case "service":
		return clsService
	case "track":
		return clsTrack
	case "path", "footway", "bridleway", "steps":
		return clsPath
	case "cycleway":
		return clsCycleway
	}
	switch t.Find("waterway") {
	case "river":
		return clsRiver
	case "canal":
		return clsCanal
	case "stream":
		return clsStream
	case "ditch", "drain":
		return clsDitch
	}
	switch t.Find("railway") {
	case "rail", "narrow_gauge", "preserved":
		return clsRail
	case "tram", "light_rail":
		return clsTram
	}
	switch t.Find("aeroway") {
	case "runway":
		return clsRunway
	case "taxiway":
		return clsTaxiway
	}
	return nil
}

// --- Kohanimed ---

type placeStyle struct {
	rank    int
	minZoom int
	size    float64
	bold    bool
	col     color.RGBA
}

var placeStyles = map[string]placeStyle{
	"city":              {0, 5, 16, true, hex("#000000")},
	"town":              {1, 8, 13, true, hex("#000000")},
	"village":           {2, 11, 12, false, hex("#000000")},
	"suburb":            {3, 12, 12, false, hex("#555555")},
	"hamlet":            {4, 13, 11, false, hex("#000000")},
	"quarter":           {5, 14, 11, false, hex("#555555")},
	"neighbourhood":     {5, 14, 11, false, hex("#555555")},
	"isolated_dwelling": {6, 15, 10, false, hex("#333333")},
	"farm":              {6, 15, 10, false, hex("#333333")},
	"locality":          {7, 15, 10, false, hex("#555555")},
}
