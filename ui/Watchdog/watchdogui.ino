// Arduino + LVGL 9.3 + TFT_eSPI
// Değişiklik Günlüğü: ./DEGISIKLIK_GUNLUGU.md

#include <TFT_eSPI.h>
#include <lvgl.h>
#include <ui.h>
#include <DHT.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

#ifndef LV_COLOR_DEPTH
#error "LV_COLOR_DEPTH is not defined!"
#endif

#if LV_COLOR_DEPTH != 16
#error "This driver only supports LV_COLOR_DEPTH 16 (RGB565)"
#endif

/* -------------------------------------------------------------------------- */
/*  Watchdog (WDT) - sistem kilitlenirse otomatik reset                       */
/* -------------------------------------------------------------------------- */
// ESP32'nin donanımsal Task Watchdog Timer'ı (TWDT) kullanılıyor.
// setup() ve loop() Arduino çatısında aynı FreeRTOS görevi ("loopTask") içinde
// çalıştığı için TEK bir görevi izlemek, dosyanın tamamını (setup + loop)
// kapsamaya yeter.
//
// İki farklı zaman aşımı kullanılıyor:
//   - WDT_TIMEOUT_KURULUM_MS : setup() boyunca. Isınma (60 sn) ve kalibrasyon
//     (5 sn / 3 sn) gibi BİLİNEN uzun bekleme adımları var; bu döngülerin
//     HER birinin içine besleme (wdtBesle) eklendiğinden asıl gereken şey,
//     iki besleme arası en kötü boşluğu (ekran/sensör başlatma gibi tek
//     seferlik adımlar) rahatça karşılayacak bir paydır.
//   - WDT_TIMEOUT_CALISMA_MS : loop() başladıktan sonra. Normal bir tur
//     (LVGL çizimi + sensör okuma) birkaç onlarca ms sürer; kısa tutulursa
//     gerçek bir kilitlenme çok daha hızlı fark edilip cihaz kendini toparlar.
//
// NOT (sürüm farkı): Arduino-ESP32 çekirdek 3.x (ESP-IDF 5.x) API'si struct
// tabanlıyken, 2.x ve öncesi (ESP-IDF 4.x) API'si (saniye, panic) şeklinde
// iki ayrı parametre alıyor. Aşağıdaki #if bloğu ikisini de destekler; hangi
// çekirdekle derlendiğine kod otomatik karar verir.
static const uint32_t WDT_TIMEOUT_KURULUM_MS = 10000; // setup() sirasinda (10 sn)
static const uint32_t WDT_TIMEOUT_CALISMA_MS = 4000;  // loop() sirasinda (4 sn)

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
// --- Arduino-ESP32 cekirdek 3.x / ESP-IDF 5.x: struct tabanli API ---
static bool wdtYapilandir(uint32_t timeout_ms) {
  esp_task_wdt_config_t cfg = {
    .timeout_ms = timeout_ms,
    .idle_core_mask = (1 << 0) | (1 << 1), // her iki cekirdegin idle task'i da izlensin
    .trigger_panic = true                   // ONEMLI: bu olmadan zaman asimi sadece uyari basar, RESET ATMAZ
  };
  esp_err_t err = esp_task_wdt_init(&cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    // TWDT, framework/varsayilan yapilandirma tarafindan zaten baslatilmis
    // olabilir (arduino-esp32'de TWDT varsayilan olarak aciktir). Bu durumda
    // init yerine mevcut olani kendi ayarlarimizla yeniden yapilandiriyoruz.
    err = esp_task_wdt_reconfigure(&cfg);
  }
  if (err != ESP_OK) {
    Serial.printf("UYARI: Watchdog yapilandirilamadi (hata kodu: %d)\n", (int)err);
    return false;
  }
  return true;
}
#else
// --- Arduino-ESP32 cekirdek 2.x ve oncesi / ESP-IDF 4.x: (saniye, panic) API'si ---
static bool wdtYapilandir(uint32_t timeout_ms) {
  uint32_t timeout_s = (timeout_ms + 999) / 1000; // yukari yuvarla: istenenden kisa olmasin
  esp_err_t err = esp_task_wdt_init(timeout_s, true); // true = panic + reset
  if (err != ESP_OK) {
    Serial.printf("UYARI: Watchdog yapilandirilamadi (hata kodu: %d)\n", (int)err);
    return false;
  }
  return true;
}
#endif

// Her cagrildiginda TWDT'ye "hala hayattayim, kilitlenmedim" sinyali gonderir.
// Bu cagri zamaninda gelmezse (kilitlenme durumu) TWDT panic + reset uretir.
static inline void wdtBesle() {
  esp_task_wdt_reset();
}

