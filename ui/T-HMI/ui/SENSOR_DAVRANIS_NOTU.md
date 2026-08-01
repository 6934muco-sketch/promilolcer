# Sensör Davranış Notu — Hangi Sensör Hangi Durumda Neyi Tetikliyor

Bu not, `ui.ino` içindeki **güncel** kodun ne yaptığını anlatır (kod okunarak
çıkarılmış gerçek davranış, tasarım önerisi değil). Kapsam: MQ-3 (alkol),
DHT22 (ortam sıcaklık/nem) ve **BMP280 (nefes/basınç)**. LED ve ses tarafı
için sırasıyla `LED_DAVRANIS_NOTU.md` ve `SES_DAVRANIS_NOTU.md`'ye bakın.

---

## 0) Donanım geçmişi: DHT11 → BME280 → BMP280

Nefes kanalındaki (üfleme ağzına yakın) sensör önce **DHT11'den BME280'e**,
sonra **BME280'den BMP280'e** geçirildi. BMP280, BME280'in nem ölçme
özelliği olmayan (sadece basınç + sıcaklık) sürümüdür — kod zaten sadece
basıncı okuduğu için (`bmeNefes.readPressure()`) bu geçiş davranışsal bir
fark yaratmadı, sadece kütüphane/tip değişti (`Adafruit_BME280` →
`Adafruit_BMP280`, obje adı `bmeNefes` korundu).

| | Eski (DHT11) | BME280 (ara aşama) | **Güncel (BMP280)** |
|---|---|---|---|
| Haberleşme | Tek hat (one-wire), GPIO25 | I2C (SDA=GPIO21, SCL=GPIO22) | I2C (SDA=GPIO21, SCL=GPIO22), adres `0x76` (yedek `0x77`) |
| Ölçtüğü büyüklük | Nem (%RH) | Basınç (hPa) + nem (kullanılmıyor) | Basınç (hPa) — nem donanımı zaten yok |
| Okuma aralığı | 1000ms | 100ms | 100ms (ana sensör timer'ıyla aynı hız) |
| Kirlilik/üfleme tespiti | Nefes-nem − Ortam-nem farkı > eşik | Anlık basınç − KENDİ EMA taban basıncı farkı > eşik | Aynı (basınç tabanlı) |
| Arıza (donma) tespiti | Nem değişmiyorsa | Basınç değişmiyorsa + I2C `begin()` başarısızsa anında arızalı | Aynı |
| `sensorID()` | — | `0x60` | **`0x58`** |
| Kod tarafı | `dhtNefes` | `bmeNefes` (Adafruit_BME280) | `bmeNefes` (Adafruit_BMP280, aynı değişken adı) |

**Neden BMP280'e geçildi:** Nem hiç kullanılmadığı için (kirlilik tespiti
zaten basınca dayanıyordu, bkz. §2) BME280'in nem donanımı gereksizdi;
BMP280 aynı iş için yeterli ve daha basit/ucuz bir alternatif.

**⚠️ Sahada kalibrasyon gerekiyor:** `KANAL_KIRLILIK_ESIK_HPA = 1.0f` şu an
yer tutucu bir başlangıç değeri — gerçek donanımda üfleme sırasında BMP280'in
gördüğü basınç sıçramasının büyüklüğü tüpün/kanalın sızdırmazlığına göre
değişir.

**Canlı test/kalibrasyon logu:** `NEFES_BASINC_SERIAL_LOG` (1 = açık,
varsayılan) açıkken Serial monitörde 300ms'de bir şu satır basılır:

```
[NEFES-BMP280] basinc:1008.32 hPa  ortalama:1008.30 hPa  fark:+0.02 hPa  kirli:hayir  arizali:hayir
```

Sensörün gerçekten okuyup okumadığını (`basinc` değeri sabit/NaN değil,
gerçekten değişiyor mu) ve üflerken `fark` sütununun ne kadar sıçradığını
görmek için bunu kullanın; `KANAL_KIRLILIK_ESIK_HPA`'yı gözlenen gerçek
sıçrama genliğine göre ayarlayın (sıçrama eşikten belirgin şekilde büyük
olmalı ki gürültüyle karışmasın). İşiniz bitince `NEFES_BASINC_SERIAL_LOG`'u
`0` yapıp kapatabilirsiniz. `setup()` ayrıca açılışta BMP280 bulunup
bulunmadığını (I2C adresi + `sensorID`, BMP280 için `0x58` olmalı) bir kez
loglar.

