# Bewertung: Physik-/Timing-Fixes (Fork gegen upstream) und D3D11-Renderer

Stand 14.10.2026. Verglichen wurden Fork `main` (`a40b3811`) und upstream `main`
(`4ff637bc`, 0.0.6.5), gemeinsamer Ausgangspunkt `1a18f4a7`. Reine Code- und
Diff-Analyse: Das Spiel lief dafür nicht, Messungen stehen als Plan in Abschnitt 1.7.

## Kurzfassung

- **Physik und Spieluhr:** Upstream ist bei 30 und 60 FPS eine **echte Obermenge**
  unseres Stands. `tick_count.cpp` ist byte-identisch, `physics_step.cpp` unterscheidet
  sich nur durch den neuen Halbschritt-Code für den fraktionalen Modus und einen
  Helfer-Umbau. Bei 30/60 FPS verhält sich die Physik exakt wie bei uns. Oberhalb von
  60 FPS bringt upstream neue Funktionen mit, die wir nicht haben.
- **„In halves“:** Die Aussage aus `2d38e589` trifft auf unseren `steady`-Modus bei 30 und
  60 FPS **nicht** im Sinne von 8,3–12,5-ms-Schritten zu. Bei ganzen Vblanks halbiert
  `steady` nur 2-Vblank-Frames (zu 2 × 16,7 ms, also harmlos) und 3-Vblank-Frames
  (zu 2 × 25 ms). Die 8,3–12,5-ms-Schritte entstehen nur bei fraktionalen Frame-Zeiten
  (upstreams Modus über 60 FPS) und bei `legacy`.
- **`tick_count_precise`** bleibt bei 30/60 FPS nötig und ist in upstream unverändert
  aktiv. `sub_8270CE10` ist ein Sprung-Thunk auf `sub_824324A8`, also dieselbe Funktion.
  Keine der beiden Seiten hat einen Fehler.
- **Empfehlung:** Beim nächsten Abgleich upstreams Timing komplett übernehmen. Behalten
  muss der Fork dabei nur seine eigenen Ergänzungen außerhalb des Timings
  (siehe Aufgabe 3).
- **D3D11:** siehe Abschnitt 2.

---

## 1. Physik und Timing

### 1.1 Was der Havok-Schritt `sub_827A55C0` tatsächlich tut

Aus dem generierten Code (`simpsons/generated/default/simpsons_recomp.45.cpp:23858`):

```
H  = statisch 0x82DFFA58 (Guard 0x82DFFA5C), beim 1. Aufruf aus 0x82159240 = 4/59,94 s
S  = 0x8215923C = 1/59,94 s           (Schrittweite für lange Frames)
T  = 0x82159238 (Fork/upstream: auf 1/59,94 s gepatcht, ab Werk 1/29,97 s)

setFrameTimeMarker(world, f1)         // sub_82AA0798
if f1 <= T:   wenn hinter dem Marker → genau EIN Schritt der Länge T
else:         step = (f1 < H) ? f1 * 0.5 : S
              solange nicht am Marker → Schritt der Länge step
```

Wichtig: Der Zweig `f1 > T` macht keine festen „zwei Schritte“, sondern **Schritte der
Länge `step`, bis die Simulation den Marker erreicht hat**. Mit `f1 / 2` sind das in der
Praxis zwei.

Gleitkommawerte (float32):

| Größe | Bits | ms |
|---|---|---|
| T (gepatcht) = S | `0x3C88AB86` | 16,68335 |
| 1 Vblank, wie die Uhr ihn rechnet (`float(1000/59,94) * 0,001`) | `0x3C88AB86` | 16,68335, also **= T** |
| H | `0x3D88AB86` | 66,7334 (= 4 × T exakt, Faktor 2er-Potenz) |
| geglättete Frame-Zeit bei `legacy` | `0x3C88AB89` | 16,68336, also **> T** |

