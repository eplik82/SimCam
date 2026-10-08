# eesti-kaart.exe – Eesti OSM rasterplaadid

Üks fail, ilma paigalduseta. Laeb Eesti ala (57,45–59,85 N, 21,60–28,30 E)
kaardiplaadid kausta `plaadid/{z}/{x}/{y}.png`, nt R&S PR200 SD-kaardi jaoks.

Topeltklõpsuga küsib programm seaded ükshaaval (Enter = vaikeväärtus).
Käsurealt:

```
eesti-kaart.exe -url "http://localhost:8080/tile/{z}/{x}/{y}.png" -min 0 -max 16 -out plaadid -threads 4
```

Katkestamisel käivita uuesti – olemasolevad plaadid jäetakse vahele.

| Suum | Plaate     | Maht (u)  |
|------|-----------:|----------:|
| 0–12 |      5 614 | 0,1 GB    |
| 0–14 |     86 187 | 1,3 GB    |
| 0–16 |  1 369 017 | 15–25 GB  |

## Plaadiserver

`tile.openstreetmap.org` ei luba massilist allalaadimist
([tile usage policy](https://operations.osmfoundation.org/policies/tiles/)),
seega programm seda ei kasuta. Kohalik server Dockeriga (sama stiil mis
openstreetmap.org-il):

```
curl -L -o estonia.osm.pbf https://download.geofabrik.de/europe/estonia-latest.osm.pbf
docker volume create osm-data
docker run --rm -v %cd%/estonia.osm.pbf:/data/region.osm.pbf -v osm-data:/data/database/ overv/openstreetmap-tile-server import
docker run -d -p 8080:80 -v osm-data:/data/database/ --shm-size=1g overv/openstreetmap-tile-server run
```

(Linuxis/macOS-is `%cd%` asemel `$PWD`.) Seejärel käivita `eesti-kaart.exe`
vaikeaadressiga.

## Ehitamine

```
cd tools/eesti-kaart
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o eesti-kaart.exe
```