**Düzeltilen bir hata (BME280 döneminden kalma, hâlâ geçerli):**
`nefesBasincOrtalama` başlangıçta hardcoded `1013.25f` (deniz seviyesi
standardı) idi; gerçek yerel basınç bundan kolayca birkaç–onlarca hPa uzak
olabildiği için bu, cihaz açılır açılmaz kalıcı "kanal kirli" kilitlenmesine
(sürekli "kanal temizleniyor" döngüsü) yol açıyordu. Artık ilk gerçek BMP280
okumasında baz değer gerçek anlık basınca anında oturtuluyor
(`nefesBasincOrtalamaIlkOkuma` bayrağı, bkz. `nefesBmeGuncelle()`).

---

## 1) Donanım / pin özeti

| Sensör | Görev | Bağlantı | Pin(ler) |
|---|---|---|---|
| MQ-3 | Alkol (ana ölçüm) + yedek kirlilik tespiti + OLCUM'un yedek üfleme-tetikleme yolu | Analog | GPIO34 |
| DHT22 (ortam) | Sadece sıcaklık/nem kompanzasyonu | Tek hat | GPIO32 |
| **BMP280 (nefes)** | **Üfleme kanalı kirlilik/üfleme tespiti (basınç) + OLCUM'un birincil üfleme-tetikleme sinyali** | **I2C** | **SDA=GPIO21, SCL=GPIO22** |
| Buton | Ölçüm başlat / R0 kalibrasyon (uzun basma) | Dijital giriş (pull-up) | GPIO27 |
| Fan | Havalandırma/temizlik | Dijital çıkış (transistör/röle üzerinden) | GPIO26 |
| DFPlayer Mini | Ses | UART2 | GPIO16/17 |
| TFT ekran | Görüntü | SPI | GPIO19/23/18/15/2/4 |

GPIO4, ekranın TFT_RST pini ile çakıştığı için kullanılamaz. GPIO21/22 (I2C)
öncesinde boştaydı, hiçbir çakışma yok.

**⚠️ "BMP280 sağlıklıysa sadece BMP280'e bakılır" kuralı HER YERDE geçerli
DEĞİL.** Bu kural sadece `kanalKirliMiEfektif` (yani TABAN_OLC) için doğru.
`BEKLE` ekranındaki otomatik/butonsuz kanal temizleme tetikleyicisi
(`butonsuzKirlilikTespitEdildiMi()`, §3) BMP280'in sağlık durumuna
BAKMAKSIZIN, MQ-3'ün kendi 10sn'lik ortalamasını her zaman BMP280 sinyaliyle
**OR**'luyor — yani BEKLE'deyken MQ-3'e yeterince yüksek bir promil verirsen,
BMP280 tamamen sağlıklı olsa bile fan otomatik açılır.

---

## 2) BMP280 (nefes) — kirlilik/üfleme tespiti nasıl çalışıyor

`nefesBmeGuncelle()` her 100ms'de bir basıncı okur (`bmeNefes.readPressure()/100`,
hPa'ya çevrilir) ve iki bağımsız kontrolü tetikler:

### a) Kirlilik/üfleme tespiti (`nefesKirlilikKontrol()`)

- `nefesBasincOrtalama`, **İLK gerçek BMP280 okumasında o anki gerçek basınca
  anında "snap"lenir** (`nefesBasincOrtalamaIlkOkuma` bayrağı, `nefesBmeGuncelle()`
  içinde). Bu adım kritik: kod başlangıçta değişken `1013.25f` (deniz seviyesi
  standart basıncı) ile başlatılıyor olsa da, gerçek yerel basınç irtifa/hava
  durumuna göre bundan kolayca birkaç–onlarca hPa uzak olabilir. Snap
  yapılmasaydı, ilk okumalar hemen `KANAL_KIRLILIK_ESIK_HPA` eşiğini aşar,
  kirlilik VARKEN EMA'nın dondurulması gerektiğinden (aşağıya bakın) ortalama
  hiç doğru değere adapte olamaz ve `kanalKirliMi` kalıcı olarak takılı kalırdı
  — pratikte gözlenen **sürekli "kanal temizleniyor" döngüsü** tam olarak
  buydu, düzeltildi.