// Onceki (bir onceki acilistaki) reset'in nedenini Seri porta yazdirir.
// Boylece bir sonraki acilista "gercekten watchdog mu tetiklendi, yoksa
// normal bir guc kesintisi/manuel reset mi oldu" ayirt edilebilir.
static void resetNedeniYazdir() {
  esp_reset_reason_t neden = esp_reset_reason();
  const char* aciklama;
  switch (neden) {
    case ESP_RST_POWERON:   aciklama = "guc verildi (power-on / ilk acilis)";                 break;
    case ESP_RST_SW:        aciklama = "yazilimsal reset (esp_restart)";                       break;
    case ESP_RST_PANIC:     aciklama = "PANIC / exception";                                    break;
    case ESP_RST_INT_WDT:   aciklama = "Interrupt Watchdog (IWDT) tetiklendi";                 break;
    case ESP_RST_TASK_WDT:  aciklama = "Task Watchdog (TWDT) tetiklendi -> SISTEM KILITLENMISTI"; break;
    case ESP_RST_WDT:       aciklama = "baska bir watchdog (or. RTC WDT) tetiklendi";           break;
    case ESP_RST_BROWNOUT:  aciklama = "brownout (guc/voltaj dususu)";                          break;
    case ESP_RST_DEEPSLEEP: aciklama = "derin uykudan cikis";                                   break;
    default:                aciklama = "bilinmiyor/diger";                                      break;
  }
  Serial.printf("Reset nedeni: %s\n", aciklama);
}

//alkolmetre
const int MQ3_PIN = 34; 
const float VOLTAJ_BOLUCU_ORANI = 1.6666; // (R1+R2)/R2 => (10k+15k)/15k
const float RL = 200.0; // MQ-3 modülü üzerindeki yük direnci (200 kOhm)
float R0 = 2531; // Temiz havadaki direnç 

// --- Sıcaklık/Nem sensörü (DHT11) ---
// NOT: GPIO4, ekranın TFT_RST pini ile çakıştığı için kullanılamaz.
// Kullanılan diğer pinler: 34 (MQ3), 27 (buton), 19/23/18/15/2/4 (TFT).
// Bunlardan uzak, giriş-only olmayan ve strapping pini olmayan GPIO25 seçildi.
#define DHT_PIN 25
#define DHT_TIPI DHT11
DHT dht(DHT_PIN, DHT_TIPI);

// R0 kalibrasyonunun yapıldığı ortam koşulları.
// Bu değerler artık SABİT değil: NVS'de (kalıcı flash hafıza) saklanıyor ve
// açılışta oradan yükleniyor. Aşağıdaki sayılar sadece NVS boşsa (ilk açılış
// veya fabrika sıfırlama) kullanılacak VARSAYILAN değerlerdir.
// Son gerçek kalibrasyon: Sicaklik: 30.3 C | Nem: %50.0
float KALIBRASYON_SICAKLIK_REF = 30.3f; // °C
float KALIBRASYON_NEM_REF      = 50.0f; // %RH

// --- Kalıcı depolama (NVS) ---
Preferences prefs;
static const char* NVS_NAMESPACE = "alkolmetre";

// Açılışta buton bu süre boyunca basılı tutulursa yeniden kalibrasyon moduna girilir.
static const uint32_t BUTON_KALIBRASYON_SURESI_MS = 3000;

// Yeniden kalibrasyon yapıldıysa "KALİBRASYON TAMAMLANDI" ekranının (ui_ekran3)
// gösterileceği süre.
static const uint32_t KALIBRASYON_EKRANI_SURESI_MS = 3000;

// MQ-3'ün ısıtıcı elemanının kararlı sıcaklığa ulaşması için gereken bekleme.
// ESP32'nin ne kadar süre kapalı kaldığını bilmesinin (RTC/pil yedeği yok)
// bir yolu olmadığından, HER açılışta (soğuk başlangıç varsayılarak) sabit
// bu süre kadar beklenir — hem normal açılışta hem de yeniden kalibrasyonda.
static const uint32_t ISINMA_SURESI_MS = 60000; // 60 saniye

// DHT11 yavaş bir sensördür (saniyede birden fazla okunamaz), bu yüzden
// cache'lenip belirli aralıklarla güncellenir.
static const uint32_t DHT_OKUMA_ARALIGI_MS = 2000;
uint32_t sonDhtOkumaZamani = 0;
float ortamSicaklik = KALIBRASYON_SICAKLIK_REF; // ilk okumadan önceki varsayılan
float ortamNem      = KALIBRASYON_NEM_REF;

// --- Havalandırma fanı (ölçüm sonrası aktif temizleme) ---
// NOT: Fan doğrudan GPIO'dan beslenemez; bir NPN transistör/MOSFET veya röle
// modülü üzerinden sürülmeli (flyback diyot eklemeyi unutma).
// Fan, SONUC durumunun (cooldown) TAMAMI boyunca çalışır; süre ayrıca
// tutulmuyor, COOLDOWN_MS ile senkron başlayıp bitiyor.
#define FAN_PIN 26
bool fanAktif = false;

