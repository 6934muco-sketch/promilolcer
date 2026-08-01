#include "led.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// Donanım: adreslenebilir LED şeridi/halka (WS2812/SK6812 ailesi, tek veri hattı).
// LED_PIN şu an projede boşta olan bir GPIO; LED_COUNT gerçek şerit/halka uzunluğuna
// göre güncellenmeli. NOT: WS2812 tipi LED'ler ESP32'nin 3.3V pininden DEĞİL, ayrı
// bir 5V hattından beslenmeli (ortak GND ile) — çok sayıda LED tam parlaklıkta
// ciddi akım çekebilir.
static const uint8_t  LED_PIN   = 33;
static const uint16_t LED_COUNT = 15;

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

struct LedRenk { uint8_t r, g, b; };

// "led ve fan coding.md" — Renk Anlamları bölümü
static const LedRenk RENK_MAVI    = {0,   60,  255};
static const LedRenk RENK_TURUNCU = {255, 90,  0};
static const LedRenk RENK_CYAN    = {0,   220, 255};
static const LedRenk RENK_YESIL   = {0,   255, 60};
static const LedRenk RENK_KIRMIZI = {255, 15,  0};
static const LedRenk RENK_MOR     = {160, 0,   255};

enum LedAnimasyon {
  ANIM_NEFES,          // yavaş parlaklık nefesi, tüm LED'ler aynı renk (BEKLE)
  ANIM_DOLU_ILERLEME,  // 1. LED'den başlayıp UFLEME_DOLMA_SURESI_MS'de LED_COUNT'a ilerleyerek dolar (ÜFLEME/analiz kısmı)
  ANIM_PULSE_MERKEZ,   // ortadan dışarı doğru yayılan halka (ANALIZ)
  ANIM_DOLU_SABIT,     // soldan sağa dolar, sonra sabit yanar (SONUÇ - güvenli)
  ANIM_DOLU_NEFES,     // dolar, sonra nefes efektiyle yanmaya devam eder (SONUÇ - yüksek)
  ANIM_HIZLI_KAYAN     // soldan sağa kayan 3 LED'lik iz, hızlı (KANAL TEMİZLEME)
};

struct LedProfil { LedRenk renk; LedAnimasyon anim; uint8_t parlaklikYuzde; };

// Sıra LedDurum enum'ıyla (led.h) birebir eşleşir.
static const LedProfil PROFILLER[] = {
  { RENK_MAVI,    ANIM_NEFES,          15 }, // LED_BEKLE
  { RENK_TURUNCU, ANIM_DOLU_ILERLEME,  40 }, // LED_UFLEME
  { RENK_CYAN,    ANIM_PULSE_MERKEZ,   60 }, // LED_ANALIZ
  { RENK_YESIL,   ANIM_DOLU_SABIT,     80 }, // LED_SONUC_GUVENLI
  { RENK_KIRMIZI, ANIM_DOLU_NEFES,     80 }, // LED_SONUC_YUKSEK
  { RENK_MOR,     ANIM_HIZLI_KAYAN,    40 }, // LED_TEMIZLEME
};

// Animasyon zamanlamaları (tek yerden ayarlanabilsin diye burada toplandı)
static const uint32_t NEFES_PERIYOT_MS    = 3000;
static const uint32_t DOLU_NEFES_PERIYOT_MS = 2500;
static const uint32_t DOLMA_SURESI_MS     = 500;
static const uint32_t UFLEME_DOLMA_SURESI_MS = 6000; // led.cpp bağımsız çalışır; bu değer ui.ino'daki ANALIZ_OLCUM_SURESI_MS (6000ms) ile MANUEL senkron tutulmalı — biri değişirse diğeri de güncellenmeli
static const uint32_t PULSE_PERIYOT_MS    = 1200;
static const uint32_t HIZLI_KAYAN_ADIM_MS = 60;
static const int      KAYAN_IZ_UZUNLUGU   = 3;

static LedDurum mevcutDurum = LED_BEKLE;
static LedDurum hedefDurum  = LED_BEKLE;

static const uint32_t FADE_SURESI_MS = 300; // doküman: renkler arası ~300ms fade
static uint32_t gecisBaslangic = 0;         // hedef durum değiştiği an; fade + animasyon faz referansı
static LedRenk  fadeBaslangicRengi = RENK_MAVI;