- Snap'ten sonra `nefesBasincOrtalama`, kirlilik **YOKKEN** yavaş bir EMA
  (`NEFES_BASINC_ORTALAMA_ALFA = 0.02`) ile güncellenir — bu "sensörün gördüğü
  sakin/ortam basıncı" kabul edilir. (Kirlilik VARKEN ortalama dondurulur ki
  olay kendi kendini maskelemesin.)
- `fark = nefesBasinc - nefesBasincOrtalama`. `fark > KANAL_KIRLILIK_ESIK_HPA`
  (şu an 1.0 hPa, **sahada ayarlanmalı**) olan her okumada `kirlilikArdisikSayac`
  artar, değilse sıfırlanır.
- `kirlilikArdisikSayac >= KANAL_KIRLILIK_ARDISIK_SAYI` (2 ardışık okuma) olunca
  `kanalKirliMi = true`. Basınç düşünce bayrak otomatik temizlenir (latch değil).

**`kanalKirliMi` (ham BMP280 bayrağı) iki farklı bağlamda kullanılıyor:**
BEKLE'nin 10sn'lik penceresinde (§3, `bmeKirlilikGorulduMu` üzerinden) ve
TABAN_OLC'da (`kanalKirliMiEfektif` üzerinden, aşağıya bakın) "kirlilik"
anlamına gelir (kanal temizlenmeli); OLCUM'da AYNI ham bayrak DOĞRUDAN
kullanılır ama "üfleme başladı, analiz kısmına geç" anlamına gelir (bkz. §4
aşağıda) — aynı fiziksel olay (basınç sıçraması), duruma göre farklı
yorumlanıyor. **Not:** R0_KALIBRASYON artık `kanalKirliMi`'ye hiç bakmıyor
(eski SETTLE fazı kontrolü kaldırıldı, bkz. §3).

### b) Arıza (donmuş/erişilemez/anlamsız sensör) tespiti

Üç bağımsız yol arızalı sayılmaya götürür:

1. **Anlık (init):** `setup()` içinde `bmeNefes.begin(0x76)` (olmazsa `0x77`)
   başarısız olursa `bmeNefesHazir = false` kalır ve `nefesBmeGuncelle()` hiç
   okuma denemeden doğrudan `nefesSensorArizali = true` yapar.
2. **Donma (`nefesArizaKontrol()`):** Sensör I2C'de yanıt vermeye devam
   ediyor ama okunan basınç `NEFES_ARIZA_SURESI_MS` (10 dakika) boyunca
   `NEFES_DEGISME_ESIK_HPA`'dan (0.03 hPa) fazla değişmiyorsa arızalı kabul
   edilir. Değer tekrar kıpırdadığı an otomatik "sağlıklı"ya döner (latch
   değil).
3. **Fiziksel olarak imkansız değer (`nefesBmeGuncelle()` içindeki aralık
   kontrolü):** Okunan basınç `NEFES_BASINC_GECERLI_MIN_HPA`–
   `NEFES_BASINC_GECERLI_MAKS_HPA` (800–1300 hPa) aralığının dışındaysa
   anında arızalı sayılır (VCC kesilip SDA/SCL/GND bağlı kalırsa parazitik
   beslenip anlamsız gürültülü değer üretebilir — "donma" testi bunu
   YAKALAYAMAZ, bu yüzden ayrı bir aralık kontrolü var). Latch değildir.

### c) Efektif kirlilik bayrağı

```
kanalKirliMiEfektif = nefesSensorArizali ? mq3KirliMi : kanalKirliMi;
```