//button + debounce
static const int BUTON_PIN = 27;
static const uint32_t DEBOUNCE_MS = 50; // debounce süresi (ms)

bool butonStabilDurum = HIGH;   // debounce sonrası kabul edilen (stabil) durum
bool butonSonOkuma = HIGH;      // bir önceki ham okuma
uint32_t butonSonDegisimZamani = 0;

bool olcumBasladi = false;        // debounce edilmiş buton durumu
bool oncekiButonDurumu = false;   // edge detection için önceki durum

// Taban (baseline) değeri — açılışta temiz havada otomatik ölçülüp kaydedilir
float tabanPromil = 0.0f;

// Ölçüm parametreleri
static const uint32_t OLCUM_SURESI_MS       = 10000; // 10 sn toplam ölçüm penceresi
static const uint32_t COOLDOWN_MS            = 30000; // ölçümler arası bekleme süresi
static const float    UFLEME_ESIK_PROMIL     = 0.05f; // bu değerin üstü = üfleme algılandı
                                                       // NOT: artık taban çıkarıldığı için 0.05
                                                       // makul bir başlangıç; test sonrası ayarlanacak
uint32_t olcumBaslangicZamani = 0;

// Peak-hold ölçüm parametreleri
static const int GEREKLI_ARDISIK_SAYI = 3; // eşik üstü kaç ardışık okuma (300ms) geçerli üfleme sayılır
float olcumPeakPromil = 0.0f;
int ardisikSayac = 0;

enum Durum
{
    BEKLE,
    OLCUM,
    SONUC
};

Durum durum = BEKLE;

unsigned long sonucBaslangic = 0;

TFT_eSPI tft = TFT_eSPI();

static const uint16_t screenWidth = 480;
static const uint16_t screenHeight = 320;

enum BoardConstants { LVGL_BUFFER_RATIO = 10 };
enum { SCREENBUFFER_SIZE_PIXELS = screenWidth * screenHeight / BoardConstants::LVGL_BUFFER_RATIO };

static uint16_t buf[SCREENBUFFER_SIZE_PIXELS] __attribute__((aligned(4)));

/* -------------------------------------------------------------------------- */
/*  LVGL logging (optional)                                                    */
/* -------------------------------------------------------------------------- */

#if LV_USE_LOG != 0
void my_print(const char *buf) {
  Serial.printf(buf);
  Serial.flush();
}
#endif

/* -------------------------------------------------------------------------- */
/*  Display flush callback                                                     */
/* -------------------------------------------------------------------------- */

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixelmap) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)pixelmap, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

/* -------------------------------------------------------------------------- */
/*  Touchpad callback                                                          */
/* -------------------------------------------------------------------------- */

