# osm2raster.exe – OSM .pbf → PNG kaardiplaadid

Üks fail, ei vaja paigaldust, Dockerit, andmebaasi ega internetti. Loeb
OpenStreetMapi väljavõtte (nt [Geofabrik](https://download.geofabrik.de/europe/estonia.html)
`estonia-latest.osm.pbf`) ja joonistab sellest rasterplaadid kausta
`plaadid/{z}/{x}/{y}.png` (256×256, Web Mercator – sama skeem mis openstreetmap.org).

## Kasutamine

* **Lohista `.pbf` fail `osm2raster.exe` peale** või käivita topeltklõpsuga ja
  sisesta faili asukoht. Programm küsib suumid (vaikimisi 0–16) ja väljundkausta.
* Käsurealt:

  ```
  osm2raster.exe -min 0 -max 16 -out plaadid estonia-latest.osm.pbf
  ```

  | Lipp       | Tähendus                                              |
  |------------|-------------------------------------------------------|
  | `-min`     | väikseim suum (0)                                     |
  | `-max`     | suurim suum (16, kuni 18)                             |
  | `-out`     | väljundkaust (vaikimisi `plaadid` pbf-faili kõrval)    |
  | `-bbox`    | ainult osa alast: `minLon,minLat,maxLon,maxLat`        |
  | `-threads` | lõimede arv (vaikimisi kõik tuumad)                   |

Katkestamisel käivita uuesti – olemasolevad plaadid jäetakse vahele.

Eesti z0–16 on ~1,4 mln plaati (u 10–20 GB). Mälu kulub hinnanguliselt 2–4 GB.

## Mida joonistatakse

Värvid openstreetmap-carto stiilist: meri ja rannajoon (merele taustaks),
veekogud ja jõed, mets, põllud, niidud, sood, elamu-/tööstus-/kaubandusalad,
pargid, kalmistud, rannad, hooned (z14+), teed klasside kaupa koos äärisega,
rajad ja kergliiklusteed, raudtee, lennurajad, riigi- ja maakonnapiirid ning
kohanimed (linn z5+, alev/alevik z8+, küla z11+, …). Kohanimed paigutatakse
kogu kaardi ulatuses korraga, et need ei kattuks ega lõikuks plaatide piiril.

Ei joonistata: tänavanimesid, maja numbreid, ikoone (POI).

## Ehitamine

```
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o osm2raster.exe
go test ./...
```
