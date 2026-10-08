// osm2raster – teeb OpenStreetMapi .osm.pbf failist rasterkaardi plaadid
// kausta {z}/{x}/{y}.png. Üks fail, ei vaja Dockerit, andmebaasi ega internetti.
//
// Kasutamine: lohista .pbf fail exe peale või käivita topeltklõpsuga ja
// sisesta faili asukoht. Käsurealt:
//
//	osm2raster.exe -min 0 -max 16 -out plaadid estonia-latest.osm.pbf
//
// Ehitamine (Windows): GOOS=windows GOARCH=amd64 go build -o osm2raster.exe
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

var stdin = bufio.NewReader(os.Stdin)

func ask(question, def string) string {
	if def != "" {
		fmt.Printf("%s [%s]: ", question, def)
	} else {
		fmt.Printf("%s: ", question)
	}
	line, _ := stdin.ReadString('\n')
	if line = strings.Trim(strings.TrimSpace(line), `"'`); line == "" {
		return def
	}
	return line
}

func askInt(question string, def, lo, hi int) int {
	for {
		v, err := strconv.Atoi(ask(question, strconv.Itoa(def)))
		if err == nil && v >= lo && v <= hi {
			return v
		}
		fmt.Printf("  Sisesta arv vahemikus %d–%d.\n", lo, hi)
	}
}

func parseBBox(s string) (bbox, error) {
	parts := strings.Split(s, ",")
	if len(parts) != 4 {
		return bbox{}, fmt.Errorf("vorming: minLon,minLat,maxLon,maxLat")
	}
	var v [4]float64
	for i, p := range parts {
		f, err := strconv.ParseFloat(strings.TrimSpace(p), 64)
		if err != nil {
			return bbox{}, err
		}
		v[i] = f
	}
	a, b := project(v[0], v[3]), project(v[2], v[1])
	return bbox{a.X, a.Y, b.X, b.Y}, nil
}

func main() {
	minFlag := flag.Int("min", 0, "väikseim suum")
	maxFlag := flag.Int("max", 16, "suurim suum (kuni 18)")
	outFlag := flag.String("out", "", "väljundkaust (vaikimisi 'plaadid' pbf-faili kõrval)")
	bboxFlag := flag.String("bbox", "", "ala piirid minLon,minLat,maxLon,maxLat (vaikimisi kogu fail)")
	thrFlag := flag.Int("threads", runtime.NumCPU(), "lõimede arv")
	flag.Parse()

	interactive := flag.NFlag() == 0
	pause := func(code int) {
		if interactive {
			fmt.Print("\nVajuta Enter, et sulgeda...")
			stdin.ReadString('\n')
		}
		os.Exit(code)
	}

	fmt.Println("=== osm2raster: OSM .pbf → PNG kaardiplaadid ===")
	in := flag.Arg(0)
	minZ, maxZ, out, threads := *minFlag, *maxFlag, *outFlag, *thrFlag
	if interactive {
		if in == "" {
			in = ask("PBF-fail (nt estonia-latest.osm.pbf)", "")
		} else {
			fmt.Println("Fail:", in)
		}
		minZ = askInt("Väikseim suum", 0, 0, 18)
		maxZ = askInt("Suurim suum", 16, minZ, 18)
	}
	if in == "" {
		fmt.Println("PBF-faili ei antud.")
		pause(2)
	}
	if _, err := os.Stat(in); err != nil {
		fmt.Println("Faili ei leitud:", in)
		pause(2)
	}
	if out == "" {
		out = filepath.Join(filepath.Dir(in), "plaadid")
	}
	if interactive {
		out = ask("Väljundkaust", out)
	}
	if minZ < 0 || maxZ > 18 || minZ > maxZ || threads < 1 {
		fmt.Println("Vigased seaded.")
		pause(2)
	}

	start := time.Now()
	fmt.Println("\nLoen andmeid (3 läbimist)...")
	data, err := load(in, maxZ)
	if err != nil {
		fmt.Println("Viga faili lugemisel:", err)
		pause(1)
	}
	bounds := data.bounds
	if *bboxFlag != "" {
		if bounds, err = parseBBox(*bboxFlag); err != nil {
			fmt.Println("Vigane -bbox:", err)
			pause(2)
		}
	}
	fmt.Printf("Andmed loetud: %s\n", time.Since(start).Round(time.Second))

	r := newRenderer(data, minZ, maxZ, out, bounds, threads)
	r.placeLabels()
	total := countTiles(bounds, minZ, maxZ)
	fmt.Printf("\nSuum %d–%d: %d plaati → %s\n", minZ, maxZ, total, out)

	stop := make(chan struct{})
	finished := make(chan struct{})
	go func() {
		defer close(finished)
		t0 := time.Now()
		tick := time.NewTicker(time.Second)
		defer tick.Stop()
		for {
			select {
			case <-stop:
				return
			case <-tick.C:
				d := r.done.Load()
				eta := "–"
				if d > 0 {
					eta = (time.Duration(float64(time.Since(t0)) / float64(d) * float64(total-d))).Round(time.Second).String()
				}
				fmt.Printf("\r%d/%d (%.1f%%)  jäänud ~%s        ", d, total, float64(d)*100/float64(total), eta)
			}
		}
	}()

	r.tile(0, 0, 0, data.features, func() {})
	close(stop)
	<-finished

	fmt.Printf("\r%d/%d (100%%)                                   \n", r.done.Load(), total)
	if n := r.failed.Load(); n > 0 {
		fmt.Printf("%d plaadi salvestamine ebaõnnestus, esimene viga: %v\n", n, r.firstErr)
		fmt.Println("Käivita uuesti – valmis plaadid jäetakse vahele.")
		pause(1)
	}
	fmt.Printf("Valmis %s jooksul. Plaadid: %s\n", time.Since(start).Round(time.Second), out)
	pause(0)
}
