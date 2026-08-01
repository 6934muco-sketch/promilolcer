#include "ses.h"
#include <DFRobotDFPlayerMini.h>
#include <math.h>

static const uint8_t SES_KLASOR_ANONS  = 1; // microSD karttaki "01" klasörü (genel cümleler)
static const uint8_t SES_KLASOR_SONUC  = 2; // microSD karttaki "02" klasörü (promil sayı + durum)

// "02" klasörü içindeki dosya numaraları
static const uint8_t SONUC_SAYI_MIN       = 1;   // 001.mp3 = "0.00"
static const uint8_t SONUC_SAYI_MAKS      = 51;  // 051.mp3 = "0.50"
static const uint8_t SONUC_DURUM_NORMAL   = 52;  // durum_normal.mp3
static const uint8_t SONUC_DURUM_TICARI   = 53;  // durum_ticari_sinir.mp3
static const uint8_t SONUC_DURUM_YASAL    = 54;  // durum_yasal_sinir.mp3

// "02" klasörünün durum eşikleri (promil)
static const float SONUC_TICARI_ESIK_PROMIL = 0.25f;
static const float SONUC_YASAL_ESIK_PROMIL  = 0.50f;

static const uint8_t SES_SES_SEVIYESI = 22; // 0-30 arası (DFPlayer Mini)
static const uint8_t SES_KUYRUK_BOYUTU = 16;

// ESP32 çekirdeğinde Serial2 (UART2) zaten global olarak tanımlıdır; ayrı bir
// HardwareSerial nesnesi oluşturmak aynı donanımı iki kez sarmalayıp çakışmaya
// yol açar, bu yüzden doğrudan Serial2 kullanılıyor.
static DFRobotDFPlayerMini dfPlayer;
static bool dfPlayerHazir = false;

struct SesKuyrukOgesi { uint8_t klasor; uint8_t dosyaNo; };

static SesKuyrukOgesi kuyruk[SES_KUYRUK_BOYUTU];
static uint8_t kuyrukBas = 0;
static uint8_t kuyrukSon = 0;
static bool birSeyCalisiyor = false;

static void sonrakiniCal() {
  if (kuyrukBas == kuyrukSon) {
    birSeyCalisiyor = false;
    return;
  }
  SesKuyrukOgesi oge = kuyruk[kuyrukBas];
  kuyrukBas = (kuyrukBas + 1) % SES_KUYRUK_BOYUTU;

  Serial.print("DFPlayer -> playFolder(");
  Serial.print(oge.klasor);
  Serial.print(", ");
  Serial.print(oge.dosyaNo);
  Serial.println(")");

  dfPlayer.playFolder(oge.klasor, oge.dosyaNo);
  birSeyCalisiyor = true;
}

static void kuyrugaEkle(uint8_t klasor, uint8_t dosyaNo) {
  if (!dfPlayerHazir) {
    Serial.println("kuyrugaEkle: DFPlayer hazir degil, komut yok sayildi.");
    return;
  }

  uint8_t sonrakiSon = (kuyrukSon + 1) % SES_KUYRUK_BOYUTU;
  if (sonrakiSon == kuyrukBas) {
    Serial.println("kuyrugaEkle: kuyruk dolu, komut yok sayildi.");
    return; // kuyruk dolu, sessizce yok say
  }

  kuyruk[kuyrukSon] = { klasor, dosyaNo };
  kuyrukSon = sonrakiSon;

  if (!birSeyCalisiyor) {
    sonrakiniCal();
  }
}

void sesBaslat() {
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17

  dfPlayerHazir = dfPlayer.begin(Serial2, /*isACK=*/true, /*doReset=*/true);
  if (dfPlayerHazir) {
    dfPlayer.volume(SES_SES_SEVIYESI);
    Serial.println("DFPlayer Mini hazir.");

    // Tani: kart gercekten bulundu mu, kok dizinde kac dosya goruyor, "02"
    // klasorunde kac dosya var. Beklenenden farkli/0 ise kart formati, klasor
    // adlandirmasi (2 haneli "01"/"02") veya dosya adlandirmasi (3 haneli
    // "001.mp3") hatali demektir.
    delay(200); // ardisik komutlar arasinda DFPlayer'a kisa nefes payi
    int toplamDosya = dfPlayer.readFileCounts(DFPLAYER_DEVICE_SD);
    Serial.print("SD karttaki toplam mp3 sayisi: ");
    Serial.println(toplamDosya);

    delay(200);
    int klasor02Dosya = dfPlayer.readFileCountsInFolder(SES_KLASOR_SONUC);
    Serial.print("\"02\" klasorundeki dosya sayisi (54 olmali): ");
    Serial.println(klasor02Dosya);
  } else {
    Serial.println("DFPlayer Mini baslatilamadi - baglantiyi/microSD karti kontrol edin.");
    Serial.print("Son hata kodu: ");
    Serial.println(dfPlayer.readType());
  }
}

