> 2026-11-02 kullanıcı değişikliği: SQLite kaldırıldı; TimescaleDB/PostgreSQL zorunlu. Lokal dağıtım tek Compose projesinde üç konteynerdir. Eski iki-konteyner topolojisi geçersizdir. ≤100 MiB hedefi simülatör+broker için korunur; TimescaleDB process RSS ve üç servis toplamı ayrıca ölçülür, bu toplam için yeni bir limit varsayılmaz. Canlıya geçiş ve eski SQLite checkpoint aktarımı bu lokal değişiklikle yapılmaz.

# CNC Digital Twin / Virtual Machine & PLC — Uygulama Teknik Şartnamesi

**Sürüm:** 1.0 — 2026-11-01
**Statü:** Uygulamaya esas tasarım; henüz uygulanmış/test edilmiş ürün değildir.
**Hedef:** Tek CNC için, Linux sunucusunda 7/24 açık, gerçekçi sentetik üretim verisi üreten, S7 / OPC UA / MQTT erişimi ve son 24 saatin TimescaleDB geçmişini sunan, düşük bellekli Docker dağıtımı.
**İşletim modeli:** Tek teslimat; özellikleri daha sonraya erteleyen faz planı yok. İnşa ve doğrulama adımları birer kontrol noktasıdır.

## 0. MİMARİ KARARLAR — KESİN

| Konu | Karar | Neden / sınır |
|---|---|---|
| Simülatör | C++17, tek ana proses, Linux x86_64/arm64 | Düşük çalışma zamanı yükü; AI'nın C++ derlemesini ve testlerini yapması şart |
| S7 | Snap7 **Server** (klasik S7comm, ISO-on-TCP), simülatörün sağladığı standart DB'ler | Gerçek S7-1200 firmware / S7CommPlus / TIA programlama **değil** |
| OPC UA | open62541 (v1.5.x sabitlenmiş ve test edilmiş sürüm) | IEC 62541 veri okuma / subscription; gerçek OPC UA server |
| MQTT | libmosquitto istemcisi + ayrı hafif Eclipse Mosquitto broker konteyneri | MQTT yayıncı broker değildir; dış broker bağımlılığı olmadan teslim |
| Kalıcılık | TimescaleDB / PostgreSQL, 24 saatlik hareketli pencere + kalıcı runtime state | C++ libpq; ayrı Docker servisi |
| Yönetim | Küçük C++ HTTP sunucusu; yalnızca okunur dashboard ve kontrollü admin API | React, Node.js, 3D runtime, CDN yok |
| Model | Tek kaynak `MachineSnapshot`, olay odaklı makine durum makinesi | Tüm protokoller aynı fiziksel olayı yansıtacak |
| Simülasyon saati | `steady_clock` ile gerçek zaman; UTC kalıcı timestamp | NTP / saat ayar değişiminden iş çevrimi bozulmasın |
| Dağıtım | Docker Compose: `cnc-simulator`, `mqtt-broker`; kalıcı `data/` hacmi | Tek komutla kurulum, restart policy, healthcheck |
| RAM | **Kabul hedefi:** simülatör + broker çalışma anı RSS toplamı ≤100 MiB, 3'er istemci yükü altında 24 saat | 50 MiB ideal; kesin tüketim **ölçülmeden garantilenemez** |
| Üretim rolü | Test/demo/dataset, sentetik veri olduğu README ve yönetim metaverisinde açık | Gerçek PLC/güvenlik kontrolü yerine kullanılmaz |

**Hedef dışı:** eksiksiz G-code yorumlayıcısı, gerçek kesme fiziği/FEA, IEC 61131-3 çalıştırma, gerçek S7-1200 firmware kimliği, Siemens sertifikasyonu, 3D model/Unity, çoklu makine, bulut bağımlılığı, uzaktan gerçek tezgâh kontrolü. Bunları 1.0'a sessizce ekleme.

**ÖNEMLİ KABUL AYRIMI:** "Gerçekçi" = süreç tutarlılığı, makul sınırlar, zamansal/istatistiksel nitelik ve gerçek konnektörlerle bağlantı. Bütün uzmanların/sınıflandırıcıların sentetik kökeni hiçbir koşulda anlayamaması ispatlanamaz. Müşteriye sunum yapılırken sentetik demo olduğu dürüstçe belirtilmelidir; normal CNC telemetri alanlarına her pakette gereksiz `fake` öneki konmaz, ancak `/api/v1/about` ve README kökeni belirtir.

## 1. ÇALIŞMA AKIŞI VE BİLEŞENLER

```text
                       program_catalog.yaml   plant.yaml
                                |                 |
                                +--------+--------+
                                         |
                              [Scheduler / Shift Planner]
                                         |
[PRNG + Scenario Director] ---> [Machine Engine / finite state machine]
                                         |
                         [Physics + Tool Wear + Quality]
                                         |
                             IMMUTABLE MachineSnapshot
                                |        |       |       |
                               S7     OPC UA    MQTT    TimescaleDB
                            DB1..4  ns=2       broker  24h WAL
                                |        |       |       |
                         existing S7 / OPC UA / MQTT connectors
                                         |
                                    HTTP REST
                                         |
                            static lightweight dashboard
```

**Tek yazıcı kuralı:** `Engine` dışında hiçbir yayıncı model durumunu değiştiremez. Engine her 200 ms'de bir yeni tam snapshot üretir, snapshot yalnızca referans veya kopya olarak yayıncılara aktarılır. OPC UA / MQTT / DB / S7 farklı anlarda yayın yapabilse de her yayında `sequence` ve UTC zaman bulunur. Aynı sequence için alanlar aynı snapshot'la eşleşmelidir; farklı protokolleri karşılaştırırken sequence'a göre hizala.

**Kompakt thread modeli:** bir engine/event-loop, bir OPC UA pump (gerekirse aynı loop), Snap7'in kendi kısıtlı worker'ları, MQTT arka plan I/O, HTTP küçük I/O ve TimescaleDB tek writer. Sınırsız queue/thread yok. Bounded FIFO: telemetry yerine en güncel snapshot sakla; event queue ≤256, sırayla yaz, kritik olay kaybında görünür hata ve health bozulması.

## 2. VARSAYIMLAR VE PARAMETRELER

