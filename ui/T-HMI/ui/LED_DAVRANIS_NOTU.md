# LED Davranış Notu — Hangi Durumda LED Nasıl Çalışıyor

Bu not, `led.cpp` + `ui.ino` içindeki **güncel** kodun ne yaptığını anlatır
(kod okunarak çıkarılmış gerçek davranış, tasarım önerisi değil).

## Donanım

- Şerit: adreslenebilir WS2812/SK6812 ailesi, **15 LED**, tek veri hattı.
- Veri pini: **GPIO33**.
- 5V hattından ayrı beslenmeli (ESP32'nin 3V3 pininden değil), GND ortak olmalı.

---

## 1) Durum makinesi → LED durumu eşlemesi

`ledDurumGuncelle()` fonksiyonu, ana 100ms'lik sensör zamanlayıcısının
(`alkolmetre_timer_cb`) **her turunda** çağrılır ve o anki `durum` değişkenine
bakıp uygun LED hedefini ayarlar. `ledDurumAyarla()` aynı hedef tekrar
verilirse hiçbir şey yapmadığı için (idempotent) bu sürekli çağrı animasyonu
yeniden başlatmaz, sadece durum gerçekten değiştiğinde geçiş tetiklenir.

| Durum makinesi durumu | LED durumu | Not |
|---|---|---|
| `BEKLE` | `LED_BEKLE` | |
| `TABAN_OLC` | `LED_BEKLE` | Taban (baseline) ölçümü sırasında henüz üfleme yok, "bekleme" rengi gösterilir |
| `OLCUM` — "sensör bekleme" alt-fazı (`analizYaziGosterildi == false`) | `LED_BEKLE` | Üfleme henüz algılanmadı |
| `OLCUM` — "analiz kısmı" alt-fazı (`analizYaziGosterildi == true`) | `LED_UFLEME` | Üfleme algılandığı an başlar, 6sn (`ANALIZ_OLCUM_SURESI_MS`) sürer |
| `SONUC` | `promil ≥ 0.50` ise `LED_SONUC_YUKSEK`, değilse `LED_SONUC_GUVENLI` | Eşik `YASAL_SINIR_PROMIL = 0.50f`, `>=` (0.50 dahil kırmızı) |
| `KANAL_TEMIZLE` | `LED_TEMIZLEME` | |
| `R0_KALIBRASYON` | `LED_ANALIZ` | Kalibrasyon için ayrı bir renk yok, analiz ile aynı cyan pulse kullanılıyor. Artık `ekran4`'te gösteriliyor (bkz. §3) |

Eskiden ayrı bir `ANALIZ` durumu vardı (OLCUM bitince 10sn'lik donuk peak
gösterimi, LED cyan pulse). Bu durum **kaldırıldı**: OLCUM'un "analiz kısmı"
(6sn, üfleme algılanınca başlar) bitince artık doğrudan `SONUC`'a geçiliyor —
peak donuk gösterimi + renk ataması OLCUM'un bitiş bloğunda yapılıyor.

---

## 2) LED durumlarının görsel tanımı

| LED durumu | Renk | Animasyon | Parlaklık | Zamanlama |
|---|---|---|---|---|
| `LED_BEKLE` | Mavi `(0,60,255)` | Nefes (yavaş parlaklık dalgalanması, tüm LED'ler aynı renk) | %15 | Periyot 3000ms |
| `LED_UFLEME` | Turuncu `(255,90,0)` | İlerleyen dolum: "analiz kısmı" başladığı an (faz=0) ilk LED hemen yanar, 6 saniyede tüm 15 LED'e ilerleyerek dolar, süre dolunca tam dolu kalır | %40 | Dolma süresi 6000ms (`UFLEME_DOLMA_SURESI_MS`, led.cpp) |
| `LED_ANALIZ` | Cyan `(0,220,255)` | Ortadan dışarı doğru yayılan dar halka (pulse) | %60 | Periyot 1200ms |
| `LED_SONUC_GUVENLI` | Yeşil `(0,255,60)` | Soldan sağa dolar, sonra sabit yanar | %80 | Dolma süresi 500ms |
| `LED_SONUC_YUKSEK` | Kırmızı `(255,15,0)` | Soldan sağa dolar, sonra nefes efektiyle yanmaya devam eder | %80 | Dolma 500ms, sonra nefes periyodu 2500ms |
| `LED_TEMIZLEME` | Mor `(160,0,255)` | Soldan sağa kayan 3 LED'lik iz (orta) | %40 | Adım 60ms |

**Geçişler:** Hedef durum değiştiğinde, önce mevcut renkten hedef renge
**300ms'lik düz bir crossfade** yapılır (tüm LED'ler aynı ara renkte, sabit
dolgu). Fade bitince hedef durumun kendi animasyonu (yukarıdaki tablo)
sıfırdan başlar. `LED_UFLEME`'nin "analiz kısmı"nın 6sn'lik penceresiyle
senkronize kalması bu crossfade'e rağmen sağlanır çünkü `ledDurumAyarla`
çağrısı `analizYaziGosterildi=true` olan AYNI tikte tetiklenir — ~300ms'lik
crossfade gecikmesi yüzünden dolum, pencerenin tam sonunda 1 LED eksik
kalıp orada tutunabilir (kozmetik, zararsız).

---

## 3) SONUC durumu hangi ekranda gösteriliyor

Ekranlar: `ekran1` (BEKLE), `ekran2` (TABAN_OLC + OLCUM + **SONUC**
— "sonuç ekranı"), `ekran11` (SADECE gerçek `KANAL_TEMIZLE`), `ekran4`
(R0 kalibrasyonu — artık gerçekten kullanılıyor, `r0KalibrasyonuBaslat()`de
gösterilir, `r0KalibrasyonuBitir()`de `ekran1`'e dönülür).

OLCUM'un "analiz kısmı" (6sn) bitip peak değer belli olduğu an ("sonuç
gösterme" anı) `uic_deger` yazı rengi ile LED (`ledDurumGuncelle`) **aynı
anda** yeşile/kırmızıya döner (`0x00FF3C` / `0xFF0F00`, LED ile aynı
`YASAL_SINIR_PROMIL` eşiği), ve doğrudan `SONUC` durumuna geçilir. **Ekran
değişmez** — `SONUC` durumunun 10 saniyelik cooldown'ı boyunca da (LED
yeşil/kırmızı yanarken) `ekran2`'de kalınır, fan arka planda çalışır ama
ekranda gösterilmez.

Cooldown bitince: kirlilik sensörüne **bakılmaksızın**, HER ölçümden sonra
`kanalTemizlemeyeGec("Kanal temizleniyor, lutfen bekleyiniz")` çağrılır —
`ekran11`'e (fan ikonu + **10sn** geri sayım + `uic_fantext`) geçilir,
`KANAL_TEMIZLE`'ye (mor LED) girilir. `KANAL_TEMIZLEME_SURESI_MS` eskiden
18sn'ydi, artık **10sn**. Geri dönüşte (`ekran1`'e) artık `hazirMesajiniGoster()`
çağrılıp 3sn'lik "HAZIR" yazısı gösteriliyor (eskiden bu dönüşte hiç
gösterilmiyordu; artık R0 kalibrasyonu dönüşüyle tutarlı).