Nefes-BMP280 sağlıklıysa onun (basınç tabanlı) sonucu kullanılır; arızalı
sayıldıysa (I2C'de hiç bulunamadıysa veya donmuşsa) **MQ-3'ün kendi ADC
okumasına** dayalı `mq3KirliMi` devreye girer.

**Önemli — `kanalKirliMiEfektif` kodda ŞU AN SADECE `TABAN_OLC`'da
kullanılıyor** (`ui.ino:1069`). OLCUM ve BEKLE bu bayrağı DEĞİL, `kanalKirliMi`
ham değerini (kendi ayrı mantıklarıyla) kullanıyor; SONUC hiçbirine bakmıyor
(koşulsuz); R0_KALIBRASYON'un eski SETTLE-fazı kontrolü kaldırıldığı için o da
artık buna bakmıyor. Yani "sağlıklıysa BMP280, arızalıysa MQ-3" saf fallback
kuralı gerçekte sadece TABAN_OLC için geçerli — diğer durumlarda MQ-3'ün rolü
farklı (BEKLE'de her zaman paralel/OR, OLCUM'da tamamen ayrı bir sayaç).

```
void mq3KirlilikKontrol(float hamPromil) {
  if (hamPromil > UFLEME_ESIK_PROMIL) kirlilikArdisikSayac++; else sifirla;
  mq3KirliMi = (ardisikSayac >= KANAL_KIRLILIK_ARDISIK_SAYI);
}
```

**Buton basılı değilken** (`alkolmetre_timer_cb()` içinde `durum != OLCUM`
iken çağrılır) MQ-3'ün ham okuması (`hamPromil`) `UFLEME_ESIK_PROMIL` (0.05)
eşiğini **2 ardışık okuma** (`KANAL_KIRLILIK_ARDISIK_SAYI`) boyunca aşarsa,
kanala informal/kalıntı bir üfleme yapıldığı kabul edilip `mq3KirliMi = true`
olur. Ama bu bayrağın sonucu (`mq3KirliMi`) yukarıda belirtildiği gibi kodda
**sadece `kanalKirliMiEfektif` üzerinden, dolayısıyla sadece TABAN_OLC'da**
etkili oluyor — BEKLE kendi ayrı `mq3OrtalamaPromil()` ortalama kontrolünü
kullanıyor (§3), OLCUM'un kendi üfleme-tetikleme mekanizması (§4) da tamamen
ayrı bir sayaç. Yani MQ-3 tarafında üç bağımsız mekanizma var:
`mq3KirlilikKontrol()`/`mq3KirliMi` (→ sadece TABAN_OLC), `mq3OrtalamaPromil()`
(→ sadece BEKLE), `uflemeTetikMq3ArdisikSayac` (→ sadece OLCUM, BMP280
arızalıyken).

---

## 3) Kanal temizlemeye (`KANAL_TEMIZLE`) nereden geçiliyor

Durum makinesi **4 farklı noktada** `kanalTemizlemeyeGec(mesaj)` (`ui.ino`)
yardımcı fonksiyonunu çağırır — fanı başlatır, `ekran11`'e geçer (fan ikonu
döner + **10sn**'lik geri sayım gösterilir — eskiden 18sn), `uic_fantext`'i
verilen mesajla doldurur, 022 sesini çalar ve `KANAL_TEMIZLE`'ye girer:

| Nerede | Tetikleyici | Mesaj |
|---|---|---|
| `BEKLE` | `butonsuzKirlilikTespitEdildiMi()` — **BMP280 VEYA MQ-3**, aşağıya bakın | **"olcum oncesi kirlilik algilandi lutfen temizlenene kadar bekleyiniz"** |
| `TABAN_OLC` | `kanalKirliMiEfektif` (anlık) — taban ölçümü sırasında kirlilik tespit edilirse, ölçüm İPTAL | "Yanlis ufleme algilandi, lutfen cihaz temizlenene kadar bekleyiniz" |
| `SONUC` (cooldown bitişinde) | **KOŞULSUZ** — kirlilik sensörüne BAKILMAKSIZIN her ölçümden sonra | "Kanal temizleniyor, lutfen bekleyiniz" |
| `R0_KALIBRASYON` (ÖRNEKLE fazında) | **MQ-3 üfleme tespiti** — pencere ortalamasına göre ani %5+ Rs düşüşü (`R0_KAL_UFLEME_ORANI`), BMP280 karışmıyor, bkz. §5 | "Yanlis ufleme algilandi, lutfen cihaz temizlenene kadar bekleyiniz" |

**Bu son satırın özel bir devamı var:** normalde `KANAL_TEMIZLE` bitince (10sn
sonunda) doğrudan `ekran1`'e (BEKLE) dönülür. Ama bu spesifik tetikleyicide
(`r0KalTemizlemeSonrasiMesajGoster` bayrağı) `KANAL_TEMIZLE` bitince BEKLE'ye
gitmeden önce `ekran4`'e (kalibrasyon ekranı) geçilip `uic_kalibrasyontext`'e
**"KANAL TEMIZ DEGIL KALIBRASYON YAPILAMADI"** yazılır, yeni bir durum olan
`R0_KAL_BASARISIZ_MESAJ`'da bu sabit **3sn** gösterilir, sonra BEKLE'ye
dönülür — ama bu dönüşte **"HAZIR" yazısı gösterilmez** (`hazirMesajiniGoster()`
çağrılmaz), diğer tüm KANAL_TEMIZLE→BEKLE dönüşlerinden farklı olarak.

R0 kalibrasyonunun **SETTLE fazındaki** kanal kirlilik kontrolü (BMP280/MQ-3
tabanlı `kanalKirliMiEfektif`) tamamen **kaldırıldı** — SETTLE bitince
kirliliğe bakılmaksızın doğrudan `R0KAL_ORNEKLE`'ye geçilir. Bunun yerine
PURGE fazı 5sn'den **30sn**'ye çıkarıldı (`R0_KAL_PURGE_SURESI_MS`), kanalın
ayrı bir kirlilik kontrolüne gerek kalmadan yeterince temizlendiği
varsayılıyor.

`KANAL_TEMIZLE`'nin kendisi: nem/basınç eşiği beklenmez, **sabit**
`KANAL_TEMIZLEME_SURESI_MS` (**10sn**) sonunda koşulsuz `ekran1`'e (BEKLE)
döner ve `hazirMesajiniGoster()` ile 3sn "HAZIR" gösterilir.

**⚠️ Fan SADECE `KANAL_TEMIZLE` ekranında 10sn çalışmıyor.** `fanBaslat()`
gerçekte OLCUM biter bitmez, SONUC'a geçişte hemen çağrılıyor
(`ui.ino:1198`, "üfleme bitti: havalandırmayı hemen başlat") — yani ölçüm
sonucu ekrana yazılır yazılmaz fan açılıyor. Fan, SONUC'un cooldown süresi
boyunca (`COOLDOWN_MS = 10000`, 10sn) açık kalıp `KANAL_TEMIZLE`'ye kesintisiz
devam ediyor, ancak `KANAL_TEMIZLE` bitiminde durduruluyor (`fanDurdur()`,
`ui.ino:~1262`). Yani normal bir ölçüm sonrası fan toplamda **~20 saniye**
(10sn SONUC + 10sn KANAL_TEMIZLE) kesintisiz çalışıyor, sadece
`KANAL_TEMIZLE`'nin kendi 10sn'i değil. (R0 kalibrasyonunun PURGE fazında —
30sn — ve TABAN_OLC/BEKLE kaynaklı `KANAL_TEMIZLE`'lerde ise fan gerçekten o
anki tetikleyicinin kendi süresi kadar çalışıyor, ekstra bir öncesi yok.)

`ekran11` böylece SADECE `KANAL_TEMIZLE`'de görünür — `SONUC` (yeşil/kırmızı
LED) `ekran2`'de kalır. Detay için bkz. `LED_DAVRANIS_NOTU.md` §3.