void my_touchpad_read(lv_indev_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touchX = 0, touchY = 0;

  bool touched = false;  // tft.getTouch(&touchX, &touchY, 600);

  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

/* -------------------------------------------------------------------------- */
/*  LVGL tick callback                                                         */
/* -------------------------------------------------------------------------- */

static uint32_t my_tick_get_cb(void) { return millis(); }

/* -------------------------------------------------------------------------- */
/*  Sıcaklık/Nem Kompanzasyonu (MQ-3 datasheet Fig.2'den dijitalleştirildi)    */
/* -------------------------------------------------------------------------- */
// Grafik, farklı sıcaklık/nem koşullarında sensörün Rs/R0 oranının referans
// koşula göre nasıl saptığını gösterir. Üç nem eğrisi (30/60/85% RH) ve her
// eğri üzerinde -10°C..50°C arası 5'er derecelik noktalar dijitalleştirildi.
// NOT: Değerler grafikten göz kararı okunmuştur; datasheet'inizdeki tam
// sayısal tablo varsa buradaki RS_R0_TABLO değerlerini onunla değiştirin.
static const float NEM_SEVIYELERI[3] = {30.0f, 60.0f, 85.0f};  // %RH
static const float SICAKLIK_NOKTALARI[13] = {-10,-5,0,5,10,15,20,25,30,35,40,45,50}; // °C

static const float RS_R0_TABLO[3][13] = {
  {1.70f,1.62f,1.58f,1.50f,1.44f,1.31f,1.27f,1.19f,1.16f,1.12f,1.00f,0.93f,0.88f}, // %30 RH
  {1.47f,1.38f,1.35f,1.28f,1.22f,1.11f,1.08f,1.00f,0.97f,0.93f,0.85f,0.78f,0.73f}, // %60 RH
  {1.28f,1.22f,1.18f,1.10f,1.05f,0.96f,0.92f,0.88f,0.85f,0.82f,0.73f,0.70f,0.63f}  // %85 RH
};

// Tek bir nem eğrisi (satır) üzerinde sıcaklığa göre lineer interpolasyon.
// Aralık dışındaki sıcaklıklar için en yakın uç değere sabitlenir (clamp).
static float _sicaklikEksenindeInterpole(const float satir[13], float sicaklik) {
  if (sicaklik <= SICAKLIK_NOKTALARI[0])  return satir[0];
  if (sicaklik >= SICAKLIK_NOKTALARI[12]) return satir[12];
  for (int i = 0; i < 12; i++) {
    if (sicaklik >= SICAKLIK_NOKTALARI[i] && sicaklik <= SICAKLIK_NOKTALARI[i + 1]) {
      float t = (sicaklik - SICAKLIK_NOKTALARI[i]) / (SICAKLIK_NOKTALARI[i + 1] - SICAKLIK_NOKTALARI[i]);
      return satir[i] + t * (satir[i + 1] - satir[i]);
    }
  }
  return satir[6]; // güvenlik amaçlı; normalde buraya ulaşılmaz
}

// Verilen sıcaklık ve nem için grafikten Rs/R0 katsayısını hesaplar.
// Önce sıcaklık ekseninde her nem eğrisi için ayrı ayrı interpolasyon yapılır,
// sonra bu iki sonuç arasında nem eksenine göre interpolasyon yapılır (bilineer).
// Nem, tablonun kapsadığı %30-%85 aralığının dışındaysa en yakın eğriye sabitlenir.
float nemSicaklikKatsayisi(float sicaklik, float nem) {
  if (nem <= NEM_SEVIYELERI[0]) {
    return _sicaklikEksenindeInterpole(RS_R0_TABLO[0], sicaklik);
  }
  if (nem >= NEM_SEVIYELERI[2]) {
    return _sicaklikEksenindeInterpole(RS_R0_TABLO[2], sicaklik);
  }
  if (nem <= NEM_SEVIYELERI[1]) {
    float alt = _sicaklikEksenindeInterpole(RS_R0_TABLO[0], sicaklik);
    float ust = _sicaklikEksenindeInterpole(RS_R0_TABLO[1], sicaklik);
    float t = (nem - NEM_SEVIYELERI[0]) / (NEM_SEVIYELERI[1] - NEM_SEVIYELERI[0]);
    return alt + t * (ust - alt);
  }
  float alt = _sicaklikEksenindeInterpole(RS_R0_TABLO[1], sicaklik);
  float ust = _sicaklikEksenindeInterpole(RS_R0_TABLO[2], sicaklik);
  float t = (nem - NEM_SEVIYELERI[1]) / (NEM_SEVIYELERI[2] - NEM_SEVIYELERI[1]);
  return alt + t * (ust - alt);
}

/* -------------------------------------------------------------------------- */
/*  DHT11 güncelleme (cache'li okuma)                                          */
/* -------------------------------------------------------------------------- */
// DHT11 saniyede birden fazla güvenilir okunamaz; bu yüzden her çağrıldığında
// okumaz, sadece DHT_OKUMA_ARALIGI_MS geçtiyse yeni okuma alır ve cache'ler.
// Okuma başarısız (NAN) gelirse bir önceki geçerli değer korunur.
void dhtGuncelle() {
  if (millis() - sonDhtOkumaZamani < DHT_OKUMA_ARALIGI_MS) return;
  sonDhtOkumaZamani = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) ortamSicaklik = t;
  if (!isnan(h)) ortamNem = h;

  Serial.printf("Sicaklik: %.1f C  |  Nem: %%%.1f\n", ortamSicaklik, ortamNem);
  if (isnan(t) || isnan(h)) {
    Serial.println("  (DHT11 okuma hatasi - son gecerli deger korunuyor)");
  }
}

/* -------------------------------------------------------------------------- */
/*  Ham sensör direnci (Rs) ölçümü                                             */
/* -------------------------------------------------------------------------- */
// Oversampling ile ADC okur ve MQ-3'ün anlık direncini (Rs) döndürür.
// Hem normal ölçümde (hamPromilOlcVeDondur) hem de R0 yeniden kalibrasyonunda
// (r0YenidenKalibreEt) kullanılır — tek yerden bakım kolaylığı için ayrıldı.
float sensorRsOlcVeDondur() {
  static const int ADC_ORNEK_SAYISI = 20;
  uint32_t mV_toplam = 0;
  for (int i = 0; i < ADC_ORNEK_SAYISI; i++) {
    mV_toplam += analogReadMilliVolts(MQ3_PIN);
  }
  uint32_t pin_mV = mV_toplam / ADC_ORNEK_SAYISI;
  float pin_voltaji = pin_mV / 1000.0;
  float gercek_sensor_voltaji = pin_voltaji * VOLTAJ_BOLUCU_ORANI;

  // Sınır korumaları
  if (gercek_sensor_voltaji < 0.1) gercek_sensor_voltaji = 0.1;
  if (gercek_sensor_voltaji > 4.9) gercek_sensor_voltaji = 4.9;

  return ((5.0 * RL) / gercek_sensor_voltaji) - RL;
}