Buton basılmadan (`BEKLE`) kendiliğinden kirlilik algılanırsa da aynı
`kanalTemizlemeyeGec()` üzerinden `ekran11`'e geçilir, mesaj artık "olcum
oncesi kirlilik algilandi lutfen temizlenene kadar bekleyiniz" olur (eskiden
"Yanlis ufleme algilandi..." idi) — bu tetikleyici `butonsuzKirlilikTespitEdildiMi()`
(BME280/BMP280 son 10sn + MQ-3 son 10sn ortalaması birlikte, bkz.
`SENSOR_DAVRANIS_NOTU.md` §3) kullanıyor. `TABAN_OLC` ve `R0_KALIBRASYON`
hâlâ eski mesajı ("Yanlis ufleme algilandi, lutfen cihaz temizlenene kadar
bekleyiniz") kullanıyor — sadece BEKLE'nin mesajı değişti. `ekran11`
**böylece SADECE `KANAL_TEMIZLE` durumunda** (mor LED) gösterilmiş olur —
`SONUC` (yeşil/kırmızı) ekrana hiç uğramaz.

---

## 4) Dikkatini çekmek istediğim bir nokta

**LED eşiği ile ses eşiği tam 0.50'de farklı davranıyor** (bkz.
`SES_DAVRANIS_NOTU.md`):

- LED → tam `0.50` promilde `promil >= 0.50` koşuluyla **kırmızı (LED_SONUC_YUKSEK)** gösterir.
- Ses → tam `0.50` promilde `durum_ticari_sinir` çalar (yasal sınırın "üstünde" değil).

Yani tam 0.50'de ekran/LED "yüksek sonuç" derken ses "ticari sınır" diyor.
Önceki notta da belirttiğim gibi bilinçli tercihinse sorun değil, ama
istersen LED eşiğini `> 0.50` yapıp sesle birebir tutarlı hâle getirebilirim.