- **V1 makine:** Generic 3-axis vertical machining center, `CNC-01`, X=800mm, Y=500mm, Z=500mm, 12 takımlı magazin; X/Y/Z eksenleri ve 4. eksen yok.
- **V1 PLC davranışı:** S7 klasik DB erişimi için görünür sanal PLC; rack=0, slot=1 test edilir, istemcide slot=0 veya farklı TSAP gerektiğinde bağlantı parametreleri testle belirlenecek. Model kimlik bilgileri Siemens donanımı olduğunu iddia etmeyecek.
- **Saat dilimi:** fabrikadaki vardiya ve iş planı için `Europe/Istanbul`; tarih/geçmiş, TimescaleDB ve MQTT timestamp için UTC (ISO 8601 `Z`).
- **Varsayılan süreç:** 3 vardiya 06:00–14:00, 14:00–22:00, 22:00–06:00; bütün vardiyalarda iş emri bulunabilir; vardiyada 2 kısa mola ve yemek planı config'de. Hafta sonu normalden düşük ama sıfır olmayan iş yoğunluğu.
- **Engine:** 200 ms, ortak snapshot 1 Hz, MQTT telemetri 1 Hz, S7 alanları 1 Hz veya değişimde, OPC UA values 1 Hz, SQL snapshot 5 sn, olaylar anında; health 10 sn, temizleme 60 sn.
- **Makine çalışma oranı:** tek gün için %60–85 üretim/çevrim ağırlıklı *konfigüre edilebilir hedef aralığı*, her gün sabit oran zorlanmayacak; başlangıç varsayımı, saha verisi ölçümü değil.
- **Disk:** ayrı kalıcı volume; SQL 5 saniye örnekleme ile 17.280 snapshot/gün + olaylar (üst sınır disk testte raporlanacak). 1 Hz verinin **tam 24 saat geçmişi** gerekirse config `history_sample_seconds=1` ile mümkün; varsayılan 5 sn yalnızca tarihsel ayrıntı sınırıdır.
- **Portlar:** S7 host TCP/102 → container 1102, OPC UA TCP/4840, MQTT TCP/1883, UI/HTTP TCP/8080. Host port 102 izin veya çakışma sorunu varsa alternatif host port config; istemci portunu belge.
- **Çalışma başlangıcı:** volume yoksa 12 yeni takım, sıra numaraları ve iş emri oluştur. Volume varsa sayaç/takım ömrü/aktif iş emri ve PRNG state'e geri dön; yarım kalmış fiziksel çevrimi tamamlanmış sayma. Yeniden başlatmayı ayrı event olarak kaydet.

## 3. VERİ SÖZLEŞMESİ (EN KRİTİK DOSYA)

**Tek doğruluk kaynağı:** `contracts/tags.csv`. Kod üretimi veya statik sabitler bu sözleşmeyi takip eder. Kanonik `key`, `type`, `unit`, `s7_db`, `s7_offset`, `opc_node_id` (`ns=2;s=Machine.CNC-01.<key>`), MQTT `data[key]`, okunabilir aralık ve update zamanı burada verilir. 4 DB bellek boyları: `DB1=256`, `DB2=256`, `DB3=256`, `DB4=128` byte. DB'leri servis başlamadan kayıt et; hot-reload offsetleri değiştirmesin.

**S7 kodlama:** S7 uyumlu big endian `UINT16`, `UINT32`, IEEE754 `REAL32`. `U8` durum/boolean *bir bayt 0/1*, Siemens `BOOL` bit adresi olduğu iddia edilmez (ör. `DB1.DBB112`); istemci `DBX112.0` üzerinden okumak isterse sonuç bit0'dır ama bu alanla tasarım bit packing'e dayanmaz. Metinler klasik S7 `STRING[N]`: ilk bayt maksimum uzunluk `N`, ikinci bayt gerçek uzunluk, ardından ASCII ve `N` data byte, toplam N+2. Offsetlerde çakışma ve sınır taşması testle doğrulanır. String içinde yalnızca ASCII ID/etiket kullan; lokalizasyon HTTP/OPC açıklamalarında.

**S7 okuma örnekleri:** `DB1.DBD16` = `spindle_target_rpm` REAL; `DB1.DBD20` = `spindle_actual_rpm` REAL; `DB2.DBD4` = `active_wear_pct` REAL; `DB3.DBD16` = `job_completed_qty` DWORD; `DB4.DBW0` = `alarm_code` WORD. `DB1.DBB112` = `is_cutting` 0/1. DB alanları dış istemci için salt okunur tasarlanır: **tercih edilen resourceless callback write request'i protokol seviyesinde reddeder**; fallback `RegisterArea` modu yazma koruması sağlamıyorsa yalnız izole geliştirme ağında kalır ve açık risk olarak raporlanır. Gelen write hiçbir koşulda engine state'ini değiştiremez. YAZMA TESTİ kesinlikle yapılmalı.

**Tool magazine DB2 alanı:** Başlık ilk 64 byte, slotlar 12×16 byte (ofset 64+16×i). Her slot içeriği `tool_id:uint16 @+0`, `tool_type:uint16 @+2`, `wear_pct:REAL32 @+4`, `cutting_minutes:REAL32 @+8`, `life_remaining_minutes:REAL32 @+12`. Sıra `i=0..11` sabit, asla değişmeden kalır; gerçek takım değişiminde ID/ömrü yenilenir. OPC UA `Tools/Tool01..Tool12` nesneleri ve MQTT `tools` listesi bu alana karşılık gelir. Tüm alanlar machine snapshot'tan türetilir.

**Semantik:** `rpm` gerçek spindle hızı, `spindle_load_pct` nominal motora göre yük, `feed_actual_mm_min` komut değil gerçek ilerleme, `wear_pct` 0 yeni / 100 limit, `energy_kwh` ömür boyu artan kümülatif, iş emri sayaçları her yeni emirde sıfırlanır ancak `total_*` sayaçları kalır. Hem sistem hem makine durumlarında 0/boş duruma makul değer atanır; mekanik sürtünme nedeniyle güç idle'da sıfır değildir.

## 4. DURUM MAKİNESİ VE PROGRAM DAVRANIŞI

| Kod | `state_code` | Anlam | Motor/ilerleme | Çıkış koşulu |
|---:|---|---|---|---|
| 0 | OFF | planlı güç kapalı | rpm→0 / 0 feed | vardiya başlangıcı |
| 10 | IDLE | iş bekliyor / yükleme | rpm→0 / 0 feed | hazır iş, operatör |
| 20 | SETUP | fikstür/program/ilk parça hazırlığı | genelde rpm 0 | setup süresi tamam |
| 30 | READY | üretime hazır | spindle kapalı | çevrim başlat |
| 40 | RUNNING | AUTO program çalışıyor | NC adımına bağlı | step / cycle bitti |
| 50 | FEED_HOLD | operatör duruşu, devam edilebilir | feed→0, spindle kontrollü | devam/iptal |
| 60 | TOOL_CHANGE | otomatik takım değiştirme | feed=0, spindle durur | magazin işlemi biter |
| 70 | ALARM | interlock/arıza | feed=0, spindle güvenli düşer | onarım ve reset |
| 80 | MAINTENANCE | bakım | spindle off, door gerekirse açık | bakım bitti |
| 90 | BREAK | molada bekleme | feed=0 | mola bitişi |

`controller_mode`: 0=OFF, 1=AUTO, 2=MANUAL, 3=MDI, 4=SETUP. `step_type`: 0=NONE, 1=LOAD, 2=RAPID, 3=CUT, 4=DRILL, 5=DWELL, 6=TOOL_SWAP, 7=UNLOAD, 8=INSPECT. `reason_code` duruş sınıfı: 0=NONE, 1=NO_JOB, 2=PLANNED_BREAK, 3=SETUP, 4=OPERATOR_PAUSE, 5=TOOL_WARN, 6=FAULT, 7=MAINTENANCE, 8=SHIFT_CHANGE, 9=NO_MATERIAL, 10=QUALITY_HOLD, 11=RESTART_RECOVERY.

