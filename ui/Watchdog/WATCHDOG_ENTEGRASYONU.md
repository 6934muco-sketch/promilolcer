# ESP32 Alkolmetre — Watchdog (WDT) Entegrasyonu

**Dosya:** `ui.ino`
**Mekanizma:** ESP32 donanımsal Task Watchdog Timer (TWDT)
**Amaç:** Cihaz herhangi bir nedenle kilitlenirse birkaç saniye içinde kendini otomatik resetlemesi

---

## 1) Özet

`ui.ino` içine, `setup()` ve `loop()`'un çalıştığı FreeRTOS görevini ("loopTask") izleyen bir **Task Watchdog Timer** eklendi. Kodun her yerinde — ısınma bekleme döngüsü, kalibrasyon döngüleri, ana `loop()` — düzenli olarak "hâlâ hayattayım" sinyali (`wdtBesle()`) gönderiliyor. Bu sinyal zamanında gelmezse (yani gerçek bir kilitlenme olursa) ESP32 **panic üretir ve kendini resetler**.

Toplam **9 besleme noktası** eklendi, dosyadaki her blocking (bloklayan) bekleme ve ana döngünün her turu dahil. Ayrıca her açılışta bir önceki reset'in nedeni Seri porta yazdırılıyor, böylece "gerçekten watchdog mu tetiklendi?" sorusu saha sorunlarında kolayca cevaplanabiliyor.

## 2) Neden gerekliydi

Mevcut kodda LVGL çizimi, sensör okumaları ve durum makinesi tek bir döngüde (`loop()` → `lv_timer_handler()` → `alkolmetre_timer_cb`) birbirine bağımlı çalışıyor. Ayrıca `setup()` içinde birkaç **uzun bloklayan bekleme** var:

| Bölüm | Süre | Risk |
|---|---|---|
| Buton basılı tutma kontrolü | ≤3 sn | `digitalRead` asılı kalırsa (donanım arızası) sonsuz döngü |
| MQ-3 ısınma bekleme | 60 sn | İçindeki `dhtGuncelle()` / `lv_timer_handler()` asılırsa fark edilmez |
| R0 yeniden kalibrasyon | ~5 sn | ADC okuma zinciri asılırsa fark edilmez |
| Taban (baseline) ölçümü | ~3 sn | Aynı risk |
| Kalibrasyon ekranı gösterimi | 3 sn | Düşük risk ama tutarlılık için eklendi |
| Ana `loop()` | Süresiz | SPI/TFT sürücüsü kilitlenirse (nadir donanım arızası) cihaz sonsuza kadar donar |

Watchdog olmadan bu senaryoların HERHANGİ birinde cihaz, elle güç kesilene kadar donuk kalır — bir alkolmetrede bu, cihazın sahada "çalışmıyor" görünmesi anlamına gelir.

## 3) Mimari

### 3.1 İki katmanlı zaman aşımı

```cpp
static const uint32_t WDT_TIMEOUT_KURULUM_MS = 10000; // setup() sirasinda (10 sn)
static const uint32_t WDT_TIMEOUT_CALISMA_MS = 4000;  // loop() sirasinda (4 sn)
```

- **Kurulum (10 sn):** `setup()` içindeki uzun döngülerin HER birine besleme eklendiği için asıl gereken şey, iki besleme arasındaki en kötü boşluğu (ör. `tft.begin()` + `ui_init()` toplamı) rahatça karşılamak. 10 saniye, ölçülen tipik süreler için (DHT stabilizasyon 1.5 sn + ekran/UI başlatma tahmini <1-2 sn) 3-4 kat pay bırakıyor.
- **Çalışma (4 sn):** `loop()` başladıktan sonra normal bir tur (LVGL çizim + 100ms'lik sensör timer'ı + 20 örnekli ADC okuma) tipik olarak birkaç ms ile birkaç onlarca ms arası sürer. 4 saniye bu sürenin ~100 katı kadar pay bırakırken, gerçek bir kilitlenmeyi de makul bir sürede yakalar.

`setup()`'ın sonunda, `lv_timer_create(...)` çağrılmadan hemen önce `wdtYapilandir(WDT_TIMEOUT_CALISMA_MS)` ile daha sıkı süreye geçiliyor.

### 3.2 Neden "kapatıp-açmak" yerine "her yerde besleme"

Bazı örneklerde uzun işlemler öncesi watchdog geçici olarak `esp_task_wdt_delete()` ile kapatılıp sonra tekrar açılır. Burada bilinçli olarak **bunun yerine her döngüye besleme eklendi**, çünkü:

- Isınma/kalibrasyon döngüleri zaten her adımda `dhtGuncelle()` / `lv_timer_handler()` çağırıyor — aynı noktaya `wdtBesle()` eklemek bedavaya yakın.
- Watchdog'u kapatırsanız, tam da "bilinen uzun süreç" içinde gerçek bir kilitlenme (ör. DHT kütüphanesinin beklenmedik şekilde asılması) olursa **hiçbir koruma kalmaz**. Besleyerek kapsamak, kurulum sırasında da gerçek korumayı sürdürür.

### 3.3 Çekirdek sürümü uyumluluk katmanı

Arduino-ESP32 çekirdek **3.x** (ESP-IDF 5.x) ile **2.x ve öncesi** (ESP-IDF 4.x) arasında `esp_task_wdt_init()` imzası tamamen değişti:

```cpp
// Cekirdek 3.x / IDF 5.x (struct tabanli, ms cinsinden):
esp_task_wdt_config_t cfg = { .timeout_ms = ..., .idle_core_mask = ..., .trigger_panic = true };
esp_task_wdt_init(&cfg);

// Cekirdek 2.x ve oncesi / IDF 4.x (iki parametre, SANIYE cinsinden):
esp_task_wdt_init(timeout_saniye, true);
```

`ui.ino` içindeki `wdtYapilandir()` fonksiyonu, derleme anında `ESP_ARDUINO_VERSION_MAJOR` makrosuna bakıp doğru API'yi otomatik seçiyor — hangi Arduino IDE board paketi sürümüyle derlerseniz derleyin kod değişmeden çalışır.

## 4) Değişiklik haritası

| Konum | Eklenen | Neden |
|---|---|---|
| Dosya başı (include'lar) | `esp_task_wdt.h`, `esp_system.h` | WDT ve reset-nedeni API'leri |
| Dosya başı (yeni bölüm) | `wdtYapilandir()`, `wdtBesle()`, `resetNedeniYazdir()` | Merkezi watchdog altyapısı |
| `setup()` başlangıcı | `resetNedeniYazdir()`, `wdtYapilandir(KURULUM)`, `esp_task_wdt_add(NULL)` | Watchdog'u mümkün olan en erken noktada devreye alma |
| Buton basılı-tutma döngüsü | `wdtBesle()` | ≤3 sn süren döngü |
| `ui_init()` sonrası | `wdtBesle()` | LVGL arayüz oluşturma süresi garantiye alınmadı, önlem olarak eklendi |
| Isınma döngüsü (60 sn) | `wdtBesle()` | En uzun bloklayan bölüm |
| `r0YenidenKalibreEt()` döngüsü | `wdtBesle()` | ~5 sn |
| Taban ölçüm döngüsü | `wdtBesle()` | ~3 sn |
| Kalibrasyon ekranı döngüsü | `wdtBesle()` | 3 sn |
| `setup()` sonu | `wdtYapilandir(CALISMA)`, `wdtBesle()` | Sıkı zaman aşımına geçiş |
| `loop()` | `wdtBesle()` | Kalıcı koruma — `lv_timer_handler()` asılırsa burası devreye giremez ve TWDT ateşlenir |

## 5) "Her ihtimal" — göz önüne alınan uç durumlar

**a) TWDT'nin zaten arka planda açık olması ihtimali.**
Arduino-ESP32 ve ESP-IDF'de TWDT'yi başlatan ayarlar (`CONFIG_ESP_TASK_WDT_INIT` ve idle-task izleme seçenekleri) varsayılan olarak **açıktır**. Bu yüzden `esp_task_wdt_init()` çağrımız `ESP_ERR_INVALID_STATE` dönebilir ("zaten başlatılmış"). Kod bunu yakalayıp otomatik olarak `esp_task_wdt_reconfigure()`'a düşüyor — hangi durumda olursanız olun çalışır.

**b) Varsayılan davranış "sadece uyarı"dır, RESET DEĞİL.**
Bu, en kritik bulgu: ESP-IDF'nin varsayılan TWDT davranışı, zaman aşımında sadece Seri porta bir uyarı/backtrace basıp **çalışmaya devam etmektir** — panik/reset üretmez. Gerçekten resetlenmesini istiyorsanız `trigger_panic = true` **açıkça** ayarlanmalı. `wdtYapilandir()` bunu her zaman zorluyor; varsayılana güvenilmiyor.

