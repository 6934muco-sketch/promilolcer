// Arduino + LVGL 9.3 + TFT_eSPI
// Değişiklik Günlüğü: ./DEGISIKLIK_GUNLUGU.md

#include <TFT_eSPI.h>
#include <lvgl.h>
#include <ui.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include "ses.h"
#include "led.h"

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
//   - WDT_TIMEOUT_KURULUM_MS : setup() boyunca. Isınma (60 sn) ve önbellek
//     ısıtma (2 sn) gibi BİLİNEN uzun bekleme adımları var; bu döngülerin
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
float R0 = 49.73; // Temiz havadaki direnç (60'a bölünmüş ölçek — bkz. hamPromilOlcVeDondur() içindeki mg_L formülü yorumu)

// --- Sıcaklık/Nem/Basınç sensörleri (2 adet, farklı görevlerde) ---
// NOT: GPIO4, ekranın TFT_RST pini ile çakıştığı için kullanılamaz.
// Kullanılan diğer pinler: 34 (MQ3), 27 (buton), 26 (fan), 16/17 (DFPlayer UART2),
// 19/23/18/15/2/4 (TFT), 21/22 (I2C SDA/SCL — BME280).
//
// NEFES_BME280 (eski DHT11'in yerine): MQ-3'ten UZAĞA, üfleme ağzına/nefes kanalına
// yakın yerleştirilir. Görevi DHT11 ile AYNI — kanalda kalıntı/informal üfleme
// (kirlilik) tespiti — ama artık nem FARKI yerine ANLIK BASINÇ SIÇRAMASI ile
// yapılıyor: kanala üflendiğinde/nefes verildiğinde sensörün gördüğü basınç kısa
// süreliğine kendi ortalamasından belirgin şekilde sapar. I2C ile haberleştiği için
// tek bir veri hattı yerine SDA/SCL kullanır (GPIO21/GPIO22, ESP32 varsayılan I2C
// pinleri — TFT/DFPlayer/MQ3/buton/fan ile çakışmıyor).
#define NEFES_I2C_SDA_PIN 21
#define NEFES_I2C_SCL_PIN 22
#define NEFES_BME280_I2C_ADRESI 0x76 // çoğu ucuz modülde varsayılan; bazılarında 0x77 olabilir
#define NEFES_BASINC_SERIAL_LOG 1    // 1 = Serial'e canlı basınç logu bas (test/kalibrasyon için); bitince 0 yapın
Adafruit_BMP280 bmeNefes(&Wire);
bool bmeNefesHazir = false; // begin() basarili oldu mu (I2C'de sensor bulundu mu)

// DHT_ORTAM (DHT22): MQ-3'ün hemen yanına yerleştirilir. Görevi eski tek DHT11'in
// yaptığı iş: hamPromilOlcVeDondur() içindeki sıcaklık/nem kompanzasyonu. GPIO32
// seçildi çünkü boşta, strapping pini değil, input-only değil ve DFPlayer'ın
// 16/17 pinleriyle çakışmıyor (yedek: GPIO33). BME280 geçişinden ETKİLENMEDİ.
#define DHT_ORTAM_PIN 32
#define DHT_ORTAM_TIPI DHT22
DHT dhtOrtam(DHT_ORTAM_PIN, DHT_ORTAM_TIPI);
#define DHT_ORTAM_SERIAL_LOG 1    // 1 = Serial'e canlı DHT22 okuma logu bas (test/hata ayıklama için); bitince 0 yapın

// R0 kalibrasyonunun yapıldığı ortam koşulları.
// Bu değerler NVS'de (kalıcı flash hafıza) saklanıyor ve açılışta oradan yükleniyor.
// Sahada yeniden yazan akış var: manuel R0_KALIBRASYON durumu (bkz. aşağıda
// R0_KAL_* sabitleri ve r0KalibrasyonuBitir()). Aşağıdaki sayılar sadece NVS boşsa
// (ilk açılış / hiç kalibrasyon yapılmamış) kullanılacak VARSAYILAN değerlerdir.
// Son gerçek kalibrasyon: Sicaklik: 27.7 C | Nem: %55.1
float KALIBRASYON_SICAKLIK_REF = 27.7f; // °C
float KALIBRASYON_NEM_REF      = 55.1f; // %RH

// --- Kalıcı depolama (NVS) ---
Preferences prefs;
static const char* NVS_NAMESPACE = "alkolmetre";

// --- Sensör önbellekleri ---
// DHT22 (ortam) saniyede birden fazla güvenilir okunamadığı için aralık gerekiyor.
// BME280 (nefes) I2C üzerinden çok daha hızlı ve güvenilir okunabiliyor; kısa bir
// üfleme basınç sıçramasını kaçırmamak için ana sensör timer'ıyla (100ms) AYNI hızda,
// pratikte her çağrıda tazeleniyor.
static const uint32_t DHT_ORTAM_OKUMA_ARALIGI_MS   = 2000; // DHT22, yavaş kompanzasyon verisi
static const uint32_t NEFES_BME280_OKUMA_ARALIGI_MS = 100;  // BME280, olay (üfleme) tespiti için hızlı tutuldu
uint32_t sonOrtamOkumaZamani = 0;
uint32_t sonNefesOkumaZamani = 0;
float ortamSicaklik = KALIBRASYON_SICAKLIK_REF; // ilk okumadan önceki varsayılan
float ortamNem      = KALIBRASYON_NEM_REF;
float nefesBasinc   = 1013.25f; // hPa, ilk okumadan önceki varsayılan (deniz seviyesi standart basıncı)

// --- Üfleme kanalı kirlilik tespiti ---
// Nefes-BME280'in ANLIK basıncı, KENDİ yavaş hareketli ortalamasına (EMA) göre bu kadar
// (hPa) yüksekse kanala üfleniyor/kalıntı üfleme var demektir. DHT11 döneminde bu
// karşılaştırma nefes-nem'i ortam-nem'e karşı bakıyordu; BME280'de ikinci bir basınç
// referansı olmadığından (DHT22 basınç ölçmez) KENDİ ortalamasına karşı bakılıyor —
// tıpkı aşağıdaki "yedek ortam kirlilik" algoritmasında zaten kullanılan yöntem gibi.
// Mutlak değer sıcaklık/hava durumu kaynaklı yavaş basınç driftinden etkilenmesin diye
// EMA'ya göre FARK kullanılıyor. Sahada ince ayar gerekebilir (gerçek üfleme genlğine göre).
static const float NEFES_BASINC_ORTALAMA_ALFA = 0.02f;  // EMA katsayısı (ortam EMA'sından biraz daha hızlı: BME280 100ms'de bir okunuyor)
static const float KANAL_KIRLILIK_ESIK_HPA    = 1.0f;   // hPa, kendi ortalamasına göre sapma eşiği — SAHADA KALİBRE EDİLMELİ
static const int   KANAL_KIRLILIK_ARDISIK_SAYI = 2;     // gürültüyü elemek için ardışık okuma sayısı
float nefesBasincOrtalama = 1013.25f; // yavaş adapte olan EMA taban basıncı (ilk gerçek okumada anında bu değere snap'lenir, bkz. nefesBmeGuncelle)
bool  nefesBasincOrtalamaIlkOkuma = true;
int  kirlilikArdisikSayac = 0;
bool kanalKirliMi = false; // nefes-BME280 tabanlı HAM tespit (sensör arızasından etkilenebilir)

// --- BEKLE'de butonsuz (kendiliğinden) kirlilik tespiti: BME280 (son 10sn
// içinde algılama) VEYA MQ-3 (son 10sn'nin ortalaması) ---
// Sadece BEKLE'nin buton-basılmadan-önceki 10 saniyelik penceresinde,
// her tikte sürekli değerlendirilir (bkz. SENSOR_DAVRANIS_NOTU.md §3).
// TABAN_OLC/SONUC/R0_KALIBRASYON'daki kirlilik kontrolleri hâlâ anlık
// kanalKirliMiEfektif'i kullanır, bu bölüm ONLARI etkilemez.
static const uint32_t UFLEME_ONCESI_PENCERE_MS  = 10000; // 10sn: BME280 "son N saniyede algılandı mı" penceresi
static const float    MQ3_ORTALAMA_ESIK_PROMIL  = 0.1f;  // MQ3'ün 10sn'lik ortalaması bunun üstündeyse
static const int      MQ3_ORTALAMA_ORNEK_SAYISI = 100; // UFLEME_ONCESI_PENCERE_MS / 100ms (SENSOR_TIMER_MS) = 100 örnek

bool     bmeKirlilikGorulduMu   = false; // hiç en az bir kez tespit edildi mi (boot sonrası ilk tetikleme öncesi false kalır)
uint32_t sonBmeKirlilikZamani   = 0;     // en son kanalKirliMi=true olduğu an (millis())

float mq3OrtalamaPencere[MQ3_ORTALAMA_ORNEK_SAYISI] = {0};
int   mq3OrtalamaIndex  = 0;
float mq3OrtalamaToplam = 0.0f;

// hamPromil'i kayan 10sn'lik pencereye ekler (eskiyi çıkarıp yenisini ekler).
void mq3OrtalamaGuncelle(float hamPromil) {
  mq3OrtalamaToplam -= mq3OrtalamaPencere[mq3OrtalamaIndex];
  mq3OrtalamaPencere[mq3OrtalamaIndex] = hamPromil;
  mq3OrtalamaToplam += hamPromil;
  mq3OrtalamaIndex = (mq3OrtalamaIndex + 1) % MQ3_ORTALAMA_ORNEK_SAYISI;
}

