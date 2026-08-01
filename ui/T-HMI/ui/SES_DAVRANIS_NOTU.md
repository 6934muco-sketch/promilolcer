# Ses Davranış Notu — Hangi Durumda Hangi Ses Çalıyor

Bu not, `ui.ino` + `ses.cpp` içindeki **güncel** kodun ne yaptığını anlatır
(kod okunarak çıkarılmış gerçek davranış, geçmiş sürümler gösterilmiyor).

---

## 1) "01" klasörü — genel yönlendirme cümleleri

| Track | Metin | Ne zaman çalıyor |
|---|---|---|
| **001** | "Cihaz hazır." | (a) R0 kalibrasyonu başarıyla bitince → BEKLE'ye dönerken. (b) `KANAL_TEMIZLE`'nin sabit **10 saniyesi** dolunca (her ölçüm sonrası VE kendiliğinden algılanan kirlilik sonrası) BEKLE'ye dönerken. |
| **002** | "Üfleme yapabilirsiniz." | Taban (baseline) ölçümü tamamlanınca (15 örnek ~1.5sn), OLCUM durumuna ("sensör bekleme" alt-fazına) geçerken. |
| **022** | "Cihaz temizleniyor." | Gerçek `KANAL_TEMIZLE` durumuna (`kanalTemizlemeyeGec()` üzerinden) geçilirken — 4 farklı yerde tetiklenebilir: BEKLE'de kendiliğinden kirlilik algılanması (`butonsuzKirlilikTespitEdildiMi()`), TABAN_OLC sırasında kirlilik algılanması, SONUC cooldown'ı bitince **koşulsuz** (her ölçümden sonra), R0 kalibrasyonu purge aşamasında kanal hâlâ kirliyse. Bu 4 durumda da ekran `ekran11`'e geçer, `uic_fantext` tetikleyiciye göre farklı mesaj gösterir (bkz. `LED_DAVRANIS_NOTU.md` §3). |
| **026** | "Analiz yapılıyor." | OLCUM'un "sensör bekleme" alt-fazında üfleme ilk kez algılanınca — bir kez. Tetikleyici artık nefes sensörü (BMP280) sağlıklıysa onun basınç-sıçraması sinyali (`kanalKirliMi`), sensör arızalıysa MQ-3'ün kendi ham okumasına dayalı yedek eşik/sayaç (bkz. `SENSOR_DAVRANIS_NOTU.md`). Anın kendisi (üfleme algılandığı ilk tik) değişmedi, sadece HANGİ sinyalin bu kararı verdiği değişti. |
| **027** | "Üflemeye devam edin." | 026'dan sonra, "analiz kısmı"nın 6 saniyelik penceresinin **tam ortasında** — bir kez. Zamanlama değişmedi. |
| **029** | "R0 kalibrasyonu yapılıyor, lütfen üflemeyin." | R0 kalibrasyonu (**sadece** manuel: BEKLE'de 10sn kesintisiz buton basılı tutma) başlarken. Otomatik/500-ölçümde-bir tetikleyici YOK — `olcumSayaci >= 500` olduğunda artık sadece `ekran1`'deki `uic_arizamesajtext` widget'ı (metni sabit "KOD-1") görünür olur, bu SADECE görsel bir hatırlatmadır, `sesCal(29)` çağrısını TETİKLEMEZ; kalibrasyon yine kullanıcının 10sn buton basılı tutmasını bekler. |
| **031** | ~~"Cihaz temizliği yapılıyor."~~ | **Artık hiçbir yerde çalınmıyor** (kod içinde `sesCal(31)` çağrısı kalmadı). `SONUC` ekran11'e hiç geçmediği (ekran2'de kalıyor) için bu anonsun yeri kalmadı. |

---

## 2) "02" klasörü — ölçüm sonucu anonsu

`output/` klasöründeki sayı dosyaları içeriğinin kendisi zaten "Promil değeriniz
X'tir" şeklinde tam cümle olduğu için, ayrıca bir giriş cümlesi çalınmıyor —
doğrudan sayı, ardından durum anonsu çalınıyor.

### a) `sesPromilSayiCal(promil)` — sayı okunuşu

- **`promil ≤ 0.50`** → `02/001.mp3`…`02/051.mp3` çalar (dosya no = `round(promil*100)+1`; örn. 0.35 → `036.mp3` → "0.35" okunuşu).
- **`promil > 0.50`** → **hiçbir şey çalmaz** (0.50 üstü için sayı dosyası yok, atlanır).

### b) `sesPromilDurumCal(promil)` — durum anonsu