### 1.2 Schritte pro Frame je Modus und Bildrate

„Vblanks“ = was die Spieluhr (`sub_82690D60`) für den Frame zählt.

| Situation | `original` | `legacy` | Fork `steady` | upstream `steady` |
|---|---|---|---|---|
| 60 FPS (1 Vblank) | 1 × 33,4 ms jeden 2. Frame (Physik 30 Hz) | **2 × 8,3 ms** (f1 = `…89` > T) | 1 × 16,7 ms | 1 × 16,7 ms (identisch) |
| 30 FPS (2 Vblanks) | 2 × 16,7 oder 1 × 33,4 ms, je nach Glättungsrichtung | 2 × 16,7 ms (≈) | 2 × 16,7 ms | 2 × 16,7 ms (identisch) |
| Schwankend 40–60 FPS bei Ziel 60 (Frames zählen 1 oder 2 Vblanks, Übertrag gleicht aus) | gemischt | 2 × 8,3–16,7 ms (geglättetes f1 liegt zwischen 1 und 2 Vblanks) | 1 × 16,7 bzw. 2 × 16,7 ms, immer 60 Schritte/s | identisch |
| Einbruch auf 3 Vblanks (20 FPS) | 2 × 25 ms | 2 × 25 ms | **2 × 25 ms** | **2 × 25 ms** (identisch) |
| 4 Vblanks (15 FPS) | 4 × 16,7 ms | 4 × 16,7 ms | 4 × 16,7 ms (f1 = H, also nicht < H) | identisch |
| >60 FPS (90–240, unbegrenzt) | – | – | gibt es im Fork nicht (Frames zählen mindestens 1 Vblank, Spiel läuft zu schnell) | fraktionale Vblanks, H = 0: jeder Schritt 16,7 ms, 60 Schritte/s; bei 120 FPS etwa jeder 2. Frame ohne Schritt |
| `legacy` oder `original` bei >60 FPS | – | – | – | H bleibt ab Werk: Frames zwischen 1 und 1,5 Vblanks → 2 × 8,3–12,5 ms (der Fall aus dem Kommentar) |

Bei Zeitskala ≠ 1 (Zeitlupe, falls das Spiel sie nutzt) wird `f1` auch bei ganzen
Vblanks fraktional. Dann kann `steady` ebenfalls halbieren, etwa bei 2 Vblanks × 0,75
zu 2 × 12,5 ms. Das betrifft Fork und upstream gleich.

### 1.3 Frage 1: Obermenge?

Datei für Datei:

| Datei | Unterschied upstream gegen Fork | Bewertung |
|---|---|---|
| `simpsons/src/tick_count.cpp` | keiner (`git diff` leer), in `simpsons/CMakeLists.txt` beider Seiten enthalten | identisch |
| `simpsons/src/physics_step.cpp` | (a) Helfer `Host/LoadBE32/StoreImageBE32` → gemeinsame `guest_memory.h` und `image_patch.h`. (b) Prüf-Patch `{0x82159240, 4/59,94 → 4/59,94}`, der zusätzlich prüft, ob das Image passt. (c) `SetHalfStepLimit()`: im `steady`-Modus H = 0, solange `FractionalVblanks()` gilt, sonst der Werkswert (nur wenn H schon initialisiert ist). (d) Kommentar und Formatierung. | Bei ganzen Vblanks wird H auf genau den Wert gesetzt, den das Spiel selbst setzt: **Verhalten identisch**. Echte Obermenge. |
| `simpsons/src/frame_pacing.cpp` | Neu: `frame_rate` (0–1000, Standard 60), `frame_pacing_vblank_lock` (aus), `frame_pacing_vblank_offset_us`, `present_late_frames_immediately` (an). Hooks auf `sub_82453EB0` (Swap-Callback), `sub_826B7B70` (Frame-Limiter: schläft bis 2 ms vor dem Ziel statt zu spinnen, bei fraktional ein fester Raster-Limiter), `sub_82718788`, `sub_826B8068`, `sub_82867D60` (Menülogik bei >30 FPS im 30-FPS-Takt), `sub_827C0F78` (UI-Movie-Zeit ohne ms-Abschneiden). `menu_frame_rate` bleibt mit gleichem Standard (30). | Unsere Menü-Drosselung ist enthalten. Unterschied im Detail: Der Fork rundet `menu_frame_rate` auf ganze Vblank-Perioden (`lround(59,94/rate)`, also 45 → 60 FPS, 25 → 30 FPS). Upstream behandelt Raten, die keine ganzen Teiler von 60 sind, fraktional mit eigenem Limiter. Das ist genauer. Obermenge. |
| `simpsons/src/game_clock.cpp` (neu) | Hook auf die Spieluhr `sub_82690D60`. Bei ganzen Raten (30/60) wird nur das Original aufgerufen und optional geloggt (`[clock] whole vblanks …`). | Bei 30/60 FPS verhaltensneutral. |
| SDK `audio/sdl/sdl_audio_driver.cpp` | Unser erneutes Anfordern der Timer-Auflösung nach `SDL_HINT_TIMER_RESOLUTION` ist erhalten, nur nach `rex::thread::RequestHighResolutionTimer()` in `core/threading_win.cpp` verschoben, inklusive `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`. | Erhalten. Zu den XMA-/No-Device-Commits des Forks siehe Aufgabe 3. |
| SDK `core/clock.cpp` | Gastuhr ohne Lock und ohne Rundungsverlust (Basispunkt + exakte Skalierung, `clock_ratio.h`). Upstream nennt 0,5 % zu langsame Gastuhr (59,65 statt 59,94 Hz). | Betrifft vor allem Linux (Host in ns, Verhältnis 1/20, bis 19 ns Verlust je Lesen). Unter Windows ist QPC fast immer 10 MHz, Verhältnis 5/1 exakt, daher kein Verlust. Das erklärt, warum der Fork unter Windows 1,001× gemessen hat. Trotzdem übernehmen, schadet nicht und spart den Mutex. |
| SDK `core/timer_queue.cpp` | Neue Option `timer_queue_sleep` (Standard **aus**, also weiter Spinnen wie bei uns). | Neutral. |

**Ergebnis:** Bei Physik und Spieluhr fehlt in upstream nichts von uns, und nichts ist bei
30/60 FPS anders gelöst. Upstream ergänzt den Betrieb über 60 FPS, einen schlafenden
Limiter und `present_late_frames_immediately`.

### 1.4 Frage 2: Trifft „in halves“ auf `steady` zu?

- **Bei 60 FPS: nein.** Ein Vblank ergibt bitgenau `f1 = T` (`0x3C88AB86`), also genau
  ein Schritt. Das deckt sich mit eurer Messung (301/301).
- **Bei 30 FPS: formal ja, praktisch harmlos.** `f1 = 2T` liegt zwischen T und H und wird
  halbiert, aber die Hälfte ist genau T: 2 × 16,7 ms, 60 Schritte/s, wie auf der Konsole.
- **Bei schwankenden 40–60 FPS (Ziel 60): nein.** Die Uhr zählt nur ganze Vblanks
  (1 oder 2, mit Übertrag). Es gibt also nur 1 × 16,7 oder 2 × 16,7 ms. Die
  „8,3–12,5 ms“ setzen Frame-Zeiten von 1 bis 1,5 Vblanks voraus, und die gibt es nur
  mit upstreams fraktionaler Uhr (`frame_rate` ≠ 60/30/20/…) oder bei Zeitskala ≠ 1.
- **Ein Rest bleibt in beiden Ständen:** Fällt ein Frame auf 3 Vblanks (20 FPS), macht
  `steady` 2 × 25 ms statt 3 × 16,7 ms. Das ist selten und stammt so vom Original.