### `BEKLE`'nin butonsuz kontrolü: `butonsuzKirlilikTespitEdildiMi()`

Diğer 3 tetikleyiciden farklı olarak, `BEKLE`'nin buton-basılmadan-önce
kontrolü artık anlık `kanalKirliMiEfektif`'i DEĞİL, iki koşulun **OR**'unu
kullanıyor (`ui.ino`, `bmeKirlilikGorulduMu`/`sonBmeKirlilikZamani`/
`mq3OrtalamaPencere[]`), sürekli olarak son 10 saniyelik pencereye bakarak:

1. **BMP280:** son `UFLEME_ONCESI_PENCERE_MS` (10sn) içinde en az bir kez
   `kanalKirliMi` true olmuş mu.
2. **MQ-3:** son 10sn'nin (100 örnek × 100ms, `mq3OrtalamaPencere[]` kayan
   pencere) `hamPromil` ortalaması `MQ3_ORTALAMA_ESIK_PROMIL` (0.1 promil)
   üstünde mi.

İkisinden **biri** doğruysa (OR) `kanalTemizlemeyeGec(...)` çağrılır.

---

## 4) OLCUM'un üfleme-tetikleme mantığı: "sensör bekleme" → "analiz kısmı"

OLCUM durumu artık iki alt-faza ayrılıyor (`analizYaziGosterildi` bayrağıyla
ayırt edilir, ayrı bir `Durum` enum değeri değil):