**Geçiş kuralları:** `OFF→IDLE→SETUP→READY→RUNNING`, program içinde `RUNNING↔TOOL_CHANGE`, operatör müdahalesinde `RUNNING→FEED_HOLD→RUNNING`, alarmda her operasyonel durumdan `ALARM→MAINTENANCE/READY`, iş bitince `RUNNING→IDLE/SETUP`. Geçersiz state geçişlerini assertion/event ile yakala. `e_stop_active` yalnızca acil stop arızasında aktif. `is_cutting=1` ancak `RUNNING && (CUT||DRILL) && spindle_actual_rpm>threshold && feed_actual_mm_min>0 && part_clamped`; kapı açıkken is_cutting olamaz. `TOOL_CHANGE` durumunda kesme yoktur. `RUNNING` içinde RAPID/DWELL/LOAD olduğunda kesme yoktur.

**NC programları:** Gerçek G-code yorumlayıcısı yazma. `config/programs.yaml` revision 2 dört sentetik operasyon programını tanımlar: `ALU_BRACKET_P1001` (914.14 sn, T03 kaba/finisaj + T05 delik dizisi), `STEEL_FLANGE_P1002` (970.88 sn, T02 kaba/finisaj + T05), `ALU_VALVE_P1003` (939.70 sn, T03 kaba/finisaj + T06), `BRASS_FITTING_P1004` (719.54 sn, T07 kaba/finisaj + T08). Çevrim başına en fazla iki planlı takım değişimi; istenen takım zaten yüklüyse M06 atlanır. Kaba/finisaj aynı takımla ardışık yapılır. Kesme yolları ve çok delikli operasyonların süreleri geometri/ilerlemeden türetilir; delik konumlama ve geri çekilme kesme/aşınma üretmez. Arıza, mola, aşınma ve hazırlık gerçek süreyi uzatabilir. G-code önizlemesi doğrulanan segmentlerden türetilir; gerçek makine için çalıştırılabilir program değildir. Canlı blok, operasyon ve takım kesme süresi tek `MachineSnapshot.nc` kaynağından HTTP, ayrı MQTT `program` v1 konusu ve SQL `nc_json` alanına aktarılır; mevcut 158 etiket/S7/OPC/telemetry v1 sözleşmesi değişmez.

**İş emri planlayıcısı:** aktif emrin `targetQty` aralığı 40–250, hedef tamamlanmadan rastgele her dakika başka işe geçme. Emir tamamlanınca veya üretim planındaki öncelik değişince 2–12 dakikalık gerçekçi setup oluşsun. Önceki parça üretim süresi ve reddi sayaçları her iş emri için korunur. Gece daha az malzeme/operatör bekleme olabilir. Vardiya değiştirme, mola ve operatör müdahalesi bir sonraki mantıklı sınırda; kritik alarm anında kesme durdurulur. Saatlerin tamamında makine çalışması değil, **veri yayını kesintisiz** olması amaçtır.

**Parça sayaçları:** yalnızca `INSPECT` tamamlanınca **bir** parça say. `job_completed_qty = job_good_qty + job_reject_qty`; `total_parts_count = total_good_count + total_reject_count` her snapshot için. `job_target_qty` negatif olamaz; iş emri 100% olunca sonraki emir. Yarım çevrimde parça sayma; restart, takım kırılması, feed-hold sırasında sayaç artmaz.

## 5. FİZİKSEL OLASILIK MODELİ — "RANDOM()" DEĞİL

**Her sensör:** nominal proses + fiziksel durum etkisi + zaman ilişkili (AR(1) veya low-pass) ölçüm gürültüsü + nadiren kalibre edilmiş sensör olayı. Aynı dağılımdan her saniye bağımsız çekilen `rand()` üretme. Config'de farklı tohumla yeniden üretilebilir; production seed UUID/entropy'den bir defa oluşturulur ve state'e kaydedilir.

| Sinyal | Minimum tutarlı model | Zorunlu etkileşim |
|---|---|---|
| Spindle Actual RPM | Hedefe 1. dereceden ivmelenme/yavaşlama, max 12.000 RPM | Hedef 0 olduğunda geçici inertia ile düşer |
| Feed Actual | Segment feed × override × hızlanma profili | RAPID farklı eksen hızı; FEED_HOLD/ALARM'da 0 |
| XYZ konumu | Segment waypoints arasında kinematik sınırlı interpolasyon | X [0,800], Y [0,500], Z [0,500]; duruşta eksenler donuk |
| Spindle load | Malzeme × derinlik/kalınlık × feed × rpm faktörü × tool wear | DRILL tepe yükleri, RAPID çok düşük kesme yükü; 0–110% geçici tepe |
| Vibration RMS | Başlangıç 0.3–1.5 mm/s, kesmede yük ve wear etkisi | Aşınma ile kademeli artış, tool break anında kuvvetli pik |
| Spindle Temperature | Dinamik ısıl denge, zaman sabiti 6–20 dk | Uzun kesimde ısınır; idle'da soğur; 20–90°C |
| Power kW | Taban güç + spindle/load + eksen hareketi + coolant | 0 W'lık idle yok; 0–25 kW arasında tasarım |
| Energy kWh | Pozitif güç × gerçek delta time / 3600 | Ömür boyu tek yönlü artış; 0'a dönmez |
| Coolant flow/pressure | Pompa açık + geçiş + devre durumu | Pompa OFF ise flow basıncı düşer; blokaj akışı düşürür |
| Air/hydraulic | Besleme basıncı ve düşük frekans oynama | Basınç kaybı alarm/interlock getirir |
| Axis velocity | Konum türevi, mm/s | Hareket etmeyen eksen yüksek hız raporlayamaz |
| Ambient/cabinet temperature | Yavaş günlük seyir + kabin ısısı | Kabin ısısı çevreden birkaç derece sıcak |

**Takım aşınması zorunlu modeli:** Her fiziksel takım için `tool_id`, `type`, `nominal_life_min`, `cutting_minutes`, `wear_pct`, `diameter_wear_mm`, `length_offset_mm`, `break_probability`, `replacement_count`. Aşınma **yalnızca gerçek kesme/DRILL sırasında** güncellenir, takım takılı ama idle iken ilerlemez. Basit formül:

```text
stress = clamp(material_factor * (0.6 + 0.4*load_pct/75)
               * (0.7 + 0.3*feed_actual/feed_nominal)
               * (1 + thermal_penalty), 0.2, 3.0)
wear_pct += 100 * cutting_dt_seconds / (60 * nominal_life_minutes) * stress
life_remaining_minutes = max(0, nominal_life_minutes * (1 - wear_pct/100) / max(0.2, stress_ema))
diameter_wear_mm = tool_specific_max_diam_wear_mm * (wear_pct/100)^1.4
```

Formül kaba deneysel simülasyon kuralıdır, gerçek takım ömrü tahmin modeli değildir. `nominal_life_min` örneğin 120–900 min, malzemeye göre değişir; ilk kurulum tool wear başlangıçlarını 0–55% heterojen ata, makine taze takımlı görünmesin. %80 `ToolLifeWarning`, %95 operatörün bekleyen takım değişimi kararı, %100 kritik limit: kontrollü durdur ve değiştirme zorunluluğu. `tool_break` olasılığı kesme sırasında aşınma×yük×titreşim koşullarına bağlı, nadir ve olay bazlı. Her replacement'ta tool ID değişmeyebilir, fakat o magazin slotundaki kullanım ömrü sıfırlanır; `replacement_count` artar, event kaydedilir. `is_cutting=0` iken aktif wear yüzdesi sabit kalmalı. Standart referans: OPC UA Machine Tools ToolLife ölçümleri.