/* -------------------------------------------------------------------------- */
/*  Ham promil okuma fonksiyonu (taban çıkarılmadan önceki değer)             */
/* -------------------------------------------------------------------------- */
// Rs/ratio hesaplar, DFRobot yerine MQ-3 datasheet Fig.1 eğrisinden türetilmiş
// güç fonksiyonu formülüyle mg/L ve promil döndürür.
float hamPromilOlcVeDondur() {
  float Rs = sensorRsOlcVeDondur();
  float ratio = Rs / R0;

  // --- Sıcaklık/Nem kompanzasyonu ---
  // R0, KALIBRASYON_SICAKLIK_REF / KALIBRASYON_NEM_REF koşulunda kalibre edildi
  // varsayılıyor. Ölçüm anındaki ortamSicaklik/ortamNem bundan farklıysa,
  // datasheet grafiğinden çıkarılan oran kadar ratio düzeltilir. Böylece
  // ölçüm, kalibrasyon koşuluna göre "normalize" edilmiş olur.
  float referansKatsayi = nemSicaklikKatsayisi(KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF);
  float anlikKatsayi    = nemSicaklikKatsayisi(ortamSicaklik, ortamNem);
  if (anlikKatsayi < 0.01f) anlikKatsayi = 0.01f; // güvenlik
  float duzeltmeFaktoru = referansKatsayi / anlikKatsayi;
  ratio *= duzeltmeFaktoru;

  if (ratio < 0.01) ratio = 0.01; // pow() için aşırı uç değerlerde stabilite

  // --- MQ-3 datasheet Fig.1 "Alcohol" eğrisinden türetilmiş güç fonksiyonu ---
  // mg/L = a * ratio^b   (a≈0.0321, b≈-1.515 — R0=temiz hava direncine göre,
  // datasheet'in S=Rs(hava)/Rs(0.4mg/L)>=5 minimum değeriyle ölçeklendirildi)
  float mg_L = 0.0321 * pow(ratio, -1.515);
  if (mg_L < 0) mg_L = 0;

  return mg_L * 2.1; // promil
}

/* -------------------------------------------------------------------------- */
/*  Kalıcı depolama (NVS) — R0 ve referans koşullarını yükle/kaydet            */
/* -------------------------------------------------------------------------- */
// Açılışta çağrılır. NVS'de kayıtlı bir değer varsa onu kullanır, yoksa
// yukarıdaki varsayılan (hardcoded) değerler geçerli kalır.
void r0veReferansiYukle() {
  prefs.begin(NVS_NAMESPACE, true); // salt-okunur aç
  R0                       = prefs.getFloat("r0",   R0);
  KALIBRASYON_SICAKLIK_REF = prefs.getFloat("tRef", KALIBRASYON_SICAKLIK_REF);
  KALIBRASYON_NEM_REF      = prefs.getFloat("hRef", KALIBRASYON_NEM_REF);
  prefs.end();

  Serial.printf("Yuklenen kalibrasyon -> R0:%.1f  Referans:%.1fC / %%%.1f\n",
                R0, KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF);
}

// Açılışta buton BUTON_KALIBRASYON_SURESI_MS boyunca basılı tutulursa çağrılır.
// Temiz hava varsayımıyla yeni bir R0 ölçer VE o anki gerçek sıcaklık/nemi
// yeni referans olarak kaydeder — böylece hem sensör yaşlanması (R0 driftı)
// hem de farklı bir ortamda kalibrasyon yapılmış olması aynı anda düzeltilir.
void r0YenidenKalibreEt() {
  Serial.println("=== R0 YENIDEN KALIBRASYON MODU ===");
  Serial.println("Ortamda TEMIZ HAVA olmali (alkol/kolonya YOK)!");

  const int ORNEK_SAYISI = 50; // ~5 saniye (100ms aralikla)
  float rsToplam = 0.0f;
  for (int i = 0; i < ORNEK_SAYISI; i++) {
    dhtGuncelle();
    lv_timer_handler(); // ui_ekran4 gibi bilgilendirme ekranının fiilen çizilmesini sağla
    wdtBesle(); // ~5 sn suren bu dongu boyunca watchdog'u besle
    rsToplam += sensorRsOlcVeDondur();
    delay(100);
  }

  R0 = rsToplam / ORNEK_SAYISI;
  KALIBRASYON_SICAKLIK_REF = ortamSicaklik;
  KALIBRASYON_NEM_REF      = ortamNem;

  prefs.begin(NVS_NAMESPACE, false); // okuma-yazma aç
  prefs.putFloat("r0",   R0);
  prefs.putFloat("tRef", KALIBRASYON_SICAKLIK_REF);
  prefs.putFloat("hRef", KALIBRASYON_NEM_REF);
  prefs.end();

  Serial.printf("Yeni R0: %.1f  |  Yeni referans: %.1fC / %%%.1f (NVS'ye kaydedildi)\n",
                R0, KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF);
}

