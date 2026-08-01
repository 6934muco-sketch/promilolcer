// Sesli anons modülü (DFPlayer Mini + microSD)
// microSD kart düzeni:
//   "01" klasörü -> genel anons/yönlendirme cümleleri (3 haneli numaralı mp3'ler,
//                   001.mp3, 002.mp3, ... sesCal() ile bu klasörden çalınır).
//   "02" klasörü -> ölçüm sonucu anonsu:
//                     001.mp3..051.mp3 = "0.00"‰..."0.50"‰ arası tam sayı okunuşu
//                       (dosya no = round(promil*100)+1)
//                     052.mp3 = durum_normal       (promil < 0.25)
//                     053.mp3 = durum_ticari_sinir (0.25 <= promil <= 0.50)
//                     054.mp3 = durum_yasal_sinir  (promil > 0.50)
#ifndef SES_H
#define SES_H

#include <Arduino.h>

// DFPlayer Mini ve UART2'yi başlatır (GPIO16=RX, GPIO17=TX). setup() içinde bir kez çağrılır.
void sesBaslat();

// Verilen dosya numarasını ("01" klasöründen) çalma kuyruğuna ekler. Kuyruk doluysa
// sessizce yok sayılır.
void sesCal(uint8_t dosyaNo);

// Promil değerinin tam sayı okunuşunu ("02" klasörü, 001-051) kuyruğa ekler.
// 0.50'nin üzerindeyse hiçbir şey çalmaz (o aralık için sayı dosyası yok).
void sesPromilSayiCal(float promil);

// Promil değerine göre durum anonsunu ("02" klasörü, 052-054) kuyruğa ekler:
//   < 0.25 promil        -> durum_normal
//   0.25 - 0.50 promil   -> durum_ticari_sinir (0.50 dahil)
//   > 0.50 promil        -> durum_yasal_sinir
void sesPromilDurumCal(float promil);

// Kuyruktaki bir sonraki dosyayı, mevcut çalma bitince başlatır. Bloklamaz;
// hem loop() içinde hem de setup() içindeki bloklayan bekleme döngülerinde çağrılmalıdır.
void sesServis();

#endif