- **"Sensör bekleme"** (`analizYaziGosterildi == false`): üfleme henüz
  algılanmadı, LED mavi (`LED_BEKLE`).
- **"Analiz kısmı"** (`analizYaziGosterildi == true`): üfleme algılandı, 6sn
  (`ANALIZ_OLCUM_SURESI_MS`) sürer, LED turuncu ilerleyen dolum (`LED_UFLEME`).

Geçiş, aşağıdaki ÖNCELİKLİ mantıkla tetiklenir (`ui.ino`, `case OLCUM:`):

```
eğer nefesSensorArizali == false (BMP280 sağlıklı):
    tetikleyici = kanalKirliMi   // mevcut basınç-sıçraması sinyali, §2a
değilse (BMP280 arızalı, yedek yol):
    hamPromil > UFLEME_TETIK_MQ3_ESIK_PROMIL (0.1 promil) 2 ardışık okuma
    (UFLEME_TETIK_ARDISIK_SAYI) → tetikleyici = true
```

**Önemli:** Bu mantık SADECE `analizYaziGosterildi`'yi ne zaman `true`
yapacağını (yani "analiz kısmı"na ne zaman geçileceğini) belirler. Ölçülen/
anons edilen NİHAİ peak promil değeri (`olcumPeakPromil`) bundan hiç
ETKİLENMEZ — o hâlâ tamamen ayrı, değişmemiş bir mekanizmayla hesaplanır:
MQ-3'ün kendi `hamPromil`/`promil` değeri, `UFLEME_ESIK_PROMIL` (0.05) eşiği,
`GEREKLI_ARDISIK_SAYI` (3) ardışık okuma, `ardisikSayac`/`olcumPeakPromil`
peak-hold birikimi (bkz. §5 tablosu). Yani "ne zaman analiz kısmına
geçileceği" ile "sonuçta hangi promil değerinin anons edileceği" birbirinden
tamamen bağımsız iki hesaplama.

Bu yeni tetikleyici sayacı (`uflemeTetikMq3ArdisikSayac`), mevcut
`mq3KirlilikArdisikSayac`/`mq3KirliMi` (§2c, BEKLE'nin kirlilik tespiti,
`durum != OLCUM`'da çalışır) ve `MQ3_ORTALAMA_ESIK_PROMIL`/`mq3OrtalamaPencere`
(§3, BEKLE'nin 10sn'lik ortalama kontrolü) ile **KARIŞTIRILMAMALI** — üçü de
farklı state/eşik kullanan, birbirinden bağımsız mekanizmalar.

**10sn içinde hiç üfleme algılanmazsa** (`guvenlikZamanAsimi`,
`analizYaziGosterildi` hiç `true` olmadan `OLCUM_SURESI_MS` dolarsa): "analiz
kısmı" hiç başlamaz, doğrudan sonuç bloğuna geçilir (bkz. `LED_DAVRANIS_NOTU.md`
§1 — eski ayrı `ANALIZ` durumu kaldırıldığı için bu davranış ek koda gerek
kalmadan doğal olarak sağlanıyor).

