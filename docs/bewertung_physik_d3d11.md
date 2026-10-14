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

---

## 2. D3D11-Renderer

Vorweg: Der Fork-`main` enthält **kein D3D11**. `gpu` kennt dort nur `vulkan` und `d3d12`
(`tools/rexglue-sdk/src/graphics/flags.cpp:56-59`). Ein `gpu = "d3d11"` in der
Fork-Config ergibt stillschweigend Vulkan. Für einen Test braucht es upstreams Release
0.0.6.5 oder den Abgleich (Aufgabe 3) und einen Build mit `-DREXGLUE_USE_D3D11=ON`.
Der Standard ist OFF, der Release-Workflow schaltet es ein.

### 2.1 Reifegrad

- **Selbstauskunft** (`simpsons/re/d3d11_renderer.md:10-14`, `.github/release-notes.md`):
  experimentell. Bisher lief er nur unter Proton/DXVK auf dem Steam Deck, **nie auf
  echten Windows-Treibern**, und ist bei höherer Skalierung langsamer als Vulkan.
- **Implementiert:**
  - Draws, Copies, Swaps
  - Render-Target-Ownership und skalierte Resolves
  - Textur-Cache, einschließlich 565/5551/4444, Mip-Tails und Volumes
  - Gamma-Tabelle und PWL-Gamma
  - FXAA, CAS, FSR1
  - ImGui-Drawer
- **Live getestet** (nur Proton): Boot, Intro-Videos, Menüs, Spielstände, Springfield bei
  1x und 2x.
- **Offene Punkte laut upstream** (`d3d11_renderer.md:205-216`):
  - volles Spiel auf Windows-Treibern, In-Engine-Cutscenes
  - direkter Resolve ohne EDRAM-Umweg
  - breitere Texturprüfung
  - Memexport und tessellierte Draws
  - EDRAM-Snapshot für Trace-Playback
  - Resize, Vollbild, Device-Loss
- **Kein Fallback, das größte Risiko.** Jeder Fehler (Shader, Textur, Resolve) ruft
  `D3D11CommandProcessor::Fail()` auf (`graphics/d3d11/command_processor.cpp:25-31`),
  das loggt `Native DX11 rendering stopped: …`. Danach brechen alle Draws, Copies und
  Swaps ab, und das Bild bleibt stehen oder wird schwarz. Es gibt keinen Rückfall auf
  D3D12 oder Vulkan, auch nicht beim Start (`ui/rex_app.cpp:398` nimmt d3d11 davon aus).
- **Bekannte harte Grenzen im Code:**
  - dynamisch indizierte Ressourcen (`shader_bytecode.cpp:404`)
  - Resolve aus einem Target in Originalauflösung bei Skalierung > 1
    (`render_target_cache.cpp:441`, `return Fail(…)`)
  - Ownership-Transfer zwischen Skalierungen (`:226`)
  - nicht unterstützte Texturformate (`texture_cache.cpp:630`)
- **`resolution_scale = 3`:** Der Code lässt höchstens 3 zu
  (`render_target_cache.cpp:49-50`), getestet sind aber nur 1x und 2x. Jeder Resolve geht
  weiter über EDRAM-Encode, Copy und einen zusätzlichen Downsample. Der
  Native-Texture-Pfad (`66bb08c0`) gilt nur für 1x-MSAA-Targets in 8888 bzw. 2_10_10_10.
  Skalierte Resolve-Puffer werden 9-mal so groß (max. 512 MB je Region).
- **Character-Tessellation und Colour Grading (0.0.6.5) gibt es nur auf Vulkan.** Die
  Cvars liegen ausschließlich in `graphics/vulkan/command_processor.cpp`. Weder D3D11
  noch D3D12 haben sie.
- **Shader-Cache:** Gefunden wurde nur ein In-Memory-Cache, kein persistenter und kein
  vorkompiliertes D3D11-Set. Bei jedem Start ist mit Übersetzungsrucklern zu rechnen
  (aus dem Code geschlossen).
- **„Effect blackouts“-Fix (`35d23b0c`):** betrifft die Copy-free-Resolves unter Vulkan.
  D3D11 führt Copy und Clear synchron aus, der Fehler sollte dort strukturell nicht
  auftreten.

### 2.2 Versprechen gegenüber D3D12 und Vulkan

- **Zielgruppe** sind GPUs ohne gute Vulkan- oder D3D12-Unterstützung (README: „for older
  GPUs“). Feature Level 11.0 genügt.
- **Kein Leistungsversprechen gegenüber D3D12**, es gibt keine einzige Vergleichsmessung.
  Gegenüber Vulkan ist er ausdrücklich langsamer bei höherer Skalierung (auch als
  Launcher-Hinweis).