float mq3OrtalamaPromil() {
  return mq3OrtalamaToplam / MQ3_ORTALAMA_ORNEK_SAYISI;
}

// BEKLE'nin buton-basılmadan-önceki (sürekli, her tikte değerlendirilen)
// 10sn'lik pencere kontrolü: BME280 son 10sn içinde en az bir kez
// üfleme/kirlilik algıladıysa VEYA MQ-3'ün son 10sn'lik ortalaması eşiği
// aşıyorsa true. İkisinden biri yeterli (OR) — herhangi biri kirlilik
// gösteriyorsa kanalı temizlemeye geç.
bool butonsuzKirlilikTespitEdildiMi() {
  bool bmeSonPencerede = bmeKirlilikGorulduMu && (millis() - sonBmeKirlilikZamani <= UFLEME_ONCESI_PENCERE_MS);
  bool mq3OrtalamaYuksek = mq3OrtalamaPromil() > MQ3_ORTALAMA_ESIK_PROMIL;
  return bmeSonPencerede || mq3OrtalamaYuksek;
}

// --- Nefes-BME280 arıza tespiti ---
// Üç bağımsız arıza belirtisi var:
//   1) begin() sırasında I2C'de sensör bulunamadı (bkz. bmeNefesHazir, setup() içinde
//      set edilir) -> ANINDA arızalı sayılır, DHT11'de olmayan bir avantaj (I2C init
//      ACK verir/vermez kesin bilgi verir).
//   2) Sensör I2C'de yanıt vermeye devam ediyor ama okunan basınç uzun süre
//      (NEFES_ARIZA_SURESI_MS) neredeyse hiç değişmiyor (donmuş/takılı kalmış veri) ->
//      bu da DHT11 döneminde kullanılan "donma tespiti" ile birebir aynı mantık, sadece
//      %RH yerine hPa eşiği kullanılıyor. Gerçek ortamda basınç, dakikalar içinde en
//      azından ölçüm gürültüsü kadar dalgalanır.
//   3) Okunan basınç fiziksel olarak İMKANSIZ bir aralıkta (bkz. NEFES_BASINC_GECERLI_*_HPA,
//      nefesBmeGuncelle() içinde kontrol edilir). Sensör VCC'si kesilip SDA/SCL/GND hâlâ
//      bağlı kaldığında (I2C pull-up'ları üzerinden parazitik beslenip) çip dijital olarak
//      cevap vermeye devam edebilir ama anlamsız (ör. negatif) değerler üretir — bu veri
//      GÜRÜLTÜLÜ olduğu için (2)'deki "donma" testi bunu YAKALAYAMAZ, bu yüzden ayrı bir
//      aralık kontrolü gerekiyor.
// Her üç durumda da "kirli" sinyali sonsuza kadar takılıp fanı sürekli açık bırakmasın diye
// otomatik olarak MQ-3'ün kendi ADC okumasına (bkz. mq3KirlilikKontrol()) geçilir.
static const float    NEFES_DEGISME_ESIK_HPA = 0.03f;     // bu kadar altındaki fark "değişmedi" sayılır
static const uint32_t NEFES_ARIZA_SURESI_MS  = 600000UL;  // 10 dakika boyunca değişmezse arızalı say
static const float    NEFES_BASINC_GECERLI_MIN_HPA  = 800.0f;  // bunun altı fiziksel olarak imkansız kabul edilir
static const float    NEFES_BASINC_GECERLI_MAKS_HPA = 1300.0f; // bunun üstü de (güçlü üfleme payı dahil) imkansız kabul edilir
bool     nefesIlkOkuma = true;
float    nefesBasincSonDeger = 0.0f;
uint32_t nefesDegismemeBaslangic = 0;
bool     nefesSensorArizali = false;

// --- Yedek (fallback) kirlilik tespiti: MQ-3'ün kendi ADC okuması ---
// Nefes sensörü arızalı sayıldığında devreye girer. Buton basılı DEĞİLKEN
// (yani resmi bir ölçüm sürmüyorken) MQ-3'ün ham okuması (hamPromil)
// UFLEME_ESIK_PROMIL eşiğini ardışık olarak aşarsa, kanala informal/kalıntı bir
// üfleme yapıldığı kabul edilir. NOT: OLCUM (asıl üfleme) durumunda ASLA
// çalıştırılmaz — o sırada MQ-3'ün yüksek okuması zaten beklenen ve istenen bir
// şeydir, kirlilik değildir.
int  mq3KirlilikArdisikSayac = 0;
bool mq3KirliMi = false;

// Durum makinesinin fiilen kullandığı EFEKTİF kirlilik bayrağı: nefes sensörü sağlıklıysa
// onun sonucu, arızalıysa yedek (MQ-3 ADC) sonucu kullanılır.
bool kanalKirliMiEfektif = false;

// Kirlilik tespit edildiğinde kanalın temizlenmesi için fanın çalışacağı SABİT süre.
// Nem eşiğinin düşmesini beklemek yerine (öngörülemez), sabit bir süre tercih edildi.
static const uint32_t KANAL_TEMIZLEME_SURESI_MS = 10000; // 10 saniye
uint32_t kanalTemizlemeBaslangic = 0;

// --- Havalandırma fanı (ölçüm sonrası aktif temizleme + kanal temizleme) ---
// NOT: Fan doğrudan GPIO'dan beslenemez; bir NPN transistör/MOSFET veya röle
// modülü üzerinden sürülmeli (flyback diyot eklemeyi unutma).
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

// Taban (baseline) değeri — artık HER ÖLÇÜMDEN HEMEN ÖNCE (TABAN_OLC durumunda)
// otomatik olarak yeniden ölçülür (eskiden sadece açılışta bir kez ölçülüyordu).
// Bu sayede R0/sensör driftinin etkisi her ölçümde kendiliğinden telafi edilir.
float tabanPromil = 0.0f;
static const int TABAN_ORNEK_SAYISI = 15; // ~1.5 sn (100ms aralıkla) - açılıştaki 30/3sn'den kısaltıldı
float tabanOlcumToplami = 0.0f;
int   tabanOlcumSayac = 0;

// Ölçüm parametreleri
static const uint32_t OLCUM_SURESI_MS       = 10000; // üfleme hiç algılanmazsa güvenlik zaman aşımı
static const uint32_t ANALIZ_OLCUM_SURESI_MS = 6000;  // üfleme algılandığı andan itibaren analiz penceresi
static const uint32_t COOLDOWN_MS            = 10000; // ölçümler arası bekleme süresi
static const float    UFLEME_ESIK_PROMIL     = 0.05f; // bu değerin üstü = üfleme algılandı
                                                       // NOT: artık taban çıkarıldığı için 0.05
                                                       // makul bir başlangıç; test sonrası ayarlanacak
static const float    YASAL_SINIR_PROMIL     = 0.50f; // bu değer ve üzeri "yüksek" sonuç sayılır

// --- OLCUM: "sensör bekleme" -> "analiz kısmı" geçiş tetikleyicisi (SADECE ---
// analizYaziGosterildi=true olma ANINI belirler; olcumPeakPromil'in nasıl
// hesaplandığını ETKİLEMEZ, o hâlâ UFLEME_ESIK_PROMIL/GEREKLI_ARDISIK_SAYI/
// ardisikSayac üzerinden değişmeden çalışır). Nefes-BMP280 sağlıklıysa onun
// zaten sürekli hesaplanan basınç-sıçraması sinyali (kanalKirliMi) kullanılır;
// arızalıysa buraya özel, MQ3_ORTALAMA_ESIK_PROMIL/mq3KirlilikArdisikSayac'tan
// KAVRAMSAL OLARAK FARKLI (o ikisi durum != OLCUM'da çalışır, BEKLE'nin
// kendiliğinden kirlilik tespiti içindir) yeni bir MQ-3 yedek sayaç kullanılır.
static const float UFLEME_TETIK_MQ3_ESIK_PROMIL = 0.05f; // nefes sensörü arızalıyken: bu eşik üstü hamPromil
static const int   UFLEME_TETIK_ARDISIK_SAYI    = 2;    // ... bu kadar ardışık okuma = "üfleme algılandı" (analiz kısmına geç)

uint32_t olcumBaslangicZamani = 0;
int      uflemeTetikMq3ArdisikSayac = 0; // sadece OLCUM'un blow-trigger yedek yolunda kullanılır (bkz. yukarıdaki not)
bool     olcumOrtaSesiCalindi = false; // "Üflemeye devam edin" bir kez çalınsın diye
bool     analizYaziGosterildi = false; // "Analiz yapılıyor" yazısı/sesi gerçek üfleme algılanınca bir kez tetiklensin diye
uint32_t analizYaziZamani = 0;         // "Analiz yapılıyor" ilk gösterildiği an (üfleme algılama anı) — 6sn'lik pencere buradan sayılır

// Peak-hold ölçüm parametreleri
static const int GEREKLI_ARDISIK_SAYI = 3; // eşik üstü kaç ardışık okuma (300ms) geçerli üfleme sayılır
float olcumPeakPromil = 0.0f;
int ardisikSayac = 0;

