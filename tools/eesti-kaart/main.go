// eesti-kaart – laeb Eesti ala rasterkaardi plaadid kausta {z}/{x}/{y}.png.
//
// Plaadid võetakse kasutaja antud plaadiserverist (vaikimisi kohalik
// openstreetmap-tile-server aadressil localhost:8080). tile.openstreetmap.org
// on keelatud: OSM-i kasutustingimused ei luba massilist allalaadimist.
//
// Ehitamine (Windows): GOOS=windows GOARCH=amd64 go build -o eesti-kaart.exe
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// Eesti piirid koos saartega.
const (
	south = 57.45
	west  = 21.60
	north = 59.85
	east  = 28.30
)

type tile struct{ z, x, y int }

func tileXY(lat, lon float64, z int) (int, int) {
	n := math.Exp2(float64(z))
	x := int((lon + 180) / 360 * n)
	y := int((1 - math.Asinh(math.Tan(lat*math.Pi/180))/math.Pi) / 2 * n)
	return x, y
}

func tileRange(z int) (x0, y0, x1, y1 int) {
	x0, y0 = tileXY(north, west, z)
	x1, y1 = tileXY(south, east, z)
	return
}

func countTiles(minZ, maxZ int) int {
	total := 0
	for z := minZ; z <= maxZ; z++ {
		x0, y0, x1, y1 := tileRange(z)
		total += (x1 - x0 + 1) * (y1 - y0 + 1)
	}
	return total
}

var in = bufio.NewReader(os.Stdin)

