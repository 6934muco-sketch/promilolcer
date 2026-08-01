# Serial Log Haritası — Debug Çıktılarına Hızlı Erişim

Bu dosya, projedeki **tüm** `Serial.print`/`println`/`printf` satırlarının bir
haritasıdır. Bir sorunla karşılaştığında ilgili bölümü bulup, satır linkine
tıklayıp doğrudan koda gidebilir, o satırı (veya bir grubu) yorum satırına
alıp/çıkarıp ya da başındaki `#if ... 1` bayrağını `0` yapıp
etkinleştirip/kapatabilirsin.

**⚠️ Bu dosya otomatik güncel tutulur.** `ui.ino` / `ses.cpp` / `led.cpp`
içindeki `Serial.*` çağrıları her değiştiğinde (eklenir/silinir/satır numarası
kayar), bu dosya ayrıca hatırlatılmasına gerek kalmadan güncellenir — elle
senkronize etmen gerekmiyor.

---

## Nasıl kullanılır

- **Tek satırı kapatmak/açmak:** Başına `//` ekle/çıkar.
- **Bir grubu toplu kapatmak/açmak:** Zaten `#if BAYRAK_ADI ... #endif` ile
  sarılmış bloklarda (aşağıda "Bayrak" sütununda belirtilir) sadece
  `#define BAYRAK_ADI 1` satırındaki `1`'i `0` yap — tüm blok tek satırda
  kapanır, tek tek yorum satırına almana gerek kalmaz.
- Henüz bayrağa sarılmamış ama sık açıp kapatman gereken bir grup varsa,
  söyle — aynı `#if .../#endif` deseniyle sarabilirim.

---