**c) Panik gerçekten reset mi üretiyor, yoksa sadece "donup" mu kalıyor?**
ESP-IDF'nin genel panic-handler davranışı (`CONFIG_ESP_SYSTEM_PANIC`) varsayılan olarak **"Print registers and reboot"**tur — yani panic tetiklenince kayıtları yazdırıp çipi gerçekten resetler. (Bu ayar birisi tarafından bilinçli olarak "halt" moduna alınmadıysa — genelde sadece JTAG ile debug yaparken tercih edilir.)

**d) Arduino'nun kendi `enableLoopWDT()` kısayolu neden kullanılmadı.**
Arduino-ESP32'nin dahili `main.cpp`'i, `loopTaskWDTEnabled` bayrağı `true` yapılırsa her `loop()` çağrısından önce otomatik besleme yapabiliyor. Ama bu mekanizma **sadece `loop()` çağrıları arasını** kapsıyor; `setup()` içindeki ısınma/kalibrasyon döngülerini hiç görmüyor. Bu projede en riskli bölümler tam olarak `setup()` içinde olduğu için, besleme noktalarını istediğimiz her yere (fonksiyon içi dahil) yerleştirebilen düşük seviyeli `esp_task_wdt_add()`/`esp_task_wdt_reset()` API'si tercih edildi.

**e) Eski/yeni Arduino-ESP32 API farkı.**
Bölüm 3.3'te açıklandı — `#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3` ile derleme zamanında otomatik seçiliyor.

**f) Reset anında fan / NVS durumu ne olur?**
- **Fan:** `setup()` en başında (watchdog'dan hemen sonra, herhangi bir riskli işlemden ÖNCE) `digitalWrite(FAN_PIN, LOW)` zaten çağrılıyor. Yani watchdog fanı SONUÇ ekranındayken (fan açıkken) resetlerse bile, cihaz yeniden açıldığında fan otomatik olarak kapanır. Ek bir önlem gerekmedi.
- **NVS/kalibrasyon:** `r0YenidenKalibreEt()` yeni R0 değerini NVS'ye ancak döngünün SONUNDA yazıyor. Watchdog kalibrasyon ORTASINDA tetiklenirse, NVS'deki eski (geçerli) kalibrasyon bozulmadan kalır — yarım/hatalı bir değer asla yazılmaz.
- **Yarıda kalan ölçüm:** Watchdog tam nefes ölçümü (`OLCUM` durumu) sırasında tetiklenirse o ölçüm kaybolur, cihaz `BEKLE` durumunda açılır. Kullanıcı testi tekrarlar — cihazın süresiz donmasından çok daha iyi bir sonuç.

**g) Debug (JTAG/OpenOCD) sırasında "watchdog çalışmıyor" gibi görünmesi.**
OpenOCD ile debug yaparken her breakpoint'te donanımsal watchdog sayaçları otomatik durdurulur ve breakpoint'ten çıkışta tekrar başlatılmaz. Yani debugger bağlıyken watchdog'un tetiklenmediğini görürseniz bu bir hata değil, beklenen davranıştır.

**h) Interrupt Watchdog (IWDT) ve brownout dedektörü.**
Bu sketch `attachInterrupt()` kullanmadığı (buton polling ile okunuyor) için ISR kilitlenmesi riski düşük, ama ESP-IDF'nin **Interrupt Watchdog**'u zaten varsayılan olarak açık ve ayrı bir donanım katmanı olarak (scheduler'ı kilitleyen ISR'lara karşı) çalışıyor — bu entegrasyonla ilgisi yok, ek kod gerekmiyor. Aynı şekilde brownout (güç/voltaj düşüşü) dedektörü de ayrı ve varsayılan olarak aktif bir koruma katmanı.

**i) Tek çekirdekli varyantlar.**
`idle_core_mask = (1 << 0) | (1 << 1)` her iki çekirdeği de izliyor. Tek çekirdekli bir ESP32 varyantı kullanıyorsanız bunu `(1 << 0)` olarak tek çekirdeğe indirebilirsiniz (bu projede kullanılan 480x320 TFT_eSPI + LVGL kurulumu tipik olarak standart çift çekirdekli ESP32 kartlarında çalışır).

**j) Reset nedeni doğrulaması.**
`resetNedeniYazdir()` her açılışta Seri porta `ESP_RST_TASK_WDT`, `ESP_RST_BROWNOUT`, `ESP_RST_POWERON` vb. yazdırıyor. Sahada "cihaz kendi kendine resetleniyor" şikayeti gelirse, bu log watchdog mı yoksa güç kaynağı mı suçlu ayrımını hemen yapmanızı sağlar.