/* -------------------------------------------------------------------------- */
/*  R0 (temiz hava direnci) manuel kalibrasyonu                                */
/* -------------------------------------------------------------------------- */
// Taban ölçümü (tabanPromil) sadece KISA VADELİ/toplamsal sürüklenmeyi telafi eder
// (bkz. yukarıdaki tabanPromil yorumu). R0'ın kendisi zamanla (sensör yaşlanması)
// kayabilir; bu, çarpımsal bir hata olduğu için taban çıkarmayla tam telafi edilmez
// (bkz. mg_L = a*(Rs/R0)^b — R0 hatası ratio'yu ölçekler, üs ile büyür). Bu yüzden
// R0'ı ayrı, daha nadir ve daha özenli (uzun örnekleme + sıkı tolerans) bir
// R0_KALIBRASYON durumunda güncelliyoruz. Tek tetikleyici: BEKLE'de 10sn
// kesintisiz buton basılı tutma (manuel). Eskiden ayrıca her 500 ölçümde bir
// otomatik/boşta-kalma tabanlı bir tetikleyici de vardı, KALDIRILDI — artık
// R0 kalibrasyonu SADECE manuel yapılıyor. olcumSayaci yine de bilgi/izleme
// amaçlı NVS'de sayılmaya devam ediyor (bkz. r0veReferansiYukle, OLCUM tamamlanma bloğu).
static const uint32_t UZUN_BASMA_SURESI_MS  = 10000;  // 10 sn kesintisiz basılı tutma = manuel R0 kalibrasyonu
static const uint32_t R0_KAL_PURGE_SURESI_MS  = 30000; // örneklemeden önce fan ile kanalı temizle
static const uint32_t R0_KAL_SETTLE_SURESI_MS = 2000; // fan kapandıktan sonra havanın durulması için bekleme
static const uint32_t R0_KAL_ORNEK_ARALIGI_MS = 500;  // kararlılık örnekleri arası süre
static const int      R0_KAL_PENCERE_BOYUTU   = 10;   // kararlılık kontrolü için kayan (dairesel) pencere
static const uint32_t R0_KAL_TIMEOUT_MS       = 120000; // kararlılığa ulaşmazsa vazgeç (sınırsız/1 saat bekleme YOK)
static const float    R0_KAL_TOLERANS      = 0.02f;   // pencere içi (maks-min)/ortalama < %2 = "oturdu"
static const float    R0_KAL_UFLEME_ORANI  = 0.05f;   // pencere ortalamasına göre ani %5+ Rs düşüşü = üfleme geldi, iptal

int      olcumSayaci      = 0;     // NVS'de kalıcı; artık sadece bilgi amaçlı (bir tetikleyici YOK)
uint32_t sonAktiviteZamani = 0;    // son buton basımı/kalibrasyon denemesi zamanı

// Uzun basma (10sn) tespiti: kısa basmadan ayırt edebilmek için artık ölçüm AŞAĞI
// kenarda değil, BIRAKILINCA (yukarı kenar) başlıyor — basılı tutulduğu sürede
// 10sn'yi geçip geçmediği izlenebiliyor. Kısa bir tıklama için bu, algılanamayacak
// kadar küçük bir gecikme (basış-bırakış arası zaten doğal olarak var).
uint32_t butonBasiliBaslangic = 0;
bool     uzunBasmaIslendi     = false;

// R0_KALIBRASYON durumu iç fazları ve kararlılık penceresi
enum R0KalFaz { R0KAL_PURGE, R0KAL_SETTLE, R0KAL_ORNEKLE };
R0KalFaz r0KalFaz            = R0KAL_PURGE;
uint32_t r0KalFazBaslangic   = 0;
uint32_t r0KalGenelBaslangic = 0;
uint32_t r0KalSonOrnekZamani = 0;
float    r0KalPencere[R0_KAL_PENCERE_BOYUTU];
int      r0KalPencereSayac = 0;
int      r0KalPencereIndex = 0;

// R0 kalibrasyonu, ÖRNEKLE fazında üfleme tespit edilip kanal temizlemeye
// yönlendirildiğinde set edilir: KANAL_TEMIZLE bitince BEKLE'ye direkt dönmek
// yerine ekran4'te 3sn uyarı mesajı gösterilmesi gerektiğini işaretler.
bool     r0KalTemizlemeSonrasiMesajGoster = false;
uint32_t r0KalBasarisizMesajBaslangic = 0;

enum Durum
{
    BEKLE,          // boşta, buton/kirlilik bekleniyor
    TABAN_OLC,      // ölçüm öncesi otomatik taban (baseline) yeniden hesaplama
    OLCUM,          // sensör bekleme (LED_BEKLE) + analiz kısmı (LED_UFLEME, 6sn) — bkz. analizYaziGosterildi
    SONUC,          // sonuç ekranı + fan cooldown
    KANAL_TEMIZLE,  // kirlilik tespit edildi: fan sabit süre çalışır, ölçüm bloklanır
    R0_KALIBRASYON, // manuel R0 (temiz hava direnci) yeniden kalibrasyonu
    R0_KAL_BASARISIZ_MESAJ // R0 kalibrasyonu kanal kirliliği yüzünden iptal oldu: kanal temizliği sonrası ekran4'te 3sn uyarı gösterilir
};

Durum durum = BEKLE;

unsigned long sonucBaslangic = 0;
int fanGeriSayimSonSaniye = -1; // uic_fangerisayma'yı sadece değer değiştiğinde yeniden çiz

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
/*  Sensör güncelleme (cache'li okuma) — iki bağımsız sensör                   */
/* -------------------------------------------------------------------------- */
// DHT22 saniyede birden fazla güvenilir okunamaz; bu yüzden her çağrıldığında
// okumaz, sadece kendi aralığı geçtiyse yeni okuma alır ve cache'ler. BME280 (nefes)
// I2C üzerinden çok daha hızlı okunabildiğinden onun aralığı ana timer'la aynı (100ms).
// İkisinde de okuma başarısız (NAN) gelirse bir önceki geçerli değer korunur.

// DHT22 (MQ-3'ün yanında) — SADECE sıcaklık/nem kompanzasyonu için
// (hamPromilOlcVeDondur() içinde kullanılır). Artık kirlilik tespiti
// GÖREVİ YOK — yedek kirlilik tespiti MQ-3'ün kendi ADC okumasına taşındı
// (bkz. mq3KirlilikKontrol()).
void dhtOrtamGuncelle() {
  if (millis() - sonOrtamOkumaZamani < DHT_ORTAM_OKUMA_ARALIGI_MS) return;
  sonOrtamOkumaZamani = millis();

  float t = dhtOrtam.readTemperature();
  float h = dhtOrtam.readHumidity();

  if (!isnan(t)) ortamSicaklik = t;
  if (!isnan(h)) ortamNem = h;

  if (isnan(t) || isnan(h)) {
    Serial.println("  (DHT22 ortam okuma hatasi - son gecerli deger korunuyor)");
  }

#if DHT_ORTAM_SERIAL_LOG
  // Test/hata ayıklama için canlı DHT22 çıktısı — sensörün gerçekten okuyup
  // okumadığını (t/h NaN mı, gerçekten değişiyor mu) görmek için kullanın.
  // İşiniz bitince DHT_ORTAM_SERIAL_LOG'u 0 yapıp kapatabilirsiniz.
  Serial.printf("[DHT22-ORTAM] pin:%d sicaklik:%s nem:%s (kullanilan -> sicaklik:%.1f C nem:%.1f %%)\n",
                DHT_ORTAM_PIN,
                isnan(t) ? "NaN(okunamadi)" : String(t, 1).c_str(),
                isnan(h) ? "NaN(okunamadi)" : String(h, 1).c_str(),
                ortamSicaklik, ortamNem);
#endif
}

// Yedek (fallback) kirlilik tespiti — MQ-3'ün kendi ADC okuması (hamPromil) üzerinden.
// Sadece nefes-BME280 arızalı sayıldığında ANLAMLIDIR (bkz. kanalKirliMiEfektif), ama
// zararsız olduğu için her zaman güncellenir. OLCUM sirasinda ASLA çağrılmamalıdır —
// çağıran taraf (alkolmetre_timer_cb) bunu garanti eder; o sırada MQ-3'ün yüksek
// okuması zaten beklenen/istenen bir şeydir (gerçek üfleme), kirlilik değildir.
void mq3KirlilikKontrol(float hamPromil) {
  if (hamPromil > UFLEME_ESIK_PROMIL) {
    mq3KirlilikArdisikSayac++;
  } else {
    mq3KirlilikArdisikSayac = 0;
  }

  mq3KirliMi = (mq3KirlilikArdisikSayac >= KANAL_KIRLILIK_ARDISIK_SAYI);
}

// Kirlilik/kalıntı tespiti: nefes-BME280'in ANLIK basıncı, kendi yavaş EMA taban
// basıncına göre eşik kadar yüksekse ardışık sayaç artar; belirli sayıya ulaşınca
// kanalKirliMi bayrağı set edilir. Basınç düştüğünde sayaç sıfırlanıp bayrak
// otomatik temizlenir (latch değildir). Ortalama, kirlilik YOKKEN güncellenir;
// aksi halde bir üfleme/kirlilik olayı ortalamayı da yukarı çekip kendi kendini
// maskeler.
void nefesKirlilikKontrol() {
  if (!kanalKirliMi) {
    nefesBasincOrtalama += NEFES_BASINC_ORTALAMA_ALFA * (nefesBasinc - nefesBasincOrtalama);
  }

  float fark = nefesBasinc - nefesBasincOrtalama;

  if (fark > KANAL_KIRLILIK_ESIK_HPA) {
    kirlilikArdisikSayac++;
  } else {
    kirlilikArdisikSayac = 0;
  }

  kanalKirliMi = (kirlilikArdisikSayac >= KANAL_KIRLILIK_ARDISIK_SAYI);

  if (kanalKirliMi) {
    // BEKLE'nin butonsuz kontrolü (butonsuzKirlilikTespitEdildiMi) için: "son
    // 10sn içinde BME280 en az bir kez algıladı mı" sorusuna cevap verebilmek
    // amacıyla en son algılama anını damgala.
    bmeKirlilikGorulduMu = true;
    sonBmeKirlilikZamani = millis();
  }
}