**⚠️ Düzeltilmiş bir hata (bu satırda önceden "peak=0.00" yazıyordu, YANLIŞTI):**
Peak-hold bloğu (`ardisikSayac`/`olcumPeakPromil`, `ui.ino:~1148`) eskiden
`analizYaziGosterildi`'ye bakmadan OLCUM'un HER tikinde koşulsuz çalışıyordu.
Yani "sensör bekleme" fazında (BMP280/MQ-3 tetikleyicisi hiç ateşlenmeden,
ekranda "Analiz yapiliyor" hiç görünmeden) bile MQ-3'ün ham promili
`UFLEME_ESIK_PROMIL`'i 3 ardışık okuma boyunca aşarsa `olcumPeakPromil`
sessizce gerçek değere güncelleniyordu — sonra `guvenlikZamanAsimi` tetiklenip
sonuç bloğuna geçilince bu sessizce yakalanmış gerçek değer, hiç analiz
gösterilmeden sonuç ekranına/seslerine/`olcumSayaci`'ye yansıyordu. **Düzeltildi:**
artık peak SADECE `analizYaziGosterildi == true` iken (yani analiz kısmı
gerçekten başladıktan sonra) biriktiriliyor; timeout olursa peak gerçekten
`0.00` kalır.

---

## 5) MQ-3 (alkol) ve DHT22 (ortam)

### a) Ana ölçüm + yedek kirlilik/tetikleme tespiti (MQ-3)

- `sensorRsOlcVeDondur()`: MQ3_PIN'den 20 örnek oversampling ile Rs (sensör
  direnci) ölçer.
- `hamPromilOlcVeDondur()`: `Rs/R0` oranını, DHT22'den okunan **ortamSicaklik/
  ortamNem**'e göre datasheet eğrisinden türetilmiş bir katsayıyla düzeltir,
  ardından MQ-3 datasheet "Alcohol" eğrisinden türetilmiş güç fonksiyonu
  (`mg_L = 0.402886 * ratio^-1.4941`) ile mg/L'ye, oradan `*2.1` ile promile
  çevirir. Bu `hamPromil` değeri hem asıl ölçüm hem de yedek kirlilik
  tespiti (§2c) hem de OLCUM'un yedek üfleme-tetikleme yolu (§4) için
  kullanılıyor.
- `dhtOrtamGuncelle()`: DHT22'yi 2000ms'de bir okur, sadece `ortamSicaklik`/
  `ortamNem` cache'ler — kirlilik tespiti YAPMIYOR.

### b) R0 kalibrasyonu — artık SADECE manuel

R0 kalibrasyonu **sadece** BEKLE durumunda 10 saniye kesintisiz buton basılı
tutularak (`r0KalibrasyonuBaslat()`) başlatılabiliyor; otomatik/boşta-tabanlı
bir tetikleyici YOK. Kalibrasyon artık `ekran4`'te gösteriliyor (eskiden bu
ekran hiç kullanılmıyordu) — `r0KalibrasyonuBaslat()` `ekran4`'e geçer,
`r0KalibrasyonuBitir()` (başarılı veya başarısız fark etmeksizin) `ekran1`'e
döner ve `hazirMesajiniGoster()` ile 3sn "HAZIR" gösterir.

`olcumSayaci` (tamamlanan ölçüm sayısı) her ölçüm sonunda artırılıp NVS'ye
yazılıyor. **Yeni:** `olcumSayaci >= 500` olduğunda `ekran1`'deki
`uic_arizamesajtext` widget'ı (metni hep sabit "KOD-1", hiç değişmiyor)
görünür hâle geliyor — bu **SADECE görsel bir hatırlatma**, R0 kalibrasyonunu
OTOMATİK TETİKLEMİYOR (kalibrasyon hâlâ tamamen manuel, 10sn buton basılı
tutma gerekiyor). Manuel bir R0 kalibrasyonu başarıyla biterse `olcumSayaci`
sıfırlanır (mevcut davranış, değişmedi) ve bir sonraki BEKLE tikinde
hatırlatma otomatik olarak gizlenir.

---

## 6) Ölçüm eşikleri (özet referans)