## 1) Sistem / Açılış (watchdog, reset, LVGL)

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:65](ui.ino#L65) | "UYARI: Watchdog yapilandirilamadi" (core 0) | — |
| [ui.ino:76](ui.ino#L76) | "UYARI: Watchdog yapilandirilamadi" (core 1) | — |
| [ui.ino:106](ui.ino#L106) | "Reset nedeni: %s" (açılışta önceki reset sebebi) | — |
| [ui.ino:407](ui.ino#L407) | LVGL'in kendi iç log çıktısı (`my_print`) | `LV_USE_LOG` (lv_conf.h, kütüphane seviyesinde) |
| [ui.ino:1550](ui.ino#L1550) | "Setup done" — **zaten yorum satırında (pasif)** | — |

## 2) BMP280 (nefes/basınç) — kanal kirlilik/üfleme tespiti

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:130](ui.ino#L130) | **Ana bayrak tanımı** — `NEFES_BASINC_SERIAL_LOG` | — |
| [ui.ino:613](ui.ino#L613) | "Nefes-BMP280 uzun suredir degismiyor - sensor arizali" (donma tespiti) | — |
| [ui.ino:615](ui.ino#L615) | "Nefes-BMP280 tekrar degisken okumalar veriyor" (arızadan dönüş) | — |
| [ui.ino:656-667](ui.ino#L656) | `[NEFES-BMP280] basinc:... ortalama:... fark:... kirli:... arizali:...` — 300ms'de bir canlı basınç logu | `NEFES_BASINC_SERIAL_LOG` (satır 130'da 1↔0) |
| [ui.ino:669](ui.ino#L669) | "BMP280 nefes okuma hatasi (NaN)" | — |
| [ui.ino:675](ui.ino#L675) | "BMP280 nefes okumasi fiziksel olarak imkansiz" (800-1300 hPa dışı) | — |
| [ui.ino:1465](ui.ino#L1465) | setup(): "Nefes-BMP280 bulundu ve baslatildi" (I2C adresi + sensorID) | — |
| [ui.ino:1468](ui.ino#L1468) | setup(): "HATA: Nefes-BMP280 I2C'de bulunamadi" | — |

## 3) DHT22 (ortam sıcaklık/nem)

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:529](ui.ino#L529) | "DHT22 ortam okuma hatasi - son gecerli deger korunuyor" | — |
| [ui.ino:536](ui.ino#L536) | `[DHT22-ORTAM] pin:... sicaklik:... nem:...` canlı log | — |

## 4) MQ-3 / Promil (ana ölçüm) — şu an pasif

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:997](ui.ino#L997) | `ham:... taban:... promil:...` — **zaten yorum satırında (pasif)**, açmak için başındaki `//`'yi sil | — |

## 5) Fan

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:761](ui.ino#L761) | "Fan acildi" | — |
| [ui.ino:767](ui.ino#L767) | "Fan kapandi" | — |

## 6) R0 Kalibrasyonu

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ui.ino:750](ui.ino#L750) | setup(): "Yuklenen kalibrasyon -> R0:... Referans:... OlcumSayaci:..." | — |
| [ui.ino:947](ui.ino#L947) | "R0 kalibrasyonu basarili -> yeni R0:..." | — |
| [ui.ino:950](ui.ino#L950) | "R0 kalibrasyonu iptal/basarisiz - eski R0 korunuyor" | — |
| [ui.ino:1312](ui.ino#L1312) | "R0 kalibrasyonu zaman asimina ugradi" | — |
| [ui.ino:1360](ui.ino#L1360) | "R0 kalibrasyonu iptal: ufleme tespit edildi, kanal temizlemeye geciliyor" | — |

## 7) Ses / DFPlayer (`ses.cpp`)

| Satır | Ne basıyor | Bayrak |
|---|---|---|
| [ses.cpp:43-47](ses.cpp#L43) | `DFPlayer -> playFolder(klasor, dosyaNo)` — her ses çalma komutu | — |
| [ses.cpp:55](ses.cpp#L55) | "kuyrugaEkle: DFPlayer hazir degil, komut yok sayildi" | — |
| [ses.cpp:61](ses.cpp#L61) | "kuyrugaEkle: kuyruk dolu, komut yok sayildi" | — |
| [ses.cpp:79](ses.cpp#L79) | setup(): "DFPlayer Mini hazir" | — |
| [ses.cpp:87-88](ses.cpp#L87) | "SD karttaki toplam mp3 sayisi: ..." (çoğu klonda güvenilmez, bkz. not) | — |
| [ses.cpp:92-93](ses.cpp#L92) | `"02" klasorundeki dosya sayisi (54 olmali): ...` | — |
| [ses.cpp:95](ses.cpp#L95) | "DFPlayer Mini baslatilamadi - baglantiyi/microSD karti kontrol edin" | — |
| [ses.cpp:96-97](ses.cpp#L96) | "Son hata kodu: ..." | — |
| [ses.cpp:154-156](ses.cpp#L154) | "DFPlayer: calma bitti (dosya ...)" | — |
| [ses.cpp:160](ses.cpp#L160) | "DFPlayer: kart takildi" | — |
| [ses.cpp:163](ses.cpp#L163) | "DFPlayer: kart cikarildi!" | — |
| [ses.cpp:166](ses.cpp#L166) | "DFPlayer: kart online" | — |
| [ses.cpp:169-172](ses.cpp#L169) | `DFPlayer HATA: ... - <sesHataAdi>` (Busy/FileIndexOut/vb.) | — |
| [ses.cpp:183-184](ses.cpp#L183) | "DFPlayer: bilinmeyen olay tipi ..." | — |

## 8) `led.cpp`

Şu an `led.cpp` içinde hiç `Serial.*` çağrısı yok.

---

## Sık kullanılan senaryolar

- **BMP280/kanal kirlilik sorunu mu araştırıyorsun?** → Bölüm 2, özellikle
  `NEFES_BASINC_SERIAL_LOG` bayrağını aç (satır 130).
- **Ses/DFPlayer sorunu mu?** → Bölüm 7, hepsi zaten koşulsuz basılıyor.
- **Ölçüm/promil hesaplaması mı şüpheli?** → Bölüm 4, satır 997'yi aç.
- **Cihaz açılışta mı takılıyor?** → Bölüm 1 + Bölüm 6 (setup() logları).