**Kalite ve üretim:** Parça bitişinde `p_reject` baz oran yaklaşık %0.5–2.0 (varsayım); yüksek tool wear, yüksek titreşim, aşırı spindle load, coolant düşüklüğü ve önceki kalite uyarısı ile yükselir; baz reject oranı iş emri/malzeme ile farklıdır. Kalite reddi olmuşsa `reject_reason` kodu türet. Reject sayıları alarm sayısıyla birebir aynı değildir. Çevrim süresi örneklerini sabit ±1% simetrik yapma: setup etkisi, toolchange ve küçük sapmalarla asimetrik dağılım oluşmalı.

## 6. KAPSAMDAKİ ANOMALİLER / ARIZALAR

Her arıza için YAML'da `id`, `precondition`, saatlik nominal hazard, `severity`, `effect`, `duration_distribution`, `repair_path`, `recovery` alanları olsun. `hazard` değerlerini **gerçek saha verisi değil sentetik başlangıç** olarak belge. Monte Carlo seçimi olay anında yap; birbirinden bağımsız her saniye %10 alarm gibi yapay davranış üretme. İki kritik arızanın birbiriyle çakışması yasak; arıza sırasında yeni iş emri başlamaz.

| Kod | Olay | Tetik | Veri/süreç etkisi | Kurtarma |
|---:|---|---|---|---|
| 1001 | Tool life warning | active wear ≥80% | warning, artan load/vibration | bir sonraki uygun sınırda değişim |
| 1002 | Tool wear limit | ≥100% | duruş, spindle coast-down | tool replacement |
| 1003 | Tool break | yüksek wear×yük, nadir | ani vibration pik, alarm, olası scrap | tool change, inspection |
| 1101 | Spindle overload | anormal kesme yükü | load spike, feed hold/alarm | yük azalması veya reset |
| 1102 | Spindle overtemp | ısı birikimi | sıcaklık artış trendi, alarm | cool-down, bakım |
| 1201 | Coolant low level | hazne seviyesi düşer | flow/pressure azalır, sonra alarm | refilling |
| 1202 | Coolant blockage | aralıklı tıkanma | pressure ↑, flow ↓ | bakım |
| 1301 | Air pressure low | basınç kaybı | air pressure düşer, interlock | supply restore |
| 1302 | Hydraulic pressure low | pompa/kaçak | pressure düşer, clamp unavailable | bakım |
| 1401 | Door interlock | kapı açma denemesi | cycle pause, door state güvenli | kapı kapat/reset |
| 1402 | Emergency stop | çok nadir operatör olayı | e-stop=1, hızlı duruş | manuel reset |
| 1501 | Axis following error | yük/servis etkisi | axis hız/konum sapması | axis re-home |
| 1502 | Lubrication warning | çalışma saati/kullanım | bakım bildirimi | refill |
| 1601 | No material | lojistik/gece | NO_MATERIAL idle, üretim durur | malzeme gelir |
| 1602 | Operator pause | görev ve mola planı | feed_hold, süreli | operatör devam |
| 1701 | Power/network outage (test only) | arıza enjeksiyonu | servis durabilir, heartbeat stale | restart; yarım parça sayılmaz |
| 1702 | Sensor drift/stale | cihaz modeli, nadir | ölçüm kalite kodu / stale | yeniden kalibrasyon |
| 1801 | Quality inspection hold | son reject serisi | setup/inspection beklemesi | kalite onayı |

**Senaryo profilleri:** `normal`, `aging_tools`, `high_utilization`, `unstable_coolant`, `frequent_microstops`, `quality_issue`, `night_shift`, `maintenance_week`. Profiller nominal istatistikleri değiştirsin, sensor random sayılarına ayrı ayrı kaba çarpan uygulamakla yetinmesin. `normal` default; özel admin test çağrılarıyla senaryo enjeksiyonu opt-in.

**Şiddet ve onarım:** Warning/major/critical; MTTR dağılımı arıza bazlı, küçük duruş 20–180sn, bakım 5–45dk gibi **sentetik** aralıklar. Deterministik test modu sahte saat ile saniyeler içinde 24h yaşatabilir ama production clock yalnızca 1x.

## 7. ÜÇ PROTOKOL — KESİN DAVRANIŞ

### 7.1 S7 — sanal PLC sunucusu

- Snap7 `TS7Server` / C API; **Tercih edilen read-only uygulama:** Snap7 resourceless `SetRWAreaCallback` ile DB1..4 isteklerine immutable snapshot byte buffer üzerinden cevap ver; `OperationWrite` için nonzero/protokol hata cevabı döndür, gerçekten reddedildiğini test et. Bu mod kullanılan Snap7 sürümüyle ve mevcut konnektörle uyumlu değilse `RegisterArea(srvAreaDB,1..4)` ile 256/256/256/128 byte bellek blokları **server başlatılmadan önce** kaydet; ancak bu ikinci modda varsayılan S7 DB yazılabilirliği güvenlik riski oluşturur ve yazma koruması test edilmeden production-safe/read-only iddiası yapılamaz.
- Container içi port 1102: `Srv_SetParam(... p_u16_LocalPort ...)`, Docker host `102:1102` **öncelikli tasarım**. Bunun gerçek Snap7 build ve istemciyle çalıştığı kanıtlanmazsa container içi TCP/102 ve Linux `CAP_NET_BIND_SERVICE` ile host `102:102` fallback yap; eski Snap7 kılavuzuyla bazı sürümlerin port davranışı çelişebilir. Dockerfile/compose/test tek ayara uygun olmalı. Dış istemci S7/IP ve GET/DB_READ gibi klasik protokolden bağlanır; PDU ve bağlantı slotları test edilir.
- Resourceless mode: kısa çalışan callback snapshot byte alanından sadece belirtilen offset/uzunluk kadar okur ve doğrular, herhangi bir bloke edici engine/DB işi yapmaz. RegisterArea fallback: DB güncellemesi `Srv_LockArea` / `Srv_UnlockArea` ile tam DB kopyası altında yapılır. Her iki modda gerçek istemciyle eşzamanlılık ve yazma denetimleri zorunludur. DB1/2/3/4 ayrı güncelleme sırasında okunsa dahi `sequence` ile tekrar okunarak tutarlılık sağlanır. `s7_write_rejection` davranışını entegrasyon testiyle doğrula, salt okunur dış network güvenliğini yalnızca uygulama mantığına bırakma.
- Snap7 bazı sistem komutlarını kısmen benzetir, firmware, protection, TIA program upload, S7CommPlus ve optimized symbolic DB verilmez. Kullanıcı bağlantı kılavuzu bunu şeffafça söyler.
- `rack=0/slot=1` ilk varsayım; `rack=0/slot=0` ile de entegrasyon smoke yap, konnektöre göre belgelenmiş bağlantı ayarı bırak. S7 max clients 8.

### 7.2 OPC UA — IEC 62541