// Arıza (donmuş sensör) tespiti: nefes-BME280'in okuduğu basınç NEFES_ARIZA_SURESI_MS
// boyunca (neredeyse) hiç değişmezse sensör arızalı kabul edilir. Değer tekrar
// kıpırdadığı an otomatik olarak "sağlıklı" durumuna geri döner (kalıcı/latch değildir) —
// böylece geçici bir donma da, gerçek bir arıza da manuel müdahale gerektirmeden kendini
// düzeltir/tespit eder.
void nefesArizaKontrol(float basinc) {
  if (nefesIlkOkuma) {
    nefesBasincSonDeger = basinc;
    nefesDegismemeBaslangic = millis();
    nefesIlkOkuma = false;
    return;
  }

  if (fabs(basinc - nefesBasincSonDeger) > NEFES_DEGISME_ESIK_HPA) {
    // Değer gerçekten kıpırdadı: donma sayacını sıfırla.
    nefesBasincSonDeger = basinc;
    nefesDegismemeBaslangic = millis();
  }

  bool eskiArizaDurumu = nefesSensorArizali;
  nefesSensorArizali = (millis() - nefesDegismemeBaslangic >= NEFES_ARIZA_SURESI_MS);

  if (nefesSensorArizali != eskiArizaDurumu) {
    if (nefesSensorArizali) {
      Serial.println("UYARI: Nefes-BMP280 uzun suredir degismiyor - sensor arizali kabul edildi, yedek (ortam-DHT22) kirlilik tespitine geciliyor.");
    } else {
      Serial.println("Nefes-BMP280 tekrar degisken okumalar veriyor - birincil kirlilik tespitine donuldu.");
    }
  }
}

// BME280 (üfleme ağzına yakın) — üfleme kanalı kirlilik/kalıntı tespiti için.
// I2C'de sensör hiç bulunamadıysa (bmeNefesHazir == false, bkz. setup()) okuma
// denemeden doğrudan arızalı kabul edilir; DHT11'in tersine bu durum init anında
// kesin olarak bilinir.
void nefesBmeGuncelle() {
  if (!bmeNefesHazir) {
    nefesSensorArizali = true;
    return;
  }

  if (millis() - sonNefesOkumaZamani < NEFES_BME280_OKUMA_ARALIGI_MS) return;
  sonNefesOkumaZamani = millis();

  float p = bmeNefes.readPressure() / 100.0f; // Pa -> hPa

  // Fiziksel olarak makul mü? Sensör VCC'si kesilip SDA/SCL/GND hâlâ bağlı kaldığında
  // (I2C pull-up'ları üzerinden parazitik beslenip) çip dijital olarak cevap vermeye
  // devam edebilir ama anlamsız (ör. negatif) değerler üretir — bu veri GÜRÜLTÜLÜ
  // olduğu için aşağıdaki nefesArizaKontrol()'deki "donma" testi bunu YAKALAYAMAZ.
  bool gecerliMi = !isnan(p) && p >= NEFES_BASINC_GECERLI_MIN_HPA && p <= NEFES_BASINC_GECERLI_MAKS_HPA;

  if (gecerliMi) {
    nefesBasinc = p;
    if (nefesBasincOrtalamaIlkOkuma) {
      // İlk gerçek okuma: EMA'yı sabit deniz seviyesi varsayımından değil,
      // gerçek yerel/anlık basınçtan başlat. Aksi halde gerçek basınç (irtifa/hava
      // durumuna göre 1013.25'ten kolayca birkaç-onlarca hPa uzak olabilir) ile
      // sabit varsayılan arasındaki fark, EMA hiç adapte olamadan (kirlilik VARKEN
      // dondurulduğu için) anında eşiği aşar ve kanalKirliMi kalıcı olarak
      // takılı kalır (sürekli "kanal temizleniyor" döngüsü).
      nefesBasincOrtalama = p;
      nefesBasincOrtalamaIlkOkuma = false;
    }
    nefesArizaKontrol(p);
    nefesKirlilikKontrol();

#if NEFES_BASINC_SERIAL_LOG
    // Test/kalibrasyon için canlı basınç çıktısı — KANAL_KIRLILIK_ESIK_HPA'yı
    // sahada ayarlarken üflerken "fark" sütununun ne kadar sıçradığını izleyin.
    // İşiniz bitince NEFES_BASINC_SERIAL_LOG'u 0 yapıp kapatabilirsiniz.
    static uint32_t sonNefesLogZamani = 0;
    if (millis() - sonNefesLogZamani >= 300) {
      sonNefesLogZamani = millis();
      Serial.printf("[NEFES-BMP280] basinc:%.2f hPa  ortalama:%.2f hPa  fark:%+.2f hPa  kirli:%s  arizali:%s\n",
                    nefesBasinc, nefesBasincOrtalama, nefesBasinc - nefesBasincOrtalama,
                    kanalKirliMi ? "EVET" : "hayir", nefesSensorArizali ? "EVET" : "hayir");
    }
#endif
  } else if (isnan(p)) {
    Serial.println("  (BMP280 nefes okuma hatasi - son gecerli deger korunuyor)");
  } else {
    // Okuma geldi ama fiziksel olarak imkansız (menzil disi) - sensor muhtemelen
    // dogru baglanmamis/arizali. Bu tick icin gecersiz sayilir (latch degil,
    // gecerli bir okuma gelir gelmez kendiliginden duzelir).
    nefesSensorArizali = true;
    Serial.printf("  (BMP280 nefes okumasi fiziksel olarak imkansiz: %.2f hPa - sensor muhtemelen dogru baglanmamis/arizali)\n", p);
  }
}

/* -------------------------------------------------------------------------- */
/*  Ham sensör direnci (Rs) ölçümü                                             */
/* -------------------------------------------------------------------------- */
// Oversampling ile ADC okur ve MQ-3'ün anlık direncini (Rs) döndürür.
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
  // varsayılıyor. Ölçüm anındaki ortamSicaklik/ortamNem (artık DHT22'den) bundan
  // farklıysa, datasheet grafiğinden çıkarılan oran kadar ratio düzeltilir.
  float referansKatsayi = nemSicaklikKatsayisi(KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF);
  float anlikKatsayi    = nemSicaklikKatsayisi(ortamSicaklik, ortamNem);
  if (anlikKatsayi < 0.01f) anlikKatsayi = 0.01f; // güvenlik
  float duzeltmeFaktoru = referansKatsayi / anlikKatsayi;
  ratio *= duzeltmeFaktoru;

  if (ratio < 0.01) ratio = 0.01; // pow() için aşırı uç değerlerde stabilite

  // --- MQ-3 datasheet Fig.1 "Alcohol" eğrisinden türetilmiş güç fonksiyonu ---
  // NOT: R0 artık 60'a bölünmüş ölçekte tutuluyor (bkz. R0 tanımı), yani
  // ratio = Rs/R0 eskisinin 60 katı büyük çıkıyor. Aynı fiziksel Rs için
  // mg_L'nin değişmemesi için katsayı buna göre yeniden ölçeklendi:
  // a_yeni = a_eski * 60^1.4941 = 0.000888 * 453.70 ≈ 0.402886
  float mg_L = 0.402886 * pow(ratio, -1.4941);
  if (mg_L < 0) mg_L = 0;

  return mg_L * 2.1; // promil
}

/* -------------------------------------------------------------------------- */
/*  Kalıcı depolama (NVS) — R0 ve referans koşullarını yükle                   */
/* -------------------------------------------------------------------------- */
// Açılışta çağrılır. NVS'de kayıtlı bir değer varsa onu kullanır, yoksa
// yukarıdaki varsayılan (hardcoded) değerler geçerli kalır.
// NOT: R0_KALIBRASYON durumu (bkz. r0KalibrasyonuBitir()) artık sahada gerçekten
// YAZIYOR — bu fonksiyon açılışta o kayıtlı değerleri (ve kaç ölçüm yapıldığını)
// geri okuyor. R0 kalibrasyonu artık SADECE manuel (10sn buton basılı tutma) ile
// tetiklenir; olcumSayaci otomatik bir tetikleyici DEĞİL, sadece bilgi/izleme
// amaçlı NVS'de tutulmaya devam ediyor.
void r0veReferansiYukle() {
  prefs.begin(NVS_NAMESPACE, true); // salt-okunur aç
  R0                       = prefs.getFloat("r0",   R0);
  KALIBRASYON_SICAKLIK_REF = prefs.getFloat("tRef", KALIBRASYON_SICAKLIK_REF);
  KALIBRASYON_NEM_REF      = prefs.getFloat("hRef", KALIBRASYON_NEM_REF);
  olcumSayaci              = prefs.getInt("olcumSayac", 0);
  prefs.end();

  Serial.printf("Yuklenen kalibrasyon -> R0:%.2f  Referans:%.1fC / %%%.1f  OlcumSayaci:%d\n",
                R0, KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF, olcumSayaci);
}