| Sabit | Değer | Anlamı |
|---|---|---|
| `UFLEME_ESIK_PROMIL` | 0.05 | Bunun üstü "gerçek üfleme" sayılır (peak-hold ölçümü için, gürültü değil) |
| `GEREKLI_ARDISIK_SAYI` | 3 | Peak-hold'un geçerli sayılması için ardışık okuma sayısı (~300ms) |
| `UFLEME_TETIK_MQ3_ESIK_PROMIL` | 0.05 (eskiden 0.1) | OLCUM'un yedek (BMP280 arızalıyken) üfleme-tetikleme eşiği — peak-hold'dan AYRI, artık `UFLEME_ESIK_PROMIL` ile aynı değer |
| `UFLEME_TETIK_ARDISIK_SAYI` | 2 | ... bu kadar ardışık okuma = "üfleme algılandı" (analiz kısmına geç) |
| `YASAL_SINIR_PROMIL` | 0.50 | LED/ses eşiği (bkz. `LED_DAVRANIS_NOTU.md` / `SES_DAVRANIS_NOTU.md` — tam 0.50'de LED ve ses birbirinden farklı davranıyor) |
| `KANAL_KIRLILIK_ESIK_HPA` | 1.0 (yer tutucu) | Nefes-BMP280 kirlilik/üfleme eşiği — **sahada kalibre edilmeli** |
| `NEFES_DEGISME_ESIK_HPA` | 0.03 | Bunun altındaki fark "değişmedi" (donma tespiti) sayılır |
| `NEFES_BASINC_GECERLI_MIN_HPA` / `_MAKS_HPA` | 800 / 1300 | Bu aralık dışındaki basınç fiziksel olarak imkansız kabul edilip anında arızalı sayılır |
| `NEFES_ARIZA_SURESI_MS` | 600000 (10 dk) | Bu süre boyunca basınç değişmezse sensör arızalı sayılır |
| `KANAL_TEMIZLEME_SURESI_MS` | **10000 (10 sn)** | Kanal temizleme fanının sabit çalışma süresi (eskiden 18000) |

MQ-3 tabanlı yedek kirlilik tespiti (`mq3KirlilikKontrol()`) ayrı bir eşik
tanımlamıyor; doğrudan `UFLEME_ESIK_PROMIL` ve `KANAL_KIRLILIK_ARDISIK_SAYI`'yı
(yukarıda) yeniden kullanıyor.

---

## 7) Dikkatini çekmek istediğim noktalar

1. **`KANAL_KIRLILIK_ESIK_HPA = 1.0f` doğrulanmadı.** BMP280 gerçek donanıma
   takılıp üfleme sırasında ürettiği basınç sıçraması Serial'den gözlenmeden bu
   değer yalnızca bir başlangıç noktası. Çok düşük kalırsa normal
   nefes almadan/ortam titreşiminden yanlış "kirli"/"üfleme algılandı"
   tetiklenir; çok yüksek kalırsa gerçek üfleme/kalıntı üfleme kaçırılır.
   Bu eşik artık hem kirlilik tespitini HEM DE OLCUM'un birincil
   üfleme-tetikleme sinyalini etkiliyor (§4), bu yüzden kalibrasyonu daha da
   önemli.
2. **BMP280'in sıcaklık kanalı şu an kullanılmıyor.** Aynı I2C sensör sıcaklık
   da ölçebiliyor ama kod sadece basıncı okuyor (`bmeNefes.readPressure()`);
   DHT22 (ortam) kompanzasyon görevini görmeye devam ediyor.
3. **I2C adresi varsayımı:** Kod önce `0x76`, olmazsa `0x77` dener (çoğu ucuz
   BMP280 modülü SDO pinine göre bu ikisinden birini kullanır).
4. **MQ-3 tabanlı yedek kirlilik/tetikleme tespiti fanla etkileşebilir.**
   `KANAL_TEMIZLE` sırasında fan MQ-3'ün üzerinden/yanından hava geçiriyor;
   bu, sensörün direncini (dolayısıyla `hamPromil`'i) geçici olarak etkileyip
   yedek yolun (nefes-BMP280 arızalıyken) yanlış bir "hâlâ kirli"/"üfleme
   algılandı" okuması vermesine yol açabilir. Nefes-BMP280 sağlıklıyken bu
   risk yok (birincil kaynak odur).
5. **R0 kalibrasyonu artık sadece manuel.** `olcumSayaci >= 500` sadece
   `ekran1`'de görsel bir hatırlatma (`uic_arizamesajtext`) gösterir,
   kalibrasyonu OTOMATİK BAŞLATMAZ — kullanıcının 10sn buton basılı tutmasını
   hatırlamasına bağlı (bkz. §5b).