## 6) Zaman aşımı değerlerini ayarlamak

Değerler dosyanın en üstünde tek yerden kontrol ediliyor:

```cpp
static const uint32_t WDT_TIMEOUT_KURULUM_MS = 10000;
static const uint32_t WDT_TIMEOUT_CALISMA_MS = 4000;
```

Eğer `ui_init()` çok sayıda görsel/font içeriyorsa ve gerçekte 10 saniyeye yakın sürdüğünü fark ederseniz `WDT_TIMEOUT_KURULUM_MS` değerini artırın. `WDT_TIMEOUT_CALISMA_MS` değerini daha da sıkılaştırmak isterseniz (ör. 2 saniyeye), önce Seri portta bir turun gerçekte ne kadar sürdüğünü ölçüp (`micros()` ile `loop()` başı/sonu farkı alarak) rahat bir pay bırakacak şekilde ayarlamanız önerilir.

## 7) Nasıl test edilir

Watchdog'un gerçekten çalıştığını doğrulamak için `loop()` içine **geçici** bir test satırı ekleyip yükleyin, davranışı gözlemleyin, sonra satırı kaldırın:

```cpp
void loop() {
  uint32_t next_ms = lv_timer_handler();
  wdtBesle();
  delay(next_ms < 1 ? 1 : next_ms);

  // --- SADECE TEST İÇİN, SONRA SİLİN ---
  static uint32_t testBaslangic = millis();
  if (millis() - testBaslangic > 15000) { while (1) {} } // kasıtlı kilitlenme
}
```

Seri Port Monitörü açıkken bu kodla yükleyin: 15. saniyede cihaz donar, ~4 saniye içinde (`WDT_TIMEOUT_CALISMA_MS`) kendini resetlemeli ve açılışta `resetNedeniYazdir()` çıktısında `"Task Watchdog (TWDT) tetiklendi -> SISTEM KILITLENMISTI"` satırını görmelisiniz. Testi doğruladıktan sonra bu bloğu mutlaka kaldırın.

## 8) Sınırlamalar — bu neyi KAPSAMAZ

- **Yanlış sensör okumaları:** Watchdog yalnızca kod "asılı kalırsa" devreye girer; MQ-3'ün yanlış kalibre edilmiş olması veya DHT11'in hatalı ama NaN-olmayan bir değer döndürmesi gibi durumları yakalamaz (bunlar ayrı bir "sensör sağlık kontrolü" konusu, önceki mesajda önerilmişti).
- **Güç kaynağı sorunları:** Brownout dedektörü ayrı bir katman; sürekli düşük/gürültülü voltaj kaynaklı tekrarlayan resetleri WDT çözmez, güç kaynağının kendisinin incelenmesi gerekir.
- **Bellek sızıntıları:** Uzun çalışma sürelerinde yavaş bir heap sızıntısı, günler sonra çökmeye yol açabilir; watchdog bu çökmeden SONRA resetler ama sızıntının kendisini gidermez.
- **Gerçek zamanlı donanım testi yapılmadı:** Bu değişiklikler bu ortamda derlenemedi (TFT_eSPI/LVGL/DHT kütüphaneleri ve ESP32 araç zinciri burada mevcut değil). Kartınıza yüklemeden önce Arduino IDE'de derleyip Bölüm 7'deki testi mutlaka yapın.

## 9) Öneri: DEGISIKLIK_GUNLUGU.md için taslak satır

Dosya başındaki yorum bir değişiklik günlüğüne işaret ediyor. Dilerseniz oraya şunu ekleyebilirsiniz:

> **[Tarih]** — Task Watchdog Timer (TWDT) eklendi: `setup()` ve `loop()` artık `esp_task_wdt` ile izleniyor; kilitlenme durumunda cihaz ~4 saniye içinde kendini resetliyor. Detaylar: `WATCHDOG_ENTEGRASYONU.md`.

## 10) Kaynaklar

- Espressif, *Watchdogs — ESP-IDF Programming Guide*: docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html
- Espressif, *Fatal Errors — ESP-IDF Programming Guide* (panic handler davranışı): docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/fatal-errors.html
- `arduino-esp32/cores/esp32/main.cpp` (loopTask ve `loopTaskWDTEnabled` kaynağı): github.com/espressif/arduino-esp32