/* -------------------------------------------------------------------------- */
/*  Havalandırma fanı kontrolü                                                 */
/* -------------------------------------------------------------------------- */
// Ölçüm bitip sonuç ekranına geçildiği an çağrılır: fanı açar ve süresini
// başlatır. Sensör haznesindeki alkol buharının aktif olarak tahliye edilmesini
// sağlar (pasif bekleme yerine).
void fanBaslat() {
  digitalWrite(FAN_PIN, HIGH);
  fanAktif = true;
  Serial.println("Fan acildi - havalandirma basladi (cooldown boyunca calisacak)");
}

// Cooldown (SONUC) süresi dolunca veya güvenlik amacıyla BEKLE durumuna
// dönüldüğünde çağrılır: fanı kapatır.
void fanDurdur() {
  digitalWrite(FAN_PIN, LOW);
  fanAktif = false;
  Serial.println("Fan kapandi");
}

/* -------------------------------------------------------------------------- */
/*  Buton debounce fonksiyonu                                                  */
/* -------------------------------------------------------------------------- */
// Her çağrıldığında ham pin durumunu okur, debounce mantığıyla stabil
// duruma ulaşınca butonStabilDurum'u günceller ve olcumBasladi'yı set eder.
void butonDebounceGuncelle() {
  bool hamOkuma = digitalRead(BUTON_PIN);

  if (hamOkuma != butonSonOkuma) {
    // Pin değişti, debounce sayacını sıfırla
    butonSonDegisimZamani = millis();
  }

  if ((millis() - butonSonDegisimZamani) > DEBOUNCE_MS) {
    // Değişiklik DEBOUNCE_MS'den uzun süredir stabil, kabul et
    if (hamOkuma != butonStabilDurum) {
      butonStabilDurum = hamOkuma;
    }
  }

  butonSonOkuma = hamOkuma;

  // LOW = basılı (INPUT_PULLUP)
  olcumBasladi = (butonStabilDurum == LOW);
}

/* -------------------------------------------------------------------------- */
/*  LVGL Timer Callback – Sensör okuma ve durum makinesi                       */
/* -------------------------------------------------------------------------- */

static const uint32_t SENSOR_TIMER_MS = 100;  // 100ms aralıklarla sensör oku

void alkolmetre_timer_cb(lv_timer_t * timer) {
  // Sıcaklık/nem cache'ini güncelle (DHT11 yavaş olduğu için kendi aralığında okur)
  dhtGuncelle();

  // Buton okuma (debounce edilmiş)
  butonDebounceGuncelle();

  // Ham promil oku (yeni güç fonksiyonu formülü + oversampling içeride yapılıyor)
  float hamPromil = hamPromilOlcVeDondur();

  // Taban (baseline) çıkarma: açılışta ölçülen temiz hava değerini çıkar
  float promil = hamPromil - tabanPromil;
  if (promil < 0) promil = 0;

  // --- TEST İÇİN canlı Serial çıktısı ---
  //Serial.printf("ham:%.3f taban:%.3f promil:%.3f\n", hamPromil, tabanPromil, promil);

  // Durum makinesi
  switch (durum)
  {
    case BEKLE:
      // Yeni buton basımı algıla (edge detection — basılı kalsa bile tekrar tetiklenmez)
      if (olcumBasladi && !oncekiButonDurumu)
      {
        // Ölçüm başlıyor: peak değişkenlerini sıfırla
        olcumPeakPromil = 0.0f;
        ardisikSayac = 0;
        olcumBaslangicZamani = millis();
        if (fanAktif) fanDurdur(); // güvenlik: yeni ölçüm başlarken fan kesin kapalı olsun
        durum = OLCUM;
      }
      break;

    case OLCUM:
    {
      uint32_t gecenSure = millis() - olcumBaslangicZamani;

      // Üfleme algılama: eşik üstü ardışık okuma sayısını takip et (gürültü spike'larını ele)
      if (promil > UFLEME_ESIK_PROMIL)
      {
        ardisikSayac++;
      }
      else
      {
        ardisikSayac = 0;
      }

      // Yeterli sayıda ardışık okuma varsa gerçek üfleme kabul edilir, peak güncellenir
      if (ardisikSayac >= GEREKLI_ARDISIK_SAYI && promil > olcumPeakPromil)
      {
        olcumPeakPromil = promil;
      }

      if (gecenSure >= OLCUM_SURESI_MS)
      {
        // 10 sn doldu: doğrulanmış peak değeri ekrana yaz
        char metinBuf[16];
        snprintf(metinBuf, sizeof(metinBuf), "%.2f", olcumPeakPromil);
        lv_label_set_text(uic_deger, metinBuf);

        _ui_screen_change(&ui_ekran2,
                          LV_SCR_LOAD_ANIM_MOVE_LEFT,
                          300,
                          0,
                          ui_ekran2_screen_init);

        fanBaslat(); // sonuç ekranına geçildi: havalandırmayı başlat
        sonucBaslangic = millis();
        durum = SONUC;
      }
      break;
    }

    case SONUC:
      // 40 saniye boyunca sonuç ekranında kal (cooldown süresi)
      // Fan bu sürenin TAMAMI boyunca çalışır, aşağıda BEKLE'ye dönerken kapatılır.
      if (millis() - sonucBaslangic >= COOLDOWN_MS)
      {
        _ui_screen_change(&ui_ekran1,
                          LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                          300,
                          0,
                          ui_ekran1_screen_init);

        fanDurdur(); // cooldown bitti: fanı kapat
        durum = BEKLE;
      }
      break;
  }

  // Edge detection: buton durumunu her döngüde güncelle
  oncekiButonDurumu = olcumBasladi;
}