static uint8_t parlaklikUygula(uint8_t yuzde, float carpan) {
  float deger = (yuzde / 100.0f) * 255.0f * carpan;
  if (deger < 0) deger = 0;
  if (deger > 255) deger = 255;
  return (uint8_t)deger;
}

static LedRenk renkKaristir(LedRenk a, LedRenk b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  LedRenk sonuc;
  sonuc.r = (uint8_t)(a.r + (b.r - a.r) * t);
  sonuc.g = (uint8_t)(a.g + (b.g - a.g) * t);
  sonuc.b = (uint8_t)(a.b + (b.b - a.b) * t);
  return sonuc;
}

static LedRenk renkParlaklikUygula(LedRenk renk, uint8_t parlaklik) {
  LedRenk sonuc;
  sonuc.r = (uint8_t)((uint16_t)renk.r * parlaklik / 255);
  sonuc.g = (uint8_t)((uint16_t)renk.g * parlaklik / 255);
  sonuc.b = (uint8_t)((uint16_t)renk.b * parlaklik / 255);
  return sonuc;
}

static void tumunuAyniRengeBoya(LedRenk renk, uint8_t parlaklik) {
  LedRenk s = renkParlaklikUygula(renk, parlaklik);
  uint32_t paket = strip.Color(s.r, s.g, s.b);
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, paket);
  }
}

void ledBaslat() {
  strip.begin();
  strip.clear();
  strip.show();
  mevcutDurum = LED_BEKLE;
  hedefDurum = LED_BEKLE;
  gecisBaslangic = millis();
  fadeBaslangicRengi = PROFILLER[LED_BEKLE].renk;
}

void ledDurumAyarla(LedDurum hedef) {
  if (hedef == hedefDurum) return; // zaten bu hedefe gidiyor/geldi, tekrar tetikleme
  fadeBaslangicRengi = PROFILLER[mevcutDurum].renk; // geçiş, o an gösterilen renkten başlasın
  hedefDurum = hedef;
  gecisBaslangic = millis();
}