void sesCal(uint8_t dosyaNo) {
  kuyrugaEkle(SES_KLASOR_ANONS, dosyaNo);
}

void sesPromilSayiCal(float promil) {
  if (promil < 0.0f) promil = 0.0f;
  if (promil > SONUC_YASAL_ESIK_PROMIL) return; // bu aralık için sayı dosyası yok

  long sayi = lroundf(promil * 100.0f) + SONUC_SAYI_MIN;
  if (sayi < SONUC_SAYI_MIN) sayi = SONUC_SAYI_MIN;
  if (sayi > SONUC_SAYI_MAKS) sayi = SONUC_SAYI_MAKS;

  kuyrugaEkle(SES_KLASOR_SONUC, (uint8_t)sayi);
}

void sesPromilDurumCal(float promil) {
  uint8_t dosyaNo;
  if (promil > SONUC_YASAL_ESIK_PROMIL) {
    // 0.50'nin kesin üzerinde: sayı zaten çalınmıyor (sesPromilSayiCal), tek
    // başına "yasal sınırların üstündesiniz" cümlesi yeterli.
    dosyaNo = SONUC_DURUM_YASAL;
  } else if (promil >= SONUC_TICARI_ESIK_PROMIL) {
    // 0.25 - 0.50 arası (0.50 dahil): sayı + "ticari sınır" anonsu.
    dosyaNo = SONUC_DURUM_TICARI;
  } else {
    dosyaNo = SONUC_DURUM_NORMAL;
  }
  kuyrugaEkle(SES_KLASOR_SONUC, dosyaNo);
}

// DFPlayer'dan gelen hata alt-kodunu okunabilir metne cevirir (bkz. DFRobotDFPlayerMini.h)
static const char* sesHataAdi(uint16_t kod) {
  switch (kod) {
    case Busy:            return "Busy (kart yok/okunamiyor)";
    case Sleeping:        return "Sleeping";
    case SerialWrongStack: return "SerialWrongStack (haberlesme hatasi)";
    case CheckSumNotMatch: return "CheckSumNotMatch (haberlesme hatasi)";
    case FileIndexOut:    return "FileIndexOut (dosya/klasor bulunamadi - numaralandirmayi kontrol et)";
    case FileMismatch:    return "FileMismatch (dosya uyumsuz)";
    case Advertise:       return "Advertise";
    default:              return "bilinmeyen";
  }
}

void sesServis() {
  if (!dfPlayerHazir) return;

  if (dfPlayer.available()) {
    uint8_t tip = dfPlayer.readType();
    uint16_t parametre = dfPlayer.read();

    switch (tip) {
      case DFPlayerPlayFinished:
        Serial.print("DFPlayer: calma bitti (dosya ");
        Serial.print(parametre);
        Serial.println(")");
        sonrakiniCal();
        break;
      case DFPlayerCardInserted:
        Serial.println("DFPlayer: kart takildi.");
        break;
      case DFPlayerCardRemoved:
        Serial.println("DFPlayer: kart cikarildi!");
        break;
      case DFPlayerCardOnline:
        Serial.println("DFPlayer: kart online.");
        break;
      case DFPlayerError:
        Serial.print("DFPlayer HATA: ");
        Serial.print(parametre);
        Serial.print(" - ");
        Serial.println(sesHataAdi(parametre));
        // Istenen dosya calinamadi (yok/bozuk/klasor eksik): DFPlayer bu durumda
        // hicbir zaman "calma bitti" bildirimi gondermiyor. Bunu beklemeye devam
        // edersek kuyruk kalici olarak kilitlenip dolar (bkz. gozlemlenen "kuyruk
        // dolu" sorunu) - bu yuzden hatali ogeyi de bitmis sayip siradakine geciyoruz.
        sonrakiniCal();
        break;
      case DFPlayerFeedBack:
        // Komut ACK'i - hata degil, bilgi amacli, yapilacak bir sey yok.
        break;
      default:
        Serial.print("DFPlayer: bilinmeyen olay tipi ");
        Serial.println(tip);
        break;
    }
  }
}