- **`legacy`:** dort entsteht wegen `0x3C88AB89 > T` das 2 × 8,3 ms bei 60 FPS, wie im
  Fork-Kommentar beschrieben. **`original`:** 30-Hz-Physik bei 60 FPS.

Der Kommentar in upstreams `physics_step.cpp` sagt das selbst korrekt im Konjunktiv
(„would be stepped in halves … While frames are fractional H is 0“). Nur die Kurzform im
Commit-Text („Frames between one and four vblanks long were stepped in halves“) liest
sich, als hätte `steady` bisher bei 60 FPS 8,3-ms-Schritte gemacht. Das stimmt nicht.

**Optionale Verbesserung für beide Stände:** Im `steady`-Modus H **immer** auf 0 setzen,
nicht nur fraktional. Dann gibt es bei 3 Vblanks 3 × 16,7 statt 2 × 25 ms, sonst ändert
sich nichts (2 Vblanks: weiter 2 × S, 1 Vblank: weiter 1 × T). Erst nach Messung
(1.7, Punkt E) entscheiden, ob es sich lohnt. Upstream setzt bei ganzen Raten
bewusst den Werkswert.

### 1.5 Frage 3: Tick-Count bei 30/60 FPS

- Bei 30/60 FPS ruft upstreams `game_clock.cpp` nur `__imp__sub_82690D60` auf. Die Uhr
  misst wie bisher über GetTickCount, also bleibt `tick_count_precise` **nötig**. Er ist
  in upstream **unverändert vorhanden und standardmäßig an** (Datei identisch, in
  `CMakeLists.txt`, im README unter „Game timing“ beschrieben).
- Im fraktionalen Modus misst `game_clock.cpp` selbst mit `QueryGuestTickCount()` und
  braucht den Tick nur für den ersten Frame nach einem Wechsel und für `clock + 12`.
  Auch das läuft über unseren Hook (siehe 1.6).
- Seit upstream die Timer-Auflösung zuverlässig hält, läuft der 1-ms-Zeitstempel-Timer
  vermutlich pünktlich genug. `tick_count_precise` wäre dann eher Absicherung als
  Notwendigkeit. Prüfbar mit Messung G.

### 1.6 Frage 4: `sub_8270CE10` gegen `sub_824324A8`

`simpsons_recomp.40.cpp:12007`:

```cpp
DEFINE_REX_FUNC(sub_8270CE10) {
  // b 0x824324a8
  sub_824324A8(ctx, base);
}
```

`sub_8270CE10` ist ein **Tail-Call-Thunk** auf `sub_824324A8` (`lwz r3, 16(KeTimeStampBundle)`).
Die Spieluhr `sub_82690D60` ruft den Thunk auf (`bl 0x8270ce10`), andere Stellen rufen
`sub_824324A8` direkt. Weil `DEFINE_REX_FUNC` den Namen `sub_X` nur als schwachen Alias
auf `__imp__sub_X` anlegt, landet auch der Thunk in unserem `REX_FUNC(sub_824324A8)`.
Beide Seiten sind also korrekt: upstream benennt den Aufrufer, der Fork den einzigen
Leser. Der Hook muss auf `sub_824324A8` bleiben, damit alle Aufrufer erfasst werden.

### 1.7 Frage 5: Empfehlung

| Bildrate | Korrekter oder robuster | Begründung |
|---|---|---|
| 30 | gleichwertig | Physik identisch. Upstream bietet zusätzlich `frame_rate = 30` im Spiel (der Fork hatte nur die Menü-Drosselung) und den schlafenden Limiter. |
| 60 | gleichwertig, upstream leicht besser | Physik identisch. Upstream: Limiter schläft statt zu spinnen (weniger CPU-Last, Leistung auf 4K/×3 eher GPU-gebunden), `present_late_frames_immediately` vermeidet einen ganzen Frame Versatz bei knapp verspäteten Frames. |
| >60 | nur upstream | Der Fork hat keinen Modus dafür. Upstream: fraktionale Uhr, feste 16,7-ms-Schritte, Menülogik im 30-FPS-Takt. Offenes Risiko (nicht im Code prüfbar): Ob das Spiel Havok-Körper zwischen den Schritten interpoliert. Wenn nicht, bewegen sich Kisten und Trümmer bei 90/120 FPS sichtbar mit 60 Hz. Ebenso können Spiellogik-Teile, die in Frames zählen, über 60 FPS schneller laufen (README: „scripted sequences“ ungetestet). |