- **Einzige Zahlen** (Deck, Proton, Springfield, `d3d11_renderer.md:95-118`):
  - 1x: 17,8–18,1 ms
  - 2x: 44,2 ms (22,6 FPS), Vulkan auf dem Deck bei 2x etwa 59 FPS
  - Größte Posten bei 2x: Resolves (~11 ms) und Dekodierung (~8 ms)
- Zu CPU-Last auf dem Desktop, zu Windows-Treibern und zur Kompatibilität gibt es
  **keine Angaben**.

### 2.3 Verträglichkeit mit den Fork-Funktionen

| Funktion | Abhängig vom Renderer? | Bemerkung |
|---|---|---|
| `ink_outlines.cpp`, `eye_shading.cpp` | nein | Sie patchen Shader-Mikrocode und Daten im geladenen Gast-Image (`TranslateVirtual`, `heap->Protect`). Jeder Renderer übersetzt danach. Upstream führt beide (umgebaut auf `image_patch.h`). **Restrisiko:** Ob die gepatchten Varianten durch den D3D11-Übersetzer gehen, ist ungeprüft. Ein Fehlschlag hieße wegen des fehlenden Fallbacks Render-Stopp. |
| `draw_telemetry.cpp` | nein | Hookt die PM4-Draw-Emitter des Gastes, nicht den Command-Processor. |
| `freecam.cpp`, Teleport, `hint_text.cpp` | nein | Nur Fenster, Eingabe und Gast-Hooks. |
| `ring_wait.cpp` | nein | Nutzt den Basis-`CommandProcessor`, D3D11 erbt davon. |
| Screenshots (`--trace_gpu_prefix`, `.shot`) | generisch | `GraphicsSystem` ruft `presenter->CaptureGuestOutput`, und `D3D11Presenter` implementiert das (`d3d11_presenter.cpp:229-262`). Sollte gehen. Frame-Trace-**Wiedergabe** geht mit D3D11 nicht (Snapshot → `Fail`). |
| ImGui-Overlay (Charaktermenü, Hinweise) | generisch | `ImGuiDrawer` entsteht über `provider->CreateImmediateDrawer()`, D3D11 liefert `D3D11ImmediateDrawer`, UI-Blending ist laut Notizen getestet. Sollte gehen. |
| SDK-Änderungen des Forks | nein | Betroffen sind nur Audio, XMA, Input, Threading und `windowed_app_main_win.cpp`. Keine D3D12- oder Vulkan-Teile, also braucht es kein D3D11-Gegenstück. |

### 2.4 Lohnt sich ein lokaler Test?

**Nur als Experiment, nicht als Ersatz für D3D12.** Für 4K mit `resolution_scale = 3`
auf einer aktuellen GPU verspricht D3D11 nichts: Er ist für ältere GPUs gedacht,
langsamer bei hoher Skalierung, 3x und Windows-Treiber sind ungetestet, und es gibt
keinen Fallback. Ein Lauf hätte vor allem Wert als **erster Erfahrungsbericht auf echten
Windows-Treibern**. Wer Leistung oder die neuen Grafikfunktionen (Tessellation,
Colour Grading) will, sollte eher **Vulkan** testen: upstream empfiehlt es, es spielt die
Intro-Videos und hat beide Funktionen. D3D12 zeigt laut upstream schwarze Intro-Videos.

**Umschalten:** im Launcher unter Settings → Graphics backend → „Direct3D 11
(experimental)“ oder mit `gpu = "d3d11"` in `simpsons.toml`. Zurück mit
`gpu = "d3d12"`.

**Worauf achten:**

1. Im Log nach `Native DX11 rendering stopped:` suchen, das ist die Ursache bei
   Standbild oder Schwarzbild.
2. „Shadow resolution“ auf „Match render resolution“ lassen. Mit „Original“
   (`native_rt_original_resolution_targets = "832:13"`) führt der Shadow-Map-Resolve bei
   Skalierung > 1 sehr wahrscheinlich in den harten Fehler aus
   `render_target_cache.cpp:441` (aus dem Code abgeleitet).
3. Leistung mit `gpu_wait_stats = 1` gegen D3D12 vergleichen (Springfield, Effekt-Combos
   und Spezialattacken).
4. Shader-Ruckler beim ersten Auftreten nach jedem Start, besonders bei den gepatchten
   Augen- und Outline-Shadern.
5. Resize, Vollbildwechsel, Alt-Tab, In-Engine-Cutscenes.
6. Charaktermenü (Y), Screenshots, Freecam, Ink- und Augen-Optionen: sehen sie aus wie
   unter D3D12?
7. Bei Fehlern `d3d11_debug = true` setzen (braucht die Windows „Graphics Tools“).

<!-- Abschnitt 3 folgt -->