// Hedef profile göre, durum aktif olalı bu yana geçen süreye (faz) bakarak LED
// şeridini bir sonraki kareye günceller. Fade bittikten SONRA çağrılır (ledServis
// içinde), yani burada her zaman "hedef durumun kendi animasyonu" çizilir.
static void animasyonuCiz(const LedProfil &profil, uint32_t faz) {
  switch (profil.anim) {
    case ANIM_NEFES: {
      float t = (faz % NEFES_PERIYOT_MS) / (float)NEFES_PERIYOT_MS;
      float carpan = 0.25f + 0.75f * (0.5f + 0.5f * sinf(2.0f * PI * t));
      tumunuAyniRengeBoya(profil.renk, parlaklikUygula(profil.parlaklikYuzde, carpan));
      break;
    }

    case ANIM_HIZLI_KAYAN: {
      int kafa = (int)((faz / HIZLI_KAYAN_ADIM_MS) % LED_COUNT);
      strip.clear();
      for (int i = 0; i < KAYAN_IZ_UZUNLUGU; i++) {
        int idx = (kafa - i + LED_COUNT) % LED_COUNT;
        float carpan = 1.0f - (float)i / KAYAN_IZ_UZUNLUGU;
        LedRenk s = renkParlaklikUygula(profil.renk, parlaklikUygula(profil.parlaklikYuzde, carpan));
        strip.setPixelColor(idx, strip.Color(s.r, s.g, s.b));
      }
      break;
    }

    case ANIM_DOLU_ILERLEME: {
      // Tetiklendiği anda (faz=0) ilk LED HEMEN yanar ("ilk üfleme algılandığında
      // 1. LED yanar" gereksinimi), UFLEME_DOLMA_SURESI_MS sonunda tüm LED_COUNT
      // LED yanmış olur. Süre aşılırsa (faz > süre) tam dolu halde kalır (clamp).
      // ANIM_DOLU_SABIT'ten farkı: o faz=0'da 0 LED yakıyor, bu her zaman en az
      // 1 LED yakar.
      uint16_t doluSayisi;
      if (faz >= UFLEME_DOLMA_SURESI_MS) {
        doluSayisi = LED_COUNT;
      } else {
        doluSayisi = 1 + (uint16_t)((uint32_t)(LED_COUNT - 1) * faz / UFLEME_DOLMA_SURESI_MS);
      }
      LedRenk s = renkParlaklikUygula(profil.renk, parlaklikUygula(profil.parlaklikYuzde, 1.0f));
      uint32_t paket = strip.Color(s.r, s.g, s.b);
      strip.clear();
      for (uint16_t i = 0; i < doluSayisi; i++) {
        strip.setPixelColor(i, paket);
      }
      break;
    }

    case ANIM_PULSE_MERKEZ: {
      float merkez = (LED_COUNT - 1) / 2.0f;
      float yaricapMax = LED_COUNT / 2.0f + 1.0f;
      float t = (faz % PULSE_PERIYOT_MS) / (float)PULSE_PERIYOT_MS;
      float yaricap = t * yaricapMax;
      strip.clear();
      for (uint16_t i = 0; i < LED_COUNT; i++) {
        float mesafe = fabsf((float)i - merkez);
        float fark = fabsf(mesafe - yaricap);
        float carpan = (fark < 1.0f) ? (1.0f - fark) : 0.0f; // dar bir halka şeklinde parlar
        if (carpan <= 0.0f) continue;
        LedRenk s = renkParlaklikUygula(profil.renk, parlaklikUygula(profil.parlaklikYuzde, carpan));
        strip.setPixelColor(i, strip.Color(s.r, s.g, s.b));
      }
      break;
    }

    case ANIM_DOLU_SABIT: {
      uint16_t doluSayisi = (faz >= DOLMA_SURESI_MS)
                               ? LED_COUNT
                               : (uint16_t)((uint32_t)LED_COUNT * faz / DOLMA_SURESI_MS);
      LedRenk s = renkParlaklikUygula(profil.renk, parlaklikUygula(profil.parlaklikYuzde, 1.0f));
      uint32_t paket = strip.Color(s.r, s.g, s.b);
      strip.clear();
      for (uint16_t i = 0; i < doluSayisi; i++) {
        strip.setPixelColor(i, paket);
      }
      break;
    }

    case ANIM_DOLU_NEFES: {
      if (faz < DOLMA_SURESI_MS) {
        // Önce doküman gereği soldan sağa dolar (ANIM_DOLU_SABIT ile aynı mantık).
        uint16_t doluSayisi = (uint16_t)((uint32_t)LED_COUNT * faz / DOLMA_SURESI_MS);
        LedRenk s = renkParlaklikUygula(profil.renk, parlaklikUygula(profil.parlaklikYuzde, 1.0f));
        uint32_t paket = strip.Color(s.r, s.g, s.b);
        strip.clear();
        for (uint16_t i = 0; i < doluSayisi; i++) {
          strip.setPixelColor(i, paket);
        }
      } else {
        // Dolma bitti: artık nefes efektiyle yanmaya devam eder.
        uint32_t nefesFaz = faz - DOLMA_SURESI_MS;
        float t = (nefesFaz % DOLU_NEFES_PERIYOT_MS) / (float)DOLU_NEFES_PERIYOT_MS;
        float carpan = 0.4f + 0.6f * (0.5f + 0.5f * sinf(2.0f * PI * t));
        tumunuAyniRengeBoya(profil.renk, parlaklikUygula(profil.parlaklikYuzde, carpan));
      }
      break;
    }
  }
}

void ledServis() {
  uint32_t simdi = millis();
  uint32_t gecenSure = simdi - gecisBaslangic;

  if (mevcutDurum != hedefDurum) {
    if (gecenSure < FADE_SURESI_MS) {
      // Renk geçişi sürüyor: eski renkten hedef renge yumuşak geçiş, sabit dolgu.
      float t = gecenSure / (float)FADE_SURESI_MS;
      LedRenk ara = renkKaristir(fadeBaslangicRengi, PROFILLER[hedefDurum].renk, t);
      tumunuAyniRengeBoya(ara, parlaklikUygula(PROFILLER[hedefDurum].parlaklikYuzde, 1.0f));
      strip.show();
      return;
    }
    // Fade tamamlandı: hedef durum artık aktif durum oldu, animasyon faz sayacı sıfırlanır.
    mevcutDurum = hedefDurum;
    gecisBaslangic = simdi;
    gecenSure = 0;
  }

  animasyonuCiz(PROFILLER[mevcutDurum], gecenSure);
  strip.show();
}