/* -------------------------------------------------------------------------- */
/*  Setup                                                                      */
/* -------------------------------------------------------------------------- */

void setup() {
  Serial.begin(115200);

  // --- Onceki reset nedenini logla (watchdog gercekten calisti mi anlamak icin) ---
  resetNedeniYazdir();

  // --- Watchdog: mumkun oldugunca erken devreye al ---
  // NOT: setup() ve loop() ayni FreeRTOS gorevinde ("loopTask") calistigi icin
  // burada eklenen tek abonelik dosyanin TAMAMI (setup + loop) boyunca gecerlidir.
  wdtYapilandir(WDT_TIMEOUT_KURULUM_MS);
  esp_task_wdt_add(NULL); // mevcut gorevi (loopTask) TWDT'ye abone et
  wdtBesle();

  // --- Buton pinini, fan pinini ve DHT11'i en başta hazırla ---
  pinMode(BUTON_PIN, INPUT_PULLUP);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // fan kapalı başlar
  dht.begin();
  delay(1500); // DHT11'in ilk kararlı okumayı verebilmesi için kısa bekleme

  // --- NVS'den kayıtlı R0 ve kalibrasyon referansını yükle (varsa) ---
  r0veReferansiYukle();

  // --- Açılışta buton basılı tutulursa yeniden kalibrasyon moduna gir ---
  // NOT: Bu kontrol sadece GPIO okuduğu için sensöre dokunmuyor; ısınma
  // beklemesinden ÖNCE yapılabilir (kullanıcı 60+3 saniye beklemek zorunda kalmasın).
  bool kalibrasyonModuTetiklendi = false;
  delay(50); // pinMode sonrası kısa stabilizasyon
  if (digitalRead(BUTON_PIN) == LOW) {
    Serial.printf("Buton basili - kalibrasyon icin %lu sn boyunca basili tutun...\n",
                  (unsigned long)(BUTON_KALIBRASYON_SURESI_MS / 1000));
    uint32_t basilmaBaslangic = millis();
    while (digitalRead(BUTON_PIN) == LOW &&
           (millis() - basilmaBaslangic) < BUTON_KALIBRASYON_SURESI_MS) {
      wdtBesle(); // bu dongu ~3 sn'ye kadar surebilir, watchdog'u besle
      delay(10);
    }
    kalibrasyonModuTetiklendi = (millis() - basilmaBaslangic) >= BUTON_KALIBRASYON_SURESI_MS;
    if (kalibrasyonModuTetiklendi) {
      Serial.println("Kalibrasyon modu onaylandi - isinma sonrasi baslayacak.");
    } else {
      Serial.println("Kalibrasyon iptal edildi (buton erken birakildi).");
    }
  }

  // --- LVGL / ekran / dokunmatik başlatma ---
  // NOT: Isınma ve (tetiklendiyse) yeniden kalibrasyon ölçümü sırasında ekranda
  // bilgilendirme gösterebilmek için LVGL ve arayüz, bu adımlardan ÖNCE
  // başlatılıyor (eskiden bu adımların sonrasındaydı).
  lv_init();

  analogSetAttenuation(ADC_11db);

  analogSetClockDiv(4);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif

  tft.begin();
  tft.setRotation(3);  // Landscape orientation

  static lv_display_t *disp;
  disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, my_disp_flush);

  static lv_indev_t *indev;
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  lv_tick_set_cb(my_tick_get_cb);

  ui_init();
  wdtBesle(); // ui_init() ne kadar surerse sursun guvenlik icin besle

  // --- Kalibrasyon modu tetiklendiyse ısınma + ölçüm boyunca bilgilendirme ekranını göster ---
  if (kalibrasyonModuTetiklendi) {
    _ui_screen_change(&ui_ekran4, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, ui_ekran4_screen_init);
  }

  // --- MQ-3 ısınma bekleme süresi ---
  // Sensöre ait HERHANGİ bir okuma (ne taban ölçümü ne de R0 kalibrasyonu)
  // bu süre dolmadan yapılmaz; aksi halde ısıtıcı tam kararlı sıcaklığa
  // ulaşmadan alınan Rs değerleri sapmış olur.
  Serial.printf("MQ-3 isiniyor, %lu saniye bekleniyor...\n",
                (unsigned long)(ISINMA_SURESI_MS / 1000));
  {
    uint32_t isinmaBaslangic = millis();
    while (millis() - isinmaBaslangic < ISINMA_SURESI_MS) {
      dhtGuncelle(); // bekleme sırasında sıcaklık/nem cache'i de güncel kalsın
      lv_timer_handler(); // ekranın (ör. ui_ekran4) fiilen çizilmesini sağla
      wdtBesle(); // 60 saniyelik bekleme boyunca watchdog'u besle
      delay(200);
    }
  }
  Serial.println("Isinma tamamlandi.");

  if (kalibrasyonModuTetiklendi) {
    r0YenidenKalibreEt();
  }

  dhtGuncelle();
  sonDhtOkumaZamani = 0; // taban ölçümü sırasında da güncel kalsın diye sıfırla

  // --- Açılışta otomatik taban (baseline) ölçümü ---
  // NOT: Bu ölçüm sırasında ortamda alkol OLMAMALI (temiz hava varsayılıyor).
  Serial.println("Taban (baseline) olculuyor, temiz hava varsayiliyor...");
  {
    const int TABAN_ORNEK_SAYISI = 30; // ~3 saniye (her biri 100ms aralikla)
    float toplam = 0.0f;
    for (int i = 0; i < TABAN_ORNEK_SAYISI; i++) {
      dhtGuncelle(); // sıcaklık/nem cache'i taze kalsın
      lv_timer_handler(); // ekranın fiilen çizilmesini sağla
      wdtBesle(); // ~3 sn suren bu dongu boyunca watchdog'u besle
      toplam += hamPromilOlcVeDondur();
      delay(100);
    }
    tabanPromil = toplam / TABAN_ORNEK_SAYISI;
  }
  //Serial.print("Taban promil degeri: ");
  //Serial.println(tabanPromil, 3);

  // --- Yeniden kalibrasyon yapıldıysa "KALİBRASYON TAMAMLANDI" ekranını göster ---
  // ui_init() içinde ui_ekran1/2/3/4 hepsi önceden oluşturulup ui_ekran1 yüklendiği
  // için burada sadece ui_ekran3'e geçip birkaç saniye bekletiyoruz (lv_timer_handler
  // manuel çağrılarak ekranın fiilen çizilmesi sağlanıyor), ardından ui_ekran1'e döneriz.
  if (kalibrasyonModuTetiklendi) {
    _ui_screen_change(&ui_ekran3, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, ui_ekran3_screen_init);
    uint32_t kalibrasyonEkraniBaslangic = millis();
    while (millis() - kalibrasyonEkraniBaslangic < KALIBRASYON_EKRANI_SURESI_MS) {
      lv_timer_handler();
      wdtBesle();
      delay(5);
    }
    _ui_screen_change(&ui_ekran1, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, ui_ekran1_screen_init);
  }

  // --- Kurulum tamamlandi: normal calisma icin daha siki bir watchdog suresine gec ---
  // Artik uzun bloklamali bekleme yok; bir loop() turunun onlarca ms surmesi
  // beklendiginden, kilitlenmeyi cok daha hizli yakalamak icin sure kisaltiliyor.
  wdtYapilandir(WDT_TIMEOUT_CALISMA_MS);
  wdtBesle();

  // Alkolmetre sensör timer'ını oluştur (100ms aralıklarla çalışır)
  lv_timer_create(alkolmetre_timer_cb, SENSOR_TIMER_MS, NULL);

  //Serial.println("Setup done");
}

/* -------------------------------------------------------------------------- */
/*  Main loop                                                                  */
/* -------------------------------------------------------------------------- */

void loop() {
  uint32_t next_ms = lv_timer_handler();  // LVGL görevlerini çalıştır
  wdtBesle(); // buraya zamaninda donulemezse (kilitlenme) TWDT cihazi resetler
  delay(next_ms < 1 ? 1 : next_ms);      // Bir sonraki timer'a kadar bekle
}
