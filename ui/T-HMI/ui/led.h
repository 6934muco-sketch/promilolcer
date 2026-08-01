// LED durum göstergesi (adreslenebilir WS2812/NeoPixel şerit veya halka)
// "led ve fan coding.md" dokümanındaki renk/animasyon tasarımının uygulanması.
#ifndef LED_H
#define LED_H

#include <Arduino.h>

enum LedDurum {
  LED_BEKLE,          // mavi, yavaş nefes
  LED_UFLEME,         // turuncu, ilerleyen dolum (analiz kısmı başında 1. LED yanar, 6sn'de tam dolar)
  LED_ANALIZ,         // açık mavi (cyan), ortadan dışarı pulse
  LED_SONUC_GUVENLI,  // yeşil, dolar + sabit yanar
  LED_SONUC_YUKSEK,   // kırmızı, dolar + nefes efektiyle yanmaya devam eder
  LED_TEMIZLEME       // mor, hızlı kayan
};

// LED şeridini/halkasını başlatır. setup() içinde, lv_init()/ui_init() sonrasında
// (LED animasyon zamanlayıcısı kurulmadan hemen önce) bir kez çağrılmalı.
void ledBaslat();

// Hedef görsel durumu ayarlar. Aynı hedef tekrar verilirse hiçbir şey yapmaz
// (idempotent), bu yüzden her sensör turunda güvenle çağrılabilir. Durum değişince
// ~300ms'lik yumuşak renk geçişiyle yeni duruma geçilir.
void ledDurumAyarla(LedDurum hedef);

// millis() tabanlı, BLOKLAMAYAN bir animasyon karesi çizer ve strip.show() çağırır.
// Akıcı animasyon için sık çağrılmalı (bkz. ui.ino'daki ayrı, ~30ms'lik LVGL timer'ı).
void ledServis();

#endif