func ask(question, def string) string {
	fmt.Printf("%s [%s]: ", question, def)
	line, _ := in.ReadString('\n')
	if line = strings.TrimSpace(line); line == "" {
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

func exit(code int, interactive bool) {
	if interactive {
		fmt.Print("\nVajuta Enter, et sulgeda...")
		in.ReadString('\n')
	}
	os.Exit(code)
}

func main() {
	urlFlag := flag.String("url", "", "plaadiserveri mall, nt http://localhost:8080/tile/{z}/{x}/{y}.png")
	minFlag := flag.Int("min", 0, "väikseim suum")
	maxFlag := flag.Int("max", 16, "suurim suum")
	outFlag := flag.String("out", "plaadid", "väljundkaust")
	thrFlag := flag.Int("threads", 4, "paralleelsete päringute arv")
	flag.Parse()

	// Ilma argumentideta (nt topeltklõps) küsitakse seaded ükshaaval.
	interactive := flag.NFlag() == 0
	tmpl, minZ, maxZ, out, threads := *urlFlag, *minFlag, *maxFlag, *outFlag, *thrFlag

	fmt.Println("=== Eesti OSM rasterkaart ===")
	fmt.Printf("Ala: %.2f–%.2f N, %.2f–%.2f E\n\n", south, north, west, east)
	if interactive {
		tmpl = ask("Plaadiserver", "http://localhost:8080/tile/{z}/{x}/{y}.png")
		minZ = askInt("Väikseim suum", 0, 0, 19)
		maxZ = askInt("Suurim suum", 16, minZ, 19)
		out = ask("Väljundkaust", "plaadid")
		threads = askInt("Paralleelseid päringuid", 4, 1, 64)
	}
	if tmpl == "" {
		tmpl = "http://localhost:8080/tile/{z}/{x}/{y}.png"
	}
	if minZ < 0 || maxZ > 19 || minZ > maxZ || threads < 1 {
		fmt.Println("Vigased suumi- või lõimeseaded.")
		exit(2, interactive)
	}

	u, err := url.Parse(strings.NewReplacer("{z}", "0", "{x}", "0", "{y}", "0").Replace(tmpl))
	if err != nil || u.Host == "" || !strings.Contains(tmpl, "{z}") ||
		!strings.Contains(tmpl, "{x}") || !strings.Contains(tmpl, "{y}") {
		fmt.Println("Vigane aadress – peab sisaldama {z}, {x} ja {y}.")
		exit(2, interactive)
	}
	if h := strings.ToLower(u.Hostname()); h == "openstreetmap.org" || strings.HasSuffix(h, ".openstreetmap.org") {
		fmt.Println("openstreetmap.org plaadiserverist massiline allalaadimine on keelatud")
		fmt.Println("(https://operations.osmfoundation.org/policies/tiles/) ja IP blokeeritakse.")
		fmt.Println("Kasuta oma plaadiserverit (vt README) või teenusepakkujat, kes seda lubab.")
		exit(2, interactive)
	}

	total := countTiles(minZ, maxZ)
	fmt.Printf("\nSuum %d–%d: %d plaati (umbes %.1f GB)\n", minZ, maxZ, total, float64(total)*15/1e6)
	fmt.Printf("Kaust: %s\n\n", out)

	client := &http.Client{Timeout: 5 * time.Minute}
	var done, skipped, failed, bytes atomic.Int64

	fetch := func(t tile) {
		path := filepath.Join(out, strconv.Itoa(t.z), strconv.Itoa(t.x), strconv.Itoa(t.y)+".png")
		if st, err := os.Stat(path); err == nil && st.Size() > 0 {
			skipped.Add(1)
			return
		}
		src := strings.NewReplacer("{z}", strconv.Itoa(t.z), "{x}", strconv.Itoa(t.x), "{y}", strconv.Itoa(t.y)).Replace(tmpl)
		var lastErr error
		for attempt := 0; attempt < 5; attempt++ {
			if attempt > 0 {
				time.Sleep(time.Duration(attempt*attempt) * time.Second)
			}
			n, err := download(client, src, path)
			if err == nil {
				bytes.Add(n)
				return
			}
			lastErr = err
		}
		failed.Add(1)
		fmt.Printf("\nVIGA %d/%d/%d: %v\n", t.z, t.x, t.y, lastErr)
	}

	jobs := make(chan tile, threads*4)
	var wg sync.WaitGroup
	for i := 0; i < threads; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for t := range jobs {
				fetch(t)
				done.Add(1)
			}
		}()
	}

	stop := make(chan struct{})
	go func() {
		start := time.Now()
		tick := time.NewTicker(time.Second)
		defer tick.Stop()
		for {
			select {
			case <-stop:
				return
			case <-tick.C:
				d := done.Load()
				eta := "–"
				if fresh := d - skipped.Load(); fresh > 0 {
					rate := float64(fresh) / time.Since(start).Seconds()
					eta = (time.Duration(float64(int64(total)-d)/rate) * time.Second).Round(time.Second).String()
				}
				fmt.Printf("\r%d/%d (%.1f%%)  %.0f MB  vigu %d  jäänud ~%s      ",
					d, total, float64(d)*100/float64(total), float64(bytes.Load())/1e6, failed.Load(), eta)
			}
		}
	}()

	for z := minZ; z <= maxZ; z++ {
		x0, y0, x1, y1 := tileRange(z)
		for x := x0; x <= x1; x++ {
			for y := y0; y <= y1; y++ {
				jobs <- tile{z, x, y}
			}
		}
	}
	close(jobs)
	wg.Wait()
	close(stop)

	fmt.Printf("\r%d/%d (100%%)                                                  \n", done.Load(), total)
	fmt.Printf("Valmis. Uusi: %d, olemas: %d, vigu: %d\n",
		done.Load()-skipped.Load()-failed.Load(), skipped.Load(), failed.Load())
	if failed.Load() > 0 {
		fmt.Println("Käivita programm uuesti – juba olemasolevad plaadid jäetakse vahele.")
		exit(1, interactive)
	}
	exit(0, interactive)
}

// download salvestab plaadi ajutisse faili ja nimetab selle ümber alles
// pärast edukat lõppu, et katkestamisel ei jääks poolikuid faile.
func download(c *http.Client, src, path string) (int64, error) {
	req, _ := http.NewRequest("GET", src, nil)
	req.Header.Set("User-Agent", "eesti-kaart/1.0 (SimCam tools)")
	resp, err := c.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return 0, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return 0, err
	}
	tmp := path + ".tmp"
	f, err := os.Create(tmp)
	if err != nil {
		return 0, err
	}
	n, err := io.Copy(f, resp.Body)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil && n == 0 {
		err = fmt.Errorf("tühi vastus")
	}
	if err != nil {
		os.Remove(tmp)
		return 0, err
	}
	return n, os.Rename(tmp, path)
}