/* -------------------------------------------------------------------------- */
/*  Havalandırma fanı kontrolü                                                 */
/* -------------------------------------------------------------------------- */
// SONUC durumunun cooldown'ı boyunca VEYA KANAL_TEMIZLE durumunda çalışır.
void fanBaslat() {
  digitalWrite(FAN_PIN, HIGH);
  fanAktif = true;
  Serial.println("Fan acildi");
}

void fanDurdur() {
  digitalWrite(FAN_PIN, LOW);
  fanAktif = false;
  Serial.println("Fan kapandi");
}

/* -------------------------------------------------------------------------- */
/*  Fan ikonu döndürme animasyonu (ekran11, uic_image3)                         */
/* -------------------------------------------------------------------------- */
// Fiziksel fan (FAN_PIN) zaten OLCUM bitince açılıyor; bu sadece ekran11'deki fan
// PNG'sini o sırada döndürerek "fan çalışıyor" hissi veren GÖRSEL bir animasyon.
static void fanRotasyonSetter(void * obj, int32_t v) {
  lv_image_set_rotation((lv_obj_t *)obj, v);
}

void fanAnimasyonuBaslat() {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, uic_image3);
  lv_anim_set_exec_cb(&a, fanRotasyonSetter);
  lv_anim_set_values(&a, 0, 3600); // lv_image_set_rotation 0.1 derece biriminde (3600 = tam tur)
  lv_anim_set_time(&a, 1200);      // 1.2 sn'de bir tam tur
  lv_anim_set_path_cb(&a, lv_anim_path_linear); // sabit hız
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

void fanAnimasyonuDurdur() {
  lv_anim_delete(uic_image3, fanRotasyonSetter);
  lv_image_set_rotation(uic_image3, 0); // bir sonraki gösterim için sıfırla
}

/* -------------------------------------------------------------------------- */
/*  Gerçek kanal temizlemeye geçiş (ekran11: fan + geri sayım + uic_fantext)   */
/* -------------------------------------------------------------------------- */
// KANAL_TEMIZLE'ye giren 4 farklı yerin (BEKLE, TABAN_OLC, SONUC cooldown
// bitişi, R0_KALIBRASYON) ortak yolu. mesaj, ekran11'deki uic_fantext'e
// yazılır — çağıran yer, tetikleyiciye uygun metni seçer: SONUC (gerçek
// ölçüm sonrası, kanal hâlâ kirliyse) için "Kanal temizleniyor"; diğer üçü
// (BEKLE/TABAN_OLC/R0 — hepsi butona bağlı olmayan/kendiliğinden algılanan
// kirlilik, gerçek bir ölçüm sonucu YOK) için "Yanlış üfleme algılandı".
// NOT: LED_TEMIZLEME (mor) SADECE bu fonksiyondan geçilen KANAL_TEMIZLE
// durumunda yanar — SONUC (yeşil/kırmızı) ekrana ekran2'de kalır, ekran11'e
// hiç uğramaz, bkz. LED_DAVRANIS_NOTU.md.
static void kanalTemizlemeyeGec(const char* mesaj) {
  fanBaslat();
  kanalTemizlemeBaslangic = millis();
  lv_label_set_text(uic_fantext, mesaj);
  fanAnimasyonuBaslat();
  fanGeriSayimSonSaniye = KANAL_TEMIZLEME_SURESI_MS / 1000;
  {
    char sayimBuf[8];
    snprintf(sayimBuf, sizeof(sayimBuf), "%d", fanGeriSayimSonSaniye);
    lv_label_set_text(uic_fangerisayma, sayimBuf);
  }
  _ui_screen_change(&ui_ekran11,
                    LV_SCR_LOAD_ANIM_MOVE_LEFT,
                    300,
                    0,
                    ui_ekran11_screen_init);
  sesCal(22); // "Cihaz temizleniyor."
  durum = KANAL_TEMIZLE;
}

/* -------------------------------------------------------------------------- */
/*  "HAZIR" mesajı (ekran1, uic_hazir) — geçici, 3 sn sonra kaybolur           */
/* -------------------------------------------------------------------------- */
// ekran11 (temizlik) veya ekran4'ten (ısınma) ekran1'e her dönüşte çağrılır.
// Kalıcı olarak açık bırakmak yerine 3 sn gösterip kendiliğinden temizlenir
// (bkz. alkolmetre_timer_cb sonundaki bağımsız kontrol).
uint32_t hazirGosterimBaslangic = 0;
bool     hazirGosteriliyor = false;