**Übernehmen:** upstreams `physics_step.cpp`, `game_clock.cpp`, `frame_pacing.cpp`,
`guest_memory.h`, `image_patch.h`, die SDK-Teile `core/clock.cpp`,
`core/threading_win.cpp` und `audio/sdl/sdl_audio_driver.cpp`.
**Behalten:** `tick_count.cpp` (ist ohnehin identisch), Standard
`physics_step = "steady"`, `physics_log`. Die eigenen Dateien des Forks, die noch
eigene `Host`/`LoadBE32`-Helfer haben, auf `guest_memory.h` umstellen.
**Einstellung für den Nutzer:** `frame_rate = 60`, `menu_frame_rate = 30`, wie bisher.

### 1.8 Frage 6: Messplan (lokal, Windows 11)

Gemeinsam für alle Läufe: `physics_log = true` in `simpsons.toml`, Tree Hugger
(Sägewerk-Passage) etwa 60 s spielen, danach die `[physics]`- und `[clock]`-Zeilen aus
dem Log sichern. Nach Laden oder Menü die erste 5-s-Zeile verwerfen.

| # | Build / Einstellung | Erwartung laut Analyse |
|---|---|---|
| A | Fork `main`, 60 FPS (Referenz) | `steps per frame 1:` ≈ alle, `step 16.683-16.683 ms`, `game clock 1.000–1.001x`, `vblanks per frame 1:` ≈ alle |
| B | upstream bzw. Merge-Branch, `frame_rate = 60` | wie A, zusätzlich `[clock] whole vblanks … frame time ≈1.000x real time` |
| C | dto., `frame_rate = 30` | `vblanks per frame 2:` alle, `steps per frame 2:` alle, Schritt 16,683 ms, ≈60 Schritte/s (Summe Schritte ÷ Sekunden) |
| D | dto., `frame_rate = 120` (oder `0`, falls der Monitor weniger kann) | `[clock] fractional vblanks … frame time ≈1.000x`, `steps per frame 0:` und `1:` etwa je die Hälfte, Schritt immer 16,683 ms, ≈60 Schritte/s. Sichtprüfung: Ruckeln physikalischer Objekte (Kisten, Trümmer) gegen Kamera und Figur |
| E | B mit absichtlicher Last (z. B. `resolution_scale` hoch an einer schweren Stelle) | `vblanks per frame 3+` und `step … -25.0 ms` zeigen, wie oft der 2 × 25-ms-Fall vorkommt. Bei nennenswerter Häufigkeit lohnt die optionale Änderung „H immer 0“ |
| F | B mit `physics_step = "legacy"` (Kontrolle) | `steps per frame 2:` ≈ alle, `step 8.342 ms`, bestätigt die Analyse |
| G | B mit `tick_count_precise = false` | Zeigt `game clock` deutlich über 1,00× (Fork damals 1,075×), bleibt er nötig. Bei ≈1,00× reicht nach upstreams Timer-Fix bereits die Kernel-Kopie |
| H | Schaden in allen Läufen | Die Zeilen `hazards … TouchDetectorHurt n` pro Sekunde sollten in A–D gleich sein. Steigt der Wert bei D, hängt der Schaden an der Bildrate und nicht an den Physikschritten |

<!-- Abschnitte 2 und 3 folgen -->