- open62541 Server; listen `opc.tcp://HOST:4840`; `ApplicationUri` ve server identity sabit, sertifika host adına/SAN'a uyumlu; NodeId'ler yeniden başlatmada **değişmez**.
- Namespace URI `urn:digitheta:cnc-simulator:machine:v1` (isim bir ürün markası sahipliği iddiası değil, değiştirilebilir namespace); NodeId `ns=2;s=Machine.CNC-01.<key>`; `ns=2` indeksini **namespace register dönüşünden** al; startup'ta indeks 2 olması garanti değilse config/tests gerçek indexi kontrol eder ve üretilen dokümanla sabit tutar.
- `Objects/CNC-01/Live`, `/Production`, `/Tools`, `/Alarms`, `/Metadata` düzeni; datatype'lar CSV'ye göre native UA type; read + subscriptions, 1000ms sampling başlangıç, 10 eş zamanlı subscription üst sınırı.
- OPC UA `SourceTimestamp` snapshot UTC, `ServerTimestamp` yayın zamanı. Kalite: normal `Good`, sensör stale/hatalı durumda uygun `Uncertain` / `Bad` status code; sentetik köken `Metadata/DataOrigin="Synthetic"`.
- **Güvenlik:** Gerçek internet erişimi yasak. VPN / endüstriyel test ağı içinde Basic256Sha256 + SignAndEncrypt modu için sertifika yapılandırma yolu oluştur; `None` yalnız `ALLOW_INSECURE_OPCUA=true` ve private LAN test profilinde. Kullanıcı bağlantı ve self-signed trust adımları README'de. Bazı client'ların open62541 kurulumuna bağlı policy seçenekleri derleme/testle doğrulanır.
- OPC UA Machine Tools (OPC 40501-1) `ToolLife` ve job kavramları semantik referans. Resmi companion namespace/Nodeset'i eksiksiz uygulamıyorsan **OPC 40501 compliant** yazma; sadece `inspired by`.

### 7.3 MQTT — broker, topic, QoS

- Broker `eclipse-mosquitto` image: güvenli, sabit major/minor ve digest belgele; test profilinde 1883 private. Publisher user parola `.env` / Docker secret; subscriber için yalnız `read` ACL; broker anonymous erişim **kapalı**. Broker internete açık veya production subnet'e bridge edilmez.
- `plant/demo/cnc/CNC-01/telemetry` her 1 sn, JSON, QoS 0, retained=false; `.../state` değişimde ve her 30s heartbeat, QoS 1 retained=true; `.../events` anında, QoS 1 retained=false; `.../tools` değişimde ve 60s, QoS 1 retained=true; `.../availability` online/offline LWT ve re-connect publish, QoS1 retained=true.
- Bütün mesajlarda `schema_version=1`, `machine_id`, `ts_utc`, `sequence`, `data`; `telemetry` data key'leri CSV ile **birebir** aynı. `tool list` 12 kayıt. Event `event_id`, `code`, `severity`, `start/end`, `reason`, `duration_s`, `order_id`, `tool_id` içerir.
- En fazla 3 kez artan backoff'dan sonra yeniden bağlanmaya *devam* (3 bağlantıdan sonra vazgeçme yok), 1–60s capped exponential backoff + jitter; bağlantı kesintisi üretim motorunu durdurmaz. Retained `online=false` için LWT; tekrar bağlanınca online true. MQTT telemetry için sonsuz offline queue birikimi yok; son snapshot gönderilir. Event kayıp veya drop gözlemlenebilir metric ve TimescaleDB olay kaydı sağlar.

**Örnek telemetry mesajı** (rakamlar açıklayıcıdır, gerçek uygulamadan alınmamıştır):

```json
{
  "schema_version": 1,
  "machine_id": "CNC-01",
  "ts_utc": "2026-11-01T07:30:24.000Z",
  "sequence": 4880,
  "data": {
    "state_code": 40,
    "is_cutting": 1,
    "spindle_target_rpm": 4800.0,
    "spindle_actual_rpm": 4781.6,
    "spindle_load_pct": 63.8,
    "feed_actual_mm_min": 1080.0,
    "vibration_rms_mm_s": 1.72,
    "active_wear_pct": 47.8,
    "job_completed_qty": 56,
    "current_tool_id": 3
  }
}
```

**Not:** Bu mesaj yalnız bazı alanları gösterir; GERÇEK telemetry publisher sözleşmedeki **tüm live alanlarını** yayınlar, `tool_id` örneğindeki kısa ad yerine CSV'deki `current_tool_id` kullanılır. Uygulama mesaj şema örneklerini gerçek serializer ile üreten testle doğrular.

## 8. VERİTABANI, SÜREKLİLİK VE GEÇMİŞ

- TimescaleDB / PostgreSQL kalıcı Docker volume kullanır. C++ libpq parametreli sorguları ve transaction kullanır; telemetry + events saatlik BIGINT UTC-ms hypertable, JSON alanları JSONB. Her snapshot state/event ile atomik yazılır. Runtime writer ve salt okunur developer rolleri ayrıdır. Bağlantı kaybında engine çalışır, health bozulur, bounded retry ile geri bağlanır; ilk açılışta checkpoint okunamıyorsa sayaçları sıfırlamak yerine startup başarısız olur.
- `telemetry(ts_ms BIGINT PRIMARY KEY, seq BIGINT NOT NULL, state_code INTEGER, job_id INTEGER, program_id INTEGER, tool_id INTEGER, spindle_rpm REAL, load_pct REAL, feed_mm_min REAL, x_mm REAL, y_mm REAL, z_mm REAL, vibration REAL, spindle_temp_c REAL, power_kw REAL, energy_kwh REAL, wear_pct REAL, coolant_flow REAL, alarm_code INTEGER, good_count INTEGER, scrap_count INTEGER, payload_json JSONB NOT NULL)` — bütün CSV alanları `payload_json` altında 5sn aralıkla; SQL sorgulanabilir çekirdek metrikler kolonlarda.
- `events(id BIGINT, ts_ms BIGINT, type TEXT, code INTEGER, severity INTEGER, state_from INTEGER, state_to INTEGER, details_json JSONB, PRIMARY KEY(ts_ms,id))`.
- `orders(order_id INTEGER PRIMARY KEY, program_id INTEGER, part_id INTEGER, target_qty INTEGER, good_qty INTEGER, scrap_qty INTEGER, started_ms INTEGER, completed_ms INTEGER, status TEXT)` — sadece son 24 saat ile kesişen işler ve aktif iş; geçmişi tutmak gerekiyorsa telemetry 24s ilkesine uygun tutulur.
- `runtime_state(key TEXT PRIMARY KEY, value_json JSONB NOT NULL, updated_ms INTEGER)` — sayaçlar, 12 takımın wear, aktif emir, numaralar, PRNG state, energy. **Bu tablo 24 saatte silinmez**, çünkü süreç durumunu yeniden başlatmada korur; tarihsel telemetry değildir.
- SQL insert: 5sn aralıkla telemetry, state/event anında. `DELETE FROM telemetry WHERE ts_ms < now_utc_ms-86400000`; events ve bitmiş orders için 24 saatlik mantık, ancak aktif order kaydını silme. Temizlik 60sn; 24h+60sn tolerans ve query'de kesin `>= now-24h` filtresi.
- TimescaleDB background job her 60 saniyede eski chunk'ları düşürür ve sınır chunk içindeki 24 saatten eski satırları siler. Job engine kapalıyken de çalışır. PostgreSQL autovacuum/WAL varsayılan dayanıklılığı korunur.
- Boşluk: 10 saniyelik veri kaydı kesintisini TimescaleDB'da **uydurma değerlerle geriye dönük doldurma**. `sample_gap` event'i bırak. Önemli reboot'ta yarım parça discard ve `restart_interruption` kaydı. Program başlar başlamaz tam son 24 saat **önceden doldurulmuş** olarak gösterilmez; 24 saatlik pencere zamanla dolar.