void hazirMesajiniGoster() {
  lv_label_set_text(uic_hazir, "HAZIR");
  hazirGosterimBaslangic = millis();
  hazirGosteriliyor = true;
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
/*  LED durum göstergesi — ayrı, sık çalışan LVGL timer'ı                      */
/* -------------------------------------------------------------------------- */
// Akıcı animasyon (kayan/nefes/pulse) için 100ms'lik ana sensör timer'ı yeterince
// sık değil; bu yüzden ledServis() ayrı ve daha sık (~30ms) bir LVGL timer'ından
// çağrılıyor. lv_timer_create sadece lv_timer_t* parametreli callback kabul ettiği
// için ledServis()'i saran ince bir sarmalayıcı gerekiyor.
static void ledServis_cb(lv_timer_t * timer) {
  ledServis();
}

// Durum makinesinin (aşağıdaki `durum` değişkeni) o anki durumunu LED gösterge
// durumuna çevirir. ledDurumAyarla() aynı hedef tekrar verilirse hiçbir şey
// yapmadığı (idempotent) için bu fonksiyon her sensör turunda güvenle çağrılabilir;
// bu, her `durum = X;` satırının yanına ayrı ayrı çağrı eklemekten daha az hataya açık.
static void ledDurumGuncelle() {
  switch (durum) {
    case BEKLE:          ledDurumAyarla(LED_BEKLE); break;
    case TABAN_OLC:       ledDurumAyarla(LED_BEKLE); break;
    // "Sensör bekleme" (henüz üfleme algılanmadı) = LED_BEKLE devam ediyor;
    // "analiz kısmı" (üfleme algılandı, 6sn'lik pencere) = LED_UFLEME (ilerleyen
    // dolum animasyonu, bkz. led.cpp ANIM_DOLU_ILERLEME). Bu geçiş,
    // ledDurumAyarla(LED_UFLEME) TAM analizYaziGosterildi=true olduğu tikte ilk
    // kez tetiklenir — led.cpp'nin kendi faz sayacı bu yüzden ui.ino'nun
    // analizYaziZamani'sıyla aynı anda başlar (bkz. led.cpp).
    case OLCUM:          ledDurumAyarla(analizYaziGosterildi ? LED_UFLEME : LED_BEKLE); break;
    case SONUC:          ledDurumAyarla(olcumPeakPromil >= YASAL_SINIR_PROMIL ? LED_SONUC_YUKSEK : LED_SONUC_GUVENLI); break;
    case KANAL_TEMIZLE:  ledDurumAyarla(LED_TEMIZLEME); break;
    case R0_KALIBRASYON: ledDurumAyarla(LED_ANALIZ); break;
    case R0_KAL_BASARISIZ_MESAJ: ledDurumAyarla(LED_ANALIZ); break;
  }
}

/* -------------------------------------------------------------------------- */
/*  R0 kalibrasyonu başlat/bitir                                               */
/* -------------------------------------------------------------------------- */
// Sadece manuel (BEKLE'de 10sn kesintisiz buton basılı tutma) tetikleyiciden
// çağrılır. Fan ile purge fazını başlatıp R0_KALIBRASYON durumuna geçer.
void r0KalibrasyonuBaslat() {
  if (fanAktif) fanDurdur(); // temiz başlangıç; purge fazı kendi fanını açacak
  fanBaslat();
  r0KalFaz = R0KAL_PURGE;
  r0KalFazBaslangic = millis();
  r0KalGenelBaslangic = millis();
  r0KalSonOrnekZamani = 0;
  r0KalPencereSayac = 0;
  r0KalPencereIndex = 0;
  r0KalTemizlemeSonrasiMesajGoster = false;
  _ui_screen_change(&ui_ekran4,
                    LV_SCR_LOAD_ANIM_MOVE_LEFT,
                    300,
                    0,
                    ui_ekran4_screen_init);
  sesCal(29); // "R0 kalibrasyonu yapılıyor, lütfen üflemeyin"
  durum = R0_KALIBRASYON;
}

// basarili=true ise R0 (ve o anki ortam koşulları referans olarak) NVS'ye yazılır
// ve sayaç sıfırlanır. false ise (zaman aşımı veya üfleme tespit edilip iptal
// edildiyse) eski R0 korunur — R0 kalibrasyonu artık sadece manuel (10sn buton
// basılı tutma) tetiklendiğinden, bir sonraki deneme de kullanıcının inisiyatifine
// kalır (otomatik yeniden deneme YOK).
void r0KalibrasyonuBitir(bool basarili) {
  fanDurdur();
  sonAktiviteZamani = millis();

  if (basarili) {
    olcumSayaci = 0;
    KALIBRASYON_SICAKLIK_REF = ortamSicaklik;
    KALIBRASYON_NEM_REF      = ortamNem;

    prefs.begin(NVS_NAMESPACE, false); // yazılabilir aç
    prefs.putFloat("r0", R0);
    prefs.putFloat("tRef", KALIBRASYON_SICAKLIK_REF);
    prefs.putFloat("hRef", KALIBRASYON_NEM_REF);
    prefs.putInt("olcumSayac", 0);
    prefs.end();

    Serial.printf("R0 kalibrasyonu basarili -> yeni R0:%.2f Referans:%.1fC / %%%.1f\n",
                  R0, KALIBRASYON_SICAKLIK_REF, KALIBRASYON_NEM_REF);
  } else {
    Serial.println("R0 kalibrasyonu iptal/basarisiz - eski R0 korunuyor, sonraki uygun anda tekrar denenecek.");
  }

  _ui_screen_change(&ui_ekran1,
                    LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                    300,
                    0,
                    ui_ekran1_screen_init);
  hazirMesajiniGoster();
  sesCal(1); // "Cihaz hazır, lütfen üfleyin"
  durum = BEKLE;
}

/* -------------------------------------------------------------------------- */
/*  LVGL Timer Callback – Sensör okuma ve durum makinesi                       */
/* -------------------------------------------------------------------------- */

static const uint32_t SENSOR_TIMER_MS = 100;  // 100ms aralıklarla sensör oku

void alkolmetre_timer_cb(lv_timer_t * timer) {
  // Sıcaklık/nem/basınç cache'lerini güncelle (her ikisi de kendi aralığında okur)
  dhtOrtamGuncelle();
  nefesBmeGuncelle();

  // Buton okuma (debounce edilmiş)
  butonDebounceGuncelle();

  // Ham promil oku (yeni güç fonksiyonu formülü + oversampling içeride yapılıyor)
  float hamPromil = hamPromilOlcVeDondur();

  // Yedek kirlilik tespiti: MQ-3'ün kendi ADC okuması üzerinden. OLCUM sirasinda
  // calistirilmaz, cunku o an MQ-3'ün yuksek okumasi zaten beklenen/istenen bir
  // seydir (gercek ufleme).
  if (durum != OLCUM) {
    mq3KirlilikKontrol(hamPromil);
    mq3OrtalamaGuncelle(hamPromil); // BEKLE'nin butonsuzKirlilikTespitEdildiMi() kontrolü için kayan 10sn ortalaması
  }

  // Efektif kirlilik bayrağı: nefes sensörü sağlıklıysa onun sonucu, arızalı
  // (uzun süredir donmuş) sayıldıysa yedek (MQ-3 ADC) tabanlı sonuç kullanılır.
  kanalKirliMiEfektif = nefesSensorArizali ? mq3KirliMi : kanalKirliMi;

  // Taban (baseline) çıkarma: TABAN_OLC durumunda en son ölçülen değeri çıkar
  float promil = hamPromil - tabanPromil;
  if (promil < 0) promil = 0;

  // --- TEST İÇİN canlı Serial çıktısı ---
  //Serial.printf("ham:%.3f taban:%.3f promil:%.3f\n", hamPromil, tabanPromil, promil);

  // Durum makinesi
  switch (durum)
  {
    case BEKLE:
      // 500-ölçüm hatırlatması: uic_arizamesajtext'in metni SABİT "KOD-1"
      // (lv_label_set_text hiç çağrılmıyor) — sadece görünürlüğü değişir.
      // BEKLE'nin HER tikinde değerlendirilir (idempotent LVGL flag çağrıları,
      // her tikte çağrılması sorun değil). r0KalibrasyonuBitir(true)
      // olcumSayaci'yi 0'a resetlediğinde bir sonraki BEKLE tikinde otomatik
      // olarak tekrar gizlenir.
      if (olcumSayaci >= 500) {
        lv_obj_remove_flag(uic_arizamesajtext, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(uic_arizamesajtext, LV_OBJ_FLAG_HIDDEN);
      }

      if (butonsuzKirlilikTespitEdildiMi())
      {
        // Buton basılmasa bile kendiliğinden kirlilik tespit edildi: BMP280
        // son 10sn içinde en az bir kez üfleme algıladı VEYA MQ-3'ün son
        // 10sn'lik ortalaması eşiği aşıyor (butonsuzKirlilikTespitEdildiMi())
        // — otomatik olarak kanalı temizlemeye geç.
        kanalTemizlemeyeGec("olcum oncesi kirlilik algilandi lutfen temizlenene kadar bekleyiniz");
      }
      else if (olcumBasladi && !oncekiButonDurumu)
      {
        // Yeni buton basımı (aşağı kenar): HENÜZ hiçbir işlem yapma — kısa bir
        // tıklama mı yoksa 10sn'lik manuel R0 kalibrasyon basması mı olduğu
        // ancak basılı tutma süresine bakılarak ayırt edilebilir.
        butonBasiliBaslangic = millis();
        uzunBasmaIslendi = false;
      }
      else if (olcumBasladi && oncekiButonDurumu && !uzunBasmaIslendi
               && millis() - butonBasiliBaslangic >= UZUN_BASMA_SURESI_MS)
      {
        // Buton kesintisiz 10sn basılı tutuldu: manuel R0 kalibrasyonuna gir
        // (bırakılmasını beklemeden, eşiği geçer geçmez tetiklenir).
        uzunBasmaIslendi = true;
        sonAktiviteZamani = millis();
        r0KalibrasyonuBaslat();
      }
      else if (!olcumBasladi && oncekiButonDurumu && !uzunBasmaIslendi)
      {
        // Buton bırakıldı ve 10sn dolmadan bırakıldı: kısa basma = normal ölçüm.
        if (fanAktif) fanDurdur(); // güvenlik: yeni ölçüm başlarken fan kesin kapalı olsun
        tabanOlcumToplami = 0.0f;
        tabanOlcumSayac = 0;

        // Ekran, buton bırakılır bırakılmaz promil ekranına döner; taban ölçümü
        // (~1.5sn) sırasında henüz anlamlı bir değer olmadığı için "0.00" gösterilir,
        // gerçek canlı değerler OLCUM başlayınca akmaya başlar. Metin rengi, o an
        // LED'in gösterdiği renkle (LED_UFLEME = turuncu, bkz. led.cpp RENK_TURUNCU)
        // aynı olacak şekilde ayarlanıyor.
        lv_label_set_text(uic_deger, "0.00");
        lv_obj_set_style_text_color(uic_deger, lv_color_hex(0xFF5A00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(uic_olcumtext, "10 saniye içerisinde üfleyiniz");
        lv_obj_add_flag(uic_Label5, LV_OBJ_FLAG_HIDDEN); // "ÖLÇÜM SONUCU" başlığı yeni ölçümde tekrar gizlensin
        _ui_screen_change(&ui_ekran2,
                          LV_SCR_LOAD_ANIM_MOVE_LEFT,
                          300,
                          0,
                          ui_ekran2_screen_init);

        sonAktiviteZamani = millis();
        durum = TABAN_OLC;
      }
      break;

    case TABAN_OLC:
    {
      if (kanalKirliMiEfektif)
      {
        // Taban ölçümü sırasında kirlilik tespit edildi: kirli havayı taban olarak
        // kaydetmek gerçek sonucu bastırıp yanlış-negatife yol açabilir (güvenlik
        // açısından ciddi) — bu yüzden ölçüm iptal edilip kanal temizlemeye geçilir.
        kanalTemizlemeyeGec("Yanlis ufleme algilandi, lutfen cihaz temizlenene kadar bekleyiniz");
        break;
      }

      tabanOlcumToplami += hamPromil;
      tabanOlcumSayac++;

      if (tabanOlcumSayac >= TABAN_ORNEK_SAYISI)
      {
        tabanPromil = tabanOlcumToplami / tabanOlcumSayac;
        olcumPeakPromil = 0.0f;
        ardisikSayac = 0;
        uflemeTetikMq3ArdisikSayac = 0;
        olcumBaslangicZamani = millis();
        olcumOrtaSesiCalindi = false;
        analizYaziGosterildi = false;
        sesCal(2); // "Üfleme yapabilirsiniz"
        // "Analiz yapiliyor" yazısı/sesi artık burada değil — gerçek üfleme
        // algılanınca (OLCUM içinde, aşağıda) bir kez tetiklenecek.
        durum = OLCUM;
      }
      break;
    }

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

      // "Sensör bekleme" -> "analiz kısmı" geçiş tetikleyicisi (SADECE ne
      // zaman analizYaziGosterildi=true olacağını belirler — olcumPeakPromil,
      // ardisikSayac üzerinden AYNEN yukarıdaki gibi hesaplanmaya devam eder,
      // buradan ETKİLENMEZ). Öncelik: nefes-BMP280 sağlıklıysa onun zaten her
      // tikte hesaplanan basınç-sıçraması sinyali (kanalKirliMi); arızalıysa
      // MQ-3'ün kendi ham okumasına dayalı, buna özel yeni bir eşik/sayaç.
      if (!analizYaziGosterildi)
      {
        bool uflemeTetiklendiMi;
        if (!nefesSensorArizali)
        {
          uflemeTetiklendiMi = kanalKirliMi;
        }
        else
        {
          if (hamPromil > UFLEME_TETIK_MQ3_ESIK_PROMIL)
          {
            uflemeTetikMq3ArdisikSayac++;
          }
          else
          {
            uflemeTetikMq3ArdisikSayac = 0;
          }
          uflemeTetiklendiMi = (uflemeTetikMq3ArdisikSayac >= UFLEME_TETIK_ARDISIK_SAYI);
        }

        if (uflemeTetiklendiMi)
        {
          lv_label_set_text(uic_olcumtext, "Analiz yapiliyor");
          sesCal(26); // "Analiz yapılıyor"
          analizYaziGosterildi = true;
          analizYaziZamani = millis();
        }
      }

      // Yeterli sayıda ardışık okuma varsa gerçek üfleme kabul edilir, peak güncellenir.
      // KRİTİK: analizYaziGosterildi şartı olmadan bu blok "sensör bekleme"
      // fazında (BMP280/MQ-3 tetikleyicisi hiç ateşlenmeden) bile MQ-3'ün ham
      // promilini sessizce peak'e yazabiliyordu — "Analiz yapiliyor" ekranı/
      // sesi hiç görünmeden, guvenlikZamanAsimi ile sonuç ekranına gerçek bir
      // değer sızabiliyordu. Artık peak SADECE analiz kısmı gerçekten
      // başladıktan sonra biriktiriliyor.
      if (analizYaziGosterildi && ardisikSayac >= GEREKLI_ARDISIK_SAYI && promil > olcumPeakPromil)
      {
        olcumPeakPromil = promil;
      }

      // Ekranda canlı okuma: kullanıcı üflerken sayacın gerçek zamanlı değiştiğini görür.
      // Doğrulanmış peak DEĞİL, o anki ham promil gösterilir; final/doğrulanmış değer
      // ölçüm bitince (aşağıda) yazılıp sabitlenir. Eşik altındaki (gürültü sayılan)
      // değerler burada da 0 gösterilir — final peak-hold zaten aynı eşiğin altını
      // 0 saydığından, canlı gösterim sonuçla tutarlı kalır.
      {
        char canliBuf[16];
        float gosterilenPromil = (promil <= UFLEME_ESIK_PROMIL) ? 0.0f : promil;
        snprintf(canliBuf, sizeof(canliBuf), "%.2f", gosterilenPromil);
        lv_label_set_text(uic_deger, canliBuf);
      }

      // Üfleme algılandıktan sonraki 6sn'lik analiz penceresinin ortasında bir kez
      // sesli yönlendirme (sesCal kuyruklu olduğu için bir bayrakla korunuyor).
      if (analizYaziGosterildi)
      {
        uint32_t analizGecen = millis() - analizYaziZamani;
        if (!olcumOrtaSesiCalindi && analizGecen >= ANALIZ_OLCUM_SURESI_MS / 2)
        {
          sesCal(27); // "Üflemeye devam edin"
          olcumOrtaSesiCalindi = true;
        }
      }

      // Bitiş: ya üfleme algılandıktan 6sn sonra (normal), ya da hiç üfleme
      // algılanmadan 10sn dolduğunda (güvenlik zaman aşımı, peak=0 ile devam).
      bool analizTamam = analizYaziGosterildi && (millis() - analizYaziZamani >= ANALIZ_OLCUM_SURESI_MS);
      bool guvenlikZamanAsimi = !analizYaziGosterildi && (gecenSure >= OLCUM_SURESI_MS);
      if (analizTamam || guvenlikZamanAsimi)
      {
        // Doğrulanmış (gürültüden arındırılmış) peak değeri ekrana yaz ve sabitle
        // — artık canlı okuma değil, bu değer değişmeden kalacak. "Analiz
        // yapılıyor" yazısının kaldırılması ve ÖLÇÜM SONUCU başlığının
        // gösterilmesi de AYNI ANDA, peak değerle birlikte yapılır.
        char metinBuf[16];
        snprintf(metinBuf, sizeof(metinBuf), "%.2f", olcumPeakPromil);
        lv_label_set_text(uic_deger, metinBuf);
        lv_label_set_text(uic_olcumtext, "");
        lv_obj_remove_flag(uic_Label5, LV_OBJ_FLAG_HIDDEN);

        // Sonuç anonsu (sayı + durum) tam bu anda, sonuç ekrana yazılırken çalınır
        // — fan/temizlik ekranına geçişte değil.
        sesPromilSayiCal(olcumPeakPromil);  // sayı okunuşu (0.50 üzerinde atlanır)
        sesPromilDurumCal(olcumPeakPromil); // durum_normal / durum_ticari_sinir / durum_yasal_sinir

        // *** GEÇİCİ TEST: fan güç düşümü/parazit şüphesini test etmek için
        // burada KASITLI OLARAK DEVRE DIŞI BIRAKILDI. Test bitince bu satırı
        // geri aç:
        // fanBaslat();
        // (KANAL_TEMIZLE ve R0 kalibrasyonundaki fanBaslat() çağrılarına
        // dokunulmadı, onlar normal çalışmaya devam ediyor.)

        // Tamamlanan ölçüm sayısını kalıcı olarak say (eskiden ayrı ANALIZ
        // durumunda yapılıyordu; artık aynı anda, tek blokta — bir tetikleyici
        // DEĞİL, sadece bilgi/izleme amaçlı, R0 kalibrasyonu hâlâ sadece manuel).
        olcumSayaci++;
        wdtBesle(); // NVS yazımı öncesi besle: flash erase/write bazen uzun sürebilir
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putInt("olcumSayac", olcumSayaci);
        prefs.end();
        wdtBesle(); // NVS yazımı sonrası besle

        // "Sonuç gösterme" anı: LED (ledDurumGuncelle) ve değer metninin rengi
        // burada AYNI ANDA yeşile/kırmızıya döner. Ekran ui_ekran2'de KALIR —
        // ekran11'e (kanal temizleme) burada GEÇİLMEZ, sadece gerçek kanal
        // temizleme gerektiğinde (SONUC cooldown'ı bitince) devreye girer.
        bool yuksekSonuc = olcumPeakPromil >= YASAL_SINIR_PROMIL;
        lv_obj_set_style_text_color(uic_deger,
                                     yuksekSonuc ? lv_color_hex(0xFF0F00) : lv_color_hex(0x00FF3C),
                                     LV_PART_MAIN | LV_STATE_DEFAULT);

        sonucBaslangic = millis();
        durum = SONUC;
      }
      break;
    }

    case SONUC:
    {
      // 10 saniye ekran2'de (renkli sonuç değeriyle) sabit kalınır — bu süre
      // boyunca LED yeşil/kırmızı yanar. Fan havalandırma amacıyla bu sürenin
      // TAMAMI boyunca arka planda çalışmaya devam eder ama ekranda ayrıca
      // gösterilmez.
      uint32_t gecenSure = millis() - sonucBaslangic;

      if (gecenSure >= COOLDOWN_MS)
      {
        // Cooldown bitince kirlilik sensörüne bakılmaksızın HER ölçümden sonra
        // gerçek kanal temizlemeye geçilir (fan zaten çalışıyor) — ekran11'de
        // kalınıp mesaj/süre güncellenir, KANAL_TEMIZLE kendi sabit 10sn'i
        // sonunda BEKLE'ye döner.
        kanalTemizlemeyeGec("Kanal temizleniyor, lutfen bekleyiniz");
      }
      break;
    }

    case KANAL_TEMIZLE:
    {
      // Sabit süre sonunda KOŞULSUZ çıkılır (nem eşiği beklenmez — öngörülemez
      // kuruma süresi yerine bilinçli olarak sabit bir süre tercih edildi).
      uint32_t gecenSure = millis() - kanalTemizlemeBaslangic;
      uint32_t kalanMs = (gecenSure >= KANAL_TEMIZLEME_SURESI_MS) ? 0 : (KANAL_TEMIZLEME_SURESI_MS - gecenSure);
      int kalanSaniye = (kalanMs + 999) / 1000; // yukarı yuvarla: 10'dan 0'a düzgün insin

      if (kalanSaniye != fanGeriSayimSonSaniye)
      {
        char sayimBuf[8];
        snprintf(sayimBuf, sizeof(sayimBuf), "%d", kalanSaniye);
        lv_label_set_text(uic_fangerisayma, sayimBuf);
        fanGeriSayimSonSaniye = kalanSaniye;
      }

      if (gecenSure >= KANAL_TEMIZLEME_SURESI_MS)
      {
        fanDurdur();
        fanAnimasyonuDurdur();

        if (r0KalTemizlemeSonrasiMesajGoster)
        {
          // R0 kalibrasyonu ÖRNEKLE fazında kanal kirliliği (üfleme) yüzünden
          // iptal olmuştu: BEKLE'ye direkt dönmek yerine ekran4'te 3sn uyarı
          // gösterilir, sonra "HAZIR" yazısı GÖSTERİLMEDEN BEKLE'ye dönülür.
          r0KalTemizlemeSonrasiMesajGoster = false;
          lv_label_set_text(uic_kalibrasyontext, "KANAL TEMIZ DEGIL KALIBRASYON YAPILAMADI");
          _ui_screen_change(&ui_ekran4,
                            LV_SCR_LOAD_ANIM_MOVE_LEFT,
                            300,
                            0,
                            ui_ekran4_screen_init);
          r0KalBasarisizMesajBaslangic = millis();
          durum = R0_KAL_BASARISIZ_MESAJ;
        }
        else
        {
          _ui_screen_change(&ui_ekran1,
                            LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                            300,
                            0,
                            ui_ekran1_screen_init);
          hazirMesajiniGoster();
          sesCal(1); // "Cihaz hazır, lütfen üfleyin"
          durum = BEKLE;
        }
      }
      break;
    }

    case R0_KALIBRASYON:
    {
      // Genel üst sınır: hiçbir fazda sonsuz beklemeyelim (yaşlanmış bir sensör
      // hiç %2 toleransına oturmayabilir) — eski R0 korunarak vazgeçilir.
      if (millis() - r0KalGenelBaslangic >= R0_KAL_TIMEOUT_MS)
      {
        Serial.println("R0 kalibrasyonu zaman asimina ugradi.");
        r0KalibrasyonuBitir(false);
        break;
      }

      if (r0KalFaz == R0KAL_PURGE)
      {
        if (millis() - r0KalFazBaslangic >= R0_KAL_PURGE_SURESI_MS)
        {
          fanDurdur();
          r0KalFaz = R0KAL_SETTLE;
          r0KalFazBaslangic = millis();
        }
      }
      else if (r0KalFaz == R0KAL_SETTLE)
      {
        if (millis() - r0KalFazBaslangic >= R0_KAL_SETTLE_SURESI_MS)
        {
          // Kanal kirlilik kontrolü kaldırıldı: SETTLE bitince kirliliğe
          // bakılmaksızın doğrudan örneklemeye geçilir (30sn'lik PURGE zaten
          // yeterli kabul ediliyor).
          r0KalFaz = R0KAL_ORNEKLE;
          r0KalFazBaslangic = millis();
          r0KalSonOrnekZamani = 0;
          r0KalPencereSayac = 0;
          r0KalPencereIndex = 0;
        }
      }
      else // R0KAL_ORNEKLE
      {
        if (millis() - r0KalSonOrnekZamani >= R0_KAL_ORNEK_ARALIGI_MS)
        {
          r0KalSonOrnekZamani = millis();
          float rsYeni = sensorRsOlcVeDondur();

          // Üfleme tespiti: pencerede zaten birkaç örnek varsa, ortalamaya göre
          // ani bir düşüş (Rs, alkol/nefesle düşer) kalibrasyonu bozar - iptal et.
          bool uflemeGeldi = false;
          if (r0KalPencereSayac >= 3)
          {
            float toplamOnceki = 0;
            for (int i = 0; i < r0KalPencereSayac; i++) toplamOnceki += r0KalPencere[i];
            float ortalamaOnceki = toplamOnceki / r0KalPencereSayac;
            if (rsYeni < ortalamaOnceki * (1.0f - R0_KAL_UFLEME_ORANI)) uflemeGeldi = true;
          }

          if (uflemeGeldi)
          {
            Serial.println("R0 kalibrasyonu iptal: ufleme tespit edildi, kanal temizlemeye geciliyor.");
            r0KalTemizlemeSonrasiMesajGoster = true;
            kanalTemizlemeyeGec("Yanlis ufleme algilandi, lutfen cihaz temizlenene kadar bekleyiniz");
            sonAktiviteZamani = millis();
          }
          else
          {
            r0KalPencere[r0KalPencereIndex] = rsYeni;
            r0KalPencereIndex = (r0KalPencereIndex + 1) % R0_KAL_PENCERE_BOYUTU;
            if (r0KalPencereSayac < R0_KAL_PENCERE_BOYUTU) r0KalPencereSayac++;

            if (r0KalPencereSayac >= R0_KAL_PENCERE_BOYUTU)
            {
              float maks = r0KalPencere[0], minv = r0KalPencere[0], toplam = 0;
              for (int i = 0; i < R0_KAL_PENCERE_BOYUTU; i++)
              {
                float v = r0KalPencere[i];
                toplam += v;
                if (v > maks) maks = v;
                if (v < minv) minv = v;
              }
              float ortalama = toplam / R0_KAL_PENCERE_BOYUTU;

              if (ortalama > 0.0f && (maks - minv) / ortalama < R0_KAL_TOLERANS)
              {
                // Ham Rs ortalaması 60'a bölünüyor: mg_L formülündeki katsayı (0.402886)
                // R0'ın "gerçek temiz hava Rs / 60" ölçeğinde olmasını varsayıyor
                // (bkz. hamPromilOlcVeDondur() içindeki a_yeni = a_eski * 60^1.4941 notu).
                // Bölünmezse R0 60 kat büyük çıkar, ratio 60 kat küçülür, negatif üs
                // yüzünden mg_L ~60^1.4941 (~454) kat şişer.
                R0 = ortalama / 60.0f;
                r0KalibrasyonuBitir(true);
              }
              // Oturmadıysa dairesel pencere kaymaya devam eder; her yeni örnekte
              // tekrar kontrol edilir. Genel timeout zaten yukarıda sınırlı.
            }
          }
        }
      }
      break;
    }

    case R0_KAL_BASARISIZ_MESAJ:
    {
      // Ekran4'te "KANAL TEMIZ DEGIL KALIBRASYON YAPILAMADI" uyarısı sabit
      // 3sn gösterilir, sonra "HAZIR" yazısı GÖSTERİLMEDEN BEKLE'ye dönülür.
      if (millis() - r0KalBasarisizMesajBaslangic >= 3000)
      {
        _ui_screen_change(&ui_ekran1,
                          LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                          300,
                          0,
                          ui_ekran1_screen_init);
        durum = BEKLE;
      }
      break;
    }
  }

  ledDurumGuncelle();

  // "HAZIR" mesajı geçici: 3sn sonra kendiliğinden temizlenir (durumdan bağımsız,
  // hangi durumda olursak olalım kontrol edilir).
  if (hazirGosteriliyor && millis() - hazirGosterimBaslangic >= 3000)
  {
    lv_label_set_text(uic_hazir, "");
    hazirGosteriliyor = false;
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

  // --- Buton pinini, fan pinini ve sensörleri en başta hazırla ---
  pinMode(BUTON_PIN, INPUT_PULLUP);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // fan kapalı başlar

  Wire.begin(NEFES_I2C_SDA_PIN, NEFES_I2C_SCL_PIN);
  uint8_t nefesBmeAdresi = NEFES_BME280_I2C_ADRESI;
  bmeNefesHazir = bmeNefes.begin(nefesBmeAdresi);
  if (!bmeNefesHazir) {
    // Bazı modüller 0x77 adresinde olabilir; ikinci adres olarak onu da dene.
    nefesBmeAdresi = 0x77;
    bmeNefesHazir = bmeNefes.begin(nefesBmeAdresi);
  }
  if (bmeNefesHazir) {
    // sensorID: BME280 -> 0x60, BMP280 -> 0x58, BME680 -> 0x61 (yanlis modul takildiysa fark edilsin)
    Serial.printf("Nefes-BMP280 bulundu ve baslatildi (I2C adresi: 0x%02X, sensorID: 0x%02X)\n",
                  nefesBmeAdresi, bmeNefes.sensorID());
  } else {
    Serial.println("HATA: Nefes-BMP280 I2C'de bulunamadi (0x76 ve 0x77 denendi) - sensor arizali kabul edilip yedek (ortam-DHT22) kirlilik tespitine gecilecek.");
  }
  dhtOrtam.begin();
  delay(1500); // sensörlerin ilk kararlı okumayı verebilmesi için kısa bekleme

  // --- NVS'den kayıtlı R0 ve kalibrasyon referansını yükle (varsa) ---
  r0veReferansiYukle();

  // --- Ses modülünü başlat (DFPlayer Mini + UART2) ---
  sesBaslat();

  // --- LVGL / ekran / dokunmatik başlatma ---
  // NOT: Isınma sırasında ekranda bilgilendirme gösterebilmek için LVGL ve arayüz
  // bu adımdan ÖNCE başlatılıyor.
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

  // --- LED durum göstergesini başlat ---
  // Ana 100ms'lik sensör timer'ı akıcı animasyon için yeterince sık olmadığından,
  // LED'e özel ayrı ve daha sık (~30ms) bir LVGL timer'ı kuruluyor.
  ledBaslat();
  lv_timer_create(ledServis_cb, 30, NULL);

  // --- Önbellek ısıtma: sensör timer'ı başlamadan önce hem DHT22'nin hem de
  // BME280'in gerçek okumalarla dolu olduğundan emin ol (kanalKirliMi sıfır/varsayılan
  // veriyle değil güncel veriyle değerlendirilsin; nefesBasincOrtalama gerçek ortam
  // basıncına otursun). tabanPromil için ayrıca bir açılış ölçümü GEREKMİYOR —
  // OLCUM'a girmeden önce her zaman TABAN_OLC tarafından tazelenecek.
  {
    uint32_t onbellekBaslangic = millis();
    while (millis() - onbellekBaslangic < 2000) {
      dhtOrtamGuncelle();
      nefesBmeGuncelle();
      lv_timer_handler();
      sesServis();
      wdtBesle(); // ~2 sn suren bu dongu boyunca watchdog'u besle
      delay(100);
    }
  }

  _ui_screen_change(&ui_ekran1, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, ui_ekran1_screen_init);
  hazirMesajiniGoster(); // "HAZIR" 3sn göster, sonra kendiliğinden kaybolur

  // Boşta-kalma sayacı (otomatik R0 kalibrasyon tetikleyicisi) şimdiden başlasın —
  // cihaz daha ısınma/önbellek doldurma bitmeden "boşta" sayılmasın.
  sonAktiviteZamani = millis();

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
  sesServis();                            // Ses kuyruğunu servis et (bloklamaz)
  delay(next_ms < 1 ? 1 : next_ms);      // Bir sonraki timer'a kadar bekle
}