| Aralık | Çalan dosya | İçerik (KULLANIM_REHBERI.md'ye göre) |
|---|---|---|
| `promil < 0.25` | `02/052.mp3` (durum_normal) | "Teşekkürler, iyi yolculuklar dileriz." |
| `0.25 ≤ promil ≤ 0.50` | `02/053.mp3` (durum_ticari_sinir) | "Ticari araç kullanım sınırının üzerindesiniz." |
| `promil > 0.50` | `02/054.mp3` (durum_yasal_sinir) | "Promil değeriniz yasal sınırların üstündedir." |

**Tam 0.50 örneği:** sayı çalınır (051.mp3 → "0.50") **+** durum_ticari_sinir
çalınır — art arda iki dosya.

**0.72 gibi bir değer örneği:** sayı **çalınmaz**, sadece durum_yasal_sinir çalınır.

---

## 3) Tam akış — ölçümden sonucun bitişine kadar

```
TABAN_OLC biter → OLCUM'a geçilir ("sensör bekleme" alt-fazı, LED mavi)
  → 002 "Üfleme yapabilirsiniz"

  ... üfleme algılanana kadar bekleniyor (en fazla 10sn, OLCUM_SURESI_MS) ...

Üfleme algılanır (BMP280 basınç sıçraması VEYA BMP280 arızalıysa MQ-3 yedek eşiği)
  → "analiz kısmı" başlar (LED turuncu, ilerleyen dolum), 026 "Analiz yapılıyor"
  → 6sn'lik pencerenin ortasında (3sn) 027 "Üflemeye devam edin"

  ... 6 saniye (ANALIZ_OLCUM_SURESI_MS) canlı okuma ekranda akar ...

"analiz kısmı" biter, peak değer + "ÖLÇÜM SONUCU" başlığı ekrana yazılır
  → 02/xxx   (sayı, sadece ≤0.50 ise)
  → 02/052 | 053 | 054   (durum anonsu)
  (fan aynı anda başlar, ekran DEĞİŞMEZ — hâlâ ekran2)
  → uic_deger rengi + LED AYNI ANDA yeşile/kırmızıya döner, SONUC durumuna
    doğrudan geçilir (eski ayrı 10sn'lik ANALIZ donuk-gösterim durumu artık
    yok — kaldırıldı, bkz. LED_DAVRANIS_NOTU.md §1)

  ... 10 saniye ekran2'de, yeşil/kırmızı LED ile (SONUC cooldown'ı) ...

SONUC cooldown'ı bitince (kirlilik sensörüne BAKILMAKSIZIN, HER ölçümden
sonra): 022 "Cihaz temizleniyor" çalar, ekran11'e (fan + 10sn geri sayım —
eskiden 18sn) geçilir, KANAL_TEMIZLE durumuna girilir (mor LED). 10sn
sonunda koşulsuz ekran1'e (BEKLE) dönülür, 3sn "HAZIR" gösterilir, 001
"Cihaz hazır" çalar.
```

Yani sonuç anonsu (sayı+durum), sonuç ekrana **ilk yazıldığı anda** ("analiz
kısmı" bitişinde) çalınıyor — bu zamanlama değişmedi. `ekran11` SADECE
`KANAL_TEMIZLE` durumunda gösteriliyor — `SONUC` (yeşil/kırmızı) ekran2'de
kalıyor. Peak-lock'tan itibaren toplam bekleme süresi artık **10 saniye**
(eskiden ayrı 10sn ANALIZ + 10sn SONUC = 20sn'ydi, ANALIZ kaldırıldığı için
kısaldı).

Kuyruk FIFO olduğu için art arda kuyruğa giren öğeler her zaman sırayla çalınır
(kuyruk 15 öğeye kadar tutabilir, aralarına başka bir ses araya giremez).

---

## 4) Dikkatini çekmek istediğim bir nokta

**LED renk eşiği ile ses eşiği tam 0.50'de farklı davranıyor:**

- LED (`ledDurumAyarla`, `ui.ino` içinde `YASAL_SINIR_PROMIL = 0.50f` ile
  `promil >= 0.50` koşulu kullanıyor) → tam 0.50'de **kırmızı (LED_SONUC_YUKSEK)**
  gösterir.
- Ses (`sesPromilDurumCal`) → tam 0.50'de **durum_ticari_sinir** çalıyor,
  "yasal sınırın üstündesiniz" demiyor.

Yani tam 0.50 promilde ekran/LED "yüksek sonuç" (kırmızı) diyor ama ses
"ticari sınır" (yasal sınırın altında ama ticari sürücü için sorunlu) anonsu
veriyor — bilinçli tercihinse sorun değil, ama tutarsız görünebilir diye
belirtmek istedim. İstersen LED eşiğini de `> 0.50` yapabilirim.