**API (read-only):** `GET /health/live`, `GET /health/ready` (kısa), `GET /api/v1/about`, `GET /api/v1/status`, `GET /api/v1/history?from=<UTC>&to=<UTC>&limit=1440&step=60`, `GET /api/v1/events?limit=100`, `GET /api/v1/tools`, `GET /api/v1/orders`, `GET /api/v1/metrics` (Prometheus text format opsiyonel; fazla bellek isterse sade JSON). `from/to` ve limit strict validation, sayfalama, 24h dışı sorgu reddi/boş liste, 5000 satır üst sınırı. HTTP sadece configured private address; history downsample (1s/5s/60s vb.) SQL-side aggregate veya streaming sınırlı.

**Admin (varsayılan kapalı):** `POST /admin/scenario`, `POST /admin/inject-fault`, `POST /admin/ack-alarm`, `POST /admin/replace-tool`, `POST /admin/set-seed`, `POST /admin/trigger-workorder`. Yalnız `ENABLE_ADMIN=true`, constant-time API bearer token kontrolü, rate limit, audit event. Gerçek kontrol komutları bu API ile gerçek PLC'ye geçmez. Güvensiz geliştirme profilinde bile internete açık bırakma.

## 9. GÖRSELLEŞTİRME VE KULLANIM

- `/` statik HTML/CSS/vanilla JS, 2–5 saniyede status polling; dış script/font/CDN olmadan, düşük bellek, erişilebilir responsive arayüz.
- Üst panel: machine status, job, program, shift, program block, active tool, runtime since restart, protokol bağlantı health.
- CNC kartı: spindle actual/commanded, load, feed, XYZ, vibration, temperature, coolant, power/energy, total & job good/scrap, alarm.
- Tool magazine görünümü: 12 takım, wear %, remaining minutes, %80-warning/%95-change. Takımın aşınmasının zamanla ilerlediği 24h mini grafik.
- 24h grafikleri (canvas/SVG basit): load, spindle temp, rpm, vibration, wear, power; event/alarm timeline, work orders, good/scrap. Görselleştirme yalnız API okur, engine'i veya örnekleme hızını belirlemez.
- OEE: `Availability = RunTime/PlannedProductionTime`; `Performance = IdealCycleTime*TotalParts/RunTime`; `Quality=Good/Total`; `OEE = A×P×Q`. Planned production window'tan planlı molaları çıkar; setup/arıza kullanılabilirlik kaybıdır. `RunTime` ve microstop sınıflandırmasını konfigürasyonda açıklayıp sıfıra bölmeyi koru; **OEE live snapshot'taki döneme göredir**, default current shift. Perf >1 üretirse config/ideal-cycle hatası olarak incele; UI'da 100%'e sessizce kırpıp kusuru gizleme.

## 10. DOCKER, GÜVENLİK VE DAYANIKLILIK

```text
services:
  cnc-simulator:
    build: .
    restart: unless-stopped
    init: true
    ports:
      - "${BIND_IP:-127.0.0.1}:102:1102"
      - "${BIND_IP:-127.0.0.1}:4840:4840"
      - "${BIND_IP:-127.0.0.1}:8080:8080"
    volumes:
      - ./data:/data
      - ./config:/app/config:ro
    depends_on:
      mqtt-broker:
        condition: service_healthy
    healthcheck: ...
  mqtt-broker:
    image: eclipse-mosquitto:<tested-pinned-version>
    restart: unless-stopped
    ports:
      - "${BIND_IP:-127.0.0.1}:1883:1883"
    volumes:
      - ./mosquitto/config:/mosquitto/config:ro
      - ./mosquitto/data:/mosquitto/data
    healthcheck: ...
```

**Bu iskelet tam çalışır Compose değildir**: AI gerçek image tag, broker auth/ACL, healthcheck executable, kullanıcı/izinler, network, secrets, Dockerfile ve app config dosyalarını üretip çalıştırmalı. Default `BIND_IP=127.0.0.1` dış erişimi engeller; LAN/VPN'den test için LAN IP'ye bağla, güvenlik duvarında yalnız gerekli test kullanıcılarını aç. Port 102 host tarafında yetki/çakışma doğurabilir; Compose yayınlama 102:1102 iç prosesin root olmasını gerektirmez, host daemon yönetir.

- image multi-stage optimized (`-O2`, `-DNDEBUG`, strip debug symbols), CMake/Ninja; C++17; native dependencies **sabit SHA/tag**, SBOM + THIRD_PARTY_NOTICES. `Snap7 LGPLv3`, `open62541 MPLv2`; dağıtım yükümlülükleri incelenmeden lisans uygunluğu iddia etme.
- Non-root final runtime, read-only rootfs mümkünse, `/data` writable, no `privileged`, no `NET_ADMIN`; seccomp default, `cap_drop: ALL` (test et). Broker credentials Git'e girmez; `.env.example` yalnız placeholder.
- Her servis SIGTERM'e yanıt verir; TimescaleDB transaction flush, last runtime state save, MQTT availability offline best-effort, Snap7 stop. Crash sonrası Docker restart & recovery; client reconnect.
- log: JSON structured, rotation (`max-size` 5m, `max-file` 3); normal telemetry loglama yok. RAM growth testinde toplam RSS hesabı (2 container) ve geçici peak ayrı raporla; `docker stats` cgroup cache yorumunu ve proses RSS'lerini birlikte göster.
- Security scope: veri **gerçek üretim ortamına geri yazılamaz**; gerçek PLC IP'lerine otomatik connect/scan yok. S7 port 102 ve şifresiz MQTT/OPC UA None internete açık bırakılmayacak. Admin kapalı default. Hız sınırlama, input validation, minimal dependency.

## 11. TEK SEFERLİK YAZILIM TESLİMATI — DOSYALAR

AI, verilen blueprint paketini **başlangıç sözleşmesi** olarak kullanarak repo'da aşağıdaki çıktıları yaratmalı:

```text
.
├── README.md                        # sıfırdan kurulum, demo, bağlantı tabloları
├── SPEC.md                          # bu dosya ve gerekiyorsa ADR açıklamaları
├── CLAUDE.md / AGENTS.md           # hafif ajan yönergeleri
├── CMakeLists.txt
├── Dockerfile                       # multi-stage, non-root
├── compose.yaml
├── .env.example
├── .gitignore
├── LICENSES/ / THIRD_PARTY_NOTICES.md
├── config/
│   ├── plant.yaml                  # makine, vardiya, olasılıklar, retention
│   ├── programs.yaml               # NC segment şablonları
│   ├── tools.yaml                  # 12 takım / life
│   └── faults.yaml                 # anomaliler ve recovery
├── contracts/
│   ├── tags.csv                    # kesin veri sözleşmesi
│   ├── mqtt.schema.json            # JSON Schema / schema_version=1
│   └── s7_map.md                   # DB tabloları, dış istemci örnekleri
├── src/
│   ├── main.cpp, config.*, core/{engine,clock,random,machine,job,program,tool,fault,quality}.*
│   ├── adapters/{s7_server,opcua_server,mqtt_publisher,timescale_store,http_server}.*
│   ├── transport/{snapshot,contract,serializer}.*
│   └── ui/{index.html,style.css,app.js}
├── tests/
│   ├── unit/                        # deterministic fake-clock, physics, tools, state
│   ├── integration/                 # gerçek S7/UA/MQTT istemciler, SQL/REST
│   ├── e2e/                         # compose spin-up, restart, fault injection
│   └── soak/                        # 24h RAM/leak/liveness
├── scripts/
│   ├── bootstrap.sh                 # tek komut dev setup
│   ├── test-all.sh                  # CI ve yerel doğrulama
│   ├── smoke-connectors.sh          # protokol kanıtı
│   └── measure-resources.sh         # RSS, CPU, DB boyutu
└── docs/
    ├── CONNECTION_GUIDE.md         # 3 konnektörün host/port/DB/NodeId/topic adresleri
    ├── TEST_REPORT.md              # PASS/FAIL, komut, gerçek çıktı, ölçümler
    └── LIMITATIONS.md              # PLC ve fiziksel model sınırları
```

`yaml-cpp` gibi config parse dependency bellek/karmaşıklığı büyütürse, program/plant/fault şablonlarını derleme zamanında JSON veya satır tabanlı küçük config'e çevirmek serbest; **sözleşmedeki anlam, örnek ve testler değişmeyecek**. Config schema testlerini yaz. S7/UA/MQTT kütüphane CMake entegrasyonu gerçek header ve API sürümüne bakılarak yapılacak; belgelere bakmadan uydurulmuş API fonksiyonları kabul edilmez.

## 12. HATAYA DAYANIKLILIK VE SINIR DURUMLARI

- **Clock:** wall-time ileri/geri alınırsa engine hızlanmaz/yavaşlamaz; only `steady_clock` ilerletir. UTC timestamp geriye atlarsa event ve kalite mesajı. Reboot'ta uzun gap geçmişi doldurulmaz; restart event'i eklenir.
- **PRNG:** config seed ve state persist, aynı seed/program dizisi/fake clock ile golden-scenario replay deterministik. Üç protokolün publish sırası PRNG state'i etkilememeli.
- **Yük artışı:** 3 S7 + 3 UA + 3 MQTT subscribers hedef, max S7=8, UA=10, broker client limit=25; HTTP query limit. Fazla client resource exhaustion olmadan reddedilir.
- **TimescaleDB:** disk dolu/readonly durumunda engine çalışabilir, telemetry ve latest durumu devam; DB health false + alarm/log, disk dönünce yeni samples yaz, tarihsel uydurma yapma. Bağlantı/query retry bounded; statement/lock timeout.
- **MQTT broker down:** engine/S7/OPC UA/DB devam, backoff reconnect, availability stale/offline, tekrar bağlanınca state/tools retained güncellenir.
- **S7 client writes:** dışarıdan counter/tool wear yazmak gerçek model durumunu değiştiremez; test bunu kanıtlar; server side DB bytes'ı da anlık manipülasyonla sahte göstermenin önüne geçmek için callback engeli veya memory refresh + doc risk.
- **Alarmlar:** alarm bitişinde seviyesi ve süresi gerçeğe uygun kapanır; `alarm_acknowledged=0` aktif alarma bağlanır; warning vs stop ayrımı; düzeltmeden doğrudan RUNNING'e dönme.
- **Retention:** aktif iş emri geçmişten silinse bile iş ve takım state'i durmalı, son 24h pencere + 60 sn dışındaki telemetry/events görünmemeli. Time zone farkları için her zaman UTC karşılaştır.

## 13. OTOMATİK TEST MATRİSİ / KABUL KRİTERLERİ

**Hiçbir AI "tamamlandı" diyemez; aşağıdaki test çıktıları `docs/TEST_REPORT.md` içine komut, tarih/sürüm, gerçek PASS/FAIL, başarısızlık gerekçesi ve ölçümle kaydedilmedikçe.**

| ID | Test | Kabul |
|---|---|---|
| T01 | `cmake -S . -B build && cmake --build build -j2` | Temiz Linux ortamında başarılı |
| T02 | `docker compose up -d --build` ve 3 servis portu | 2 konteyner, 4 port reachable (S7, UA, MQTT, HTTP) |
| T03 | S7 gerçek istemci `DB1/2/3/4` oku | Her DB doğru boyutta, endian/offset/tag değerleri doğru |
| T04 | 1 S7 + 1 OPC UA + 1 MQTT ile aynı sekans değerleri | Aynı sequence içindeki alanlar eşit (REAL tolerans 1e-3) |
| T05 | 20 dk normal-mode akış veya hızlandırılmış fake-clock | RUNNING, SETUP, TOOL_CHANGE, IDLE durumları, en az 1 tamamlanmış parça |
| T06 | tool wear aktif CUT/DRILL | monoton artış; idle ve stop sırasında sabit; 80/95/100% olayları |
| T07 | alarm injection | uygun sensör eğrisi → olay → duruş → kontrollü recovery; false invariants yok |
| T08 | iş emri bitişi | counters invariant, yeni job + setup, program id ilerlemesi |
| T09 | kalıcılık | compose restart sonrası wear, energy, counters, active workorder korunur, incomplete piece sayılmaz |
| T10 | 24h retention fake clock | 24h dışındaki event/telemetry yok, runtime state bozulmaz; 24h+60s temizlik |
| T11 | TimescaleDB geçmiş | 5s örnekleme, olaylar ayrı; history API valid + limit |
| T12 | MQTT broker restart | publisher reconnect, retained state/tools/online, S7/UA/DB devam |
| T13 | live 24h soak | simülatör+broker RSS ≤100MiB hedef; TimescaleDB ve üç servis toplamı ayrıca raporlanır, 24h crash yok, sürekli RSS trendi/leak yok |
| T14 | CPU idle/live | 1 vCPU hostta 5dk ortalama ≤5% CPU *hedef*, ölçümle rapor |
| T15 | kötü JSON/HTTP + admin yok | auth, validation, rate-limit, no crashes; external admin kapalı |
| T16 | Sensör fiziksel tutarlılık | CUT→spindle ON, kapı kapalı, feed>0; E-stop→feed 0; counters tutarlı |
| T17 | Synthetic stress run | 1000 fake cycles, transitions legal, negative/NaN/Inf yok |
| T18 | UI | offline CDN yok, 24h grafik ve 12 takım görünür, refresh çalışır |
| T19 | S7 write attempt | makine durumuna hiçbir yetkisiz write etki edemez; gerekiyorsa read-only kaynak/ACL riski açık |
| T20 | Security/lisans | secrets Git'te yok, S7-1200 firmware iddiası yok, THIRD_PARTY_NOTICES var |

**Rapor formatı:** `Test ID | Actual command | Timestamp | Result | Measured values | Log evidence | Remaining limitation`. T13'ün 24h soak gerçek zamanla tamamlanmadığı bir oturumda açıkça `NOT RUN` kalması gerekir; hızlandırılmış testi gerçek 24 saatle eşdeğer gösterme. T02/03/04, T06/09/10 ve T13 kritik; başarısızsa tamamlandı deme.

**İstatistik kalite sınamaları (heuristic; mutlak benzerlik ispatı değil):** normal 24h sample için fiziksel invariant %100, zaman içinde wear ile load/vibration arasında makul pozitif korelasyon (proses/malzeme kontrol edilerek), zero cutting dışı wear drift = 0, power/energy integral farkı <1%, good+reject=total %100, spindle hedef değişimi sonrası actual anlık teleport yapmaz, 10+ farklı event/anomaly türü uzun sentetik koşuda aktive edilebilir, no periodic fixed 60s fault spam. Histogram ve 3 adet 24h grafik raporda sunulacak.

## 14. AI / VIBE CODING OPERASYON KURALLARI

- **Tek görev, tek teslimat:** `START_HERE.md` komutu ile agent bütün sistemi kodlayıp çalıştırır, test eder ve raporlar. Faz / milestone / turlara bölme yok; gerektiğinde kendi içinde kısa TODO list kullanabilir.
- **Bağlam:** `CLAUDE.md` ve `AGENTS.md` en fazla ~100 satır; tamamını her adımda tekrar okuyup token harcama. Detay gerektiğinde ilgili `SPEC.md` bölümü, `contracts/tags.csv`, `config/` dosyasını oku. Log output sınırlı `tail -n 80`; test çıktısını `logs/` altında kaydet.
- **Agent sayısı:** ana implementer; bir `protocol-reviewer` (yalnız gerektiğinde), bir `test-auditor` (sadece önemli kontrollerde). Her dosya değiştirmede subagent yaratma. Varsayılan ayrı agent gerekmiyorsa yalnız ana agent.
- **Skills:** `.claude/skills/industrial-test/SKILL.md` yalnız entegrasyon / kabul durumunda kullan. Rule/hook'lar her tool edit sonrası derleme başlatmaz (token, süre, gereksiz yük); `Stop`/manuel `scripts/test-all.sh` kullanılabilir.
- **Hooks:** `.claude/settings.json` ve `.claude/hooks/preflight.sh` sadece tahrip edici shell komutları için güvenlik kontrolü; default no-op/hook must not prevent normal work; `Stop` hook çok ağır testleri otomatik tekrar tekrar çalıştırmaz. Gerçek sürümün hook API'sine göre doğrula.
- **MCP / graph indexing:** boş repo için Graphify benzeri kod indeksleme gerekmez; büyük repo oluşunca bile ölçülmüş token kazancı yoksa kurma. Ajanın kaynak okuma sınırları `rg` + dosya yolu + test raporu. Ağda arama yalnız gerçekten bloklanan API/dep doğrulamak için.
- **Agent iş sırası** (faz değil, bağımlılık mantığı): sözleşme/konfigürasyon → engine/clock/programs/tools → TimescaleDB → MQTT → S7 → OPC UA → HTTP/UI → compose → entegrasyon ve soak. Her adımda mini unit test, sonuçta *tek* çalışır dağıtım. Bu sıra proje teslimatını parçalamaz.
- **Kendini denetle:** bir kütüphane çalışmadığında gerçeğe aykırı yeşil test üretme; kolay diye Python production'a dönme; 100MiB hedefi gerçek ölçümle sağlanamıyorsa açık BLOCKER ve alternatif (daha düşük UA features / sampling / node sayısı) raporla, gereksinimi sessizce değiştirme.

## 15. KAYNAKLAR, SINIRLAR VE GERÇEKLİK KONTROLÜ

1. Snap7 Server kılavuzu: https://snap7.sourceforge.net/snap7_server.html — sanal CP ve DB paylaşımı; firmware değil, kaynak yazarının da belirttiği gibi CPU emülasyonu tam değildir.
2. Snap7 API: https://snap7.sourceforge.net/snap7_api_ref.html — register/lock/unlock API.
3. Snap7 lisans: https://snap7.sourceforge.net/licensing.html — LGPLv3.
4. Siemens S7-1200 PUT/GET ve standard DB: https://docs.tia.siemens.cloud/r/simatic_s7_1200_manual_collection_enus_20/communication/s7-communication/get-and-put-read-and-write-from-a-remote-cpu — gerçek S7-1200 test konfigürasyonu, simülatöre TIA proje gerektirmez.
5. open62541 resmi site ve lisans: https://open62541.org/download/ — C server, MPLv2. Projede sürüm pinlenerek API doğrulanmalı.
6. OPC UA Machine Tools companion specification: https://reference.opcfoundation.org/specs/OPC-40501-1/full — özellikle Job ve ToolLife semantiği. V1 kendi namespace'i ile çalışır, conformance iddiası yok.
7. Eclipse Mosquitto auth: https://mosquitto.org/documentation/authentication-methods/ — ACL/parola; gerçek broker gerekir.
8. TimescaleDB: https://www.tigerdata.com/docs/reference/timescaledb/hypertables/create_hypertable — hypertables; PostgreSQL libpq: https://www.postgresql.org/docs/17/libpq.html.
9. MTConnect 2.5.0 doküman endeksi: https://docs.mtconnect.org/ — machining telemetri semantiği; MTConnect server V1 kapsamına alınmamıştır.
10. Claude Code skills/agents yaklaşımı: https://claude.com/blog/steering-claude-code-skills-hooks-rules-subagents-and-more — proje dosyaları, context/bellek sınırları.

**Tasarım varsayımı olan değerler:** fiziksel aralıklar, tool-life katsayıları, 3 vardiya, iş emri büyüklükleri, hata hazard ve süreleri; örnek saha ölçümü değildir. Müşteri saha verisi alınırsa `config/` üzerinde kalibrasyon yapılır, protokol sözleşmesi geriye dönük kırılmaz.

## 16. SON TESLİM TANIMI

Ürün tamamlandı ancak şu koşullarda denebilir: `docker compose up -d --build` sonrası S7/OPC UA/MQTT gerçekten harici istemcilerle okunur, bir makine durmadan kendi olaylarını üretir, takım aşınması zamana yayılır, 24h TimescaleDB geçmişi analiz edilir, UI makineyi gösterir, reboot'ta sayaçlar korunur, 24h soak ve RAM ölçümü raporludur. Her şey **test ve ölçüm kanıtıyla** sunulur. Bu dokümanın verilmesi uygulamanın veya testlerin yapılmış olduğu anlamına gelmez.

### Authorized PiriVision compatibility extension (2026-11-03)

An optional PgBouncer process is bundled in the CNC simulator image/container;
no separate proxy image/service. It exposes read-only SQL on TCP/6432 without
client TLS/certificates by explicit operator request. Existing TimescaleDB and
broker services remain. Upstream uses verify-full TLS; direct PostgreSQL TLS
and private S7/admin restrictions remain. Credentials are mounted files.
Runtime memory measurements must include the proxy and its supervisor.
