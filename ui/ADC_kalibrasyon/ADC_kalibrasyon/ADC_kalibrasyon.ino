#include <DHT.h>

const int MQ3_PIN = 34;

const float VOLTAJ_BOLUCU_ORANI = 1.6666;
const float RL = 200.0;
const float CLEAN_AIR_RATIO = 60.0;

// ui.ino'daki DHT_ORTAM (MQ-3'ün hemen yanındaki DHT22) ile aynı pin/tip: R0
// burada hangi sıcaklık/nemde bulunduysa, ui.ino'daki KALIBRASYON_SICAKLIK_REF /
// KALIBRASYON_NEM_REF sabitleri de aynı koşulları göstermeli - aksi halde
// hamPromilOlcVeDondur() içindeki sıcaklık/nem kompanzasyonu yanlış referanstan
// düzeltme yapar.
#define DHT_ORTAM_PIN 32
#define DHT_ORTAM_TIPI DHT22
DHT dhtOrtam(DHT_ORTAM_PIN, DHT_ORTAM_TIPI);

float R0 = 0;
float sicaklik = 0;
float nem = 0;

float R0_Hesapla()
{
    const int SAMPLE_COUNT = 500;

    float toplamVolt = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        uint32_t mv = analogReadMilliVolts(MQ3_PIN);
        toplamVolt += (mv / 1000.0f) * VOLTAJ_BOLUCU_ORANI;
        delay(5);
    }

    float volt = toplamVolt / SAMPLE_COUNT;

    if (volt < 0.10f)
        volt = 0.10f;

    float Rs = ((5.0f * RL) / volt) - RL;

    return Rs / CLEAN_AIR_RATIO;
}

// DHT22 saniyede birden fazla güvenilir okunamaz; R0_Hesapla() zaten ~2.5sn
// sürdüğü için her turda bir okuma yeterli. Okuma başarısız (NAN) gelirse
// önceki geçerli değer korunur.
void sicaklikNemGuncelle()
{
    float t = dhtOrtam.readTemperature();
    float h = dhtOrtam.readHumidity();

    if (!isnan(t)) sicaklik = t;
    if (!isnan(h)) nem = h;
}

void setup()
{
    Serial.begin(115200);

    analogSetAttenuation(ADC_11db);
    analogSetClockDiv(4);

    dhtOrtam.begin();

    Serial.println("Sensör Isınıyor...");
    delay(30000);   // 30 saniye bekle

    float oncekiR0 = 0;
    int kararlilik = 0;

    while (true)
    {
        float yeniR0 = R0_Hesapla();
        sicaklikNemGuncelle();

        Serial.print("R0 = ");
        Serial.print(yeniR0, 2);
        Serial.print("  |  Sicaklik = ");
        Serial.print(sicaklik, 1);
        Serial.print(" C  |  Nem = %");
        Serial.println(nem, 1);

        if (oncekiR0 != 0)
        {
            float fark = abs(yeniR0 - oncekiR0) / oncekiR0;

            Serial.print("Degisim = %");
            Serial.println(fark * 100.0, 3);

            if (fark < 0.002)      // %0.2
            {
                kararlilik++;
            }
            else
            {
                kararlilik = 0;
            }
        }

        oncekiR0 = yeniR0;

        if (kararlilik >= 10)
        {
            R0 = yeniR0;

            Serial.println();
            Serial.println("================================");
            Serial.println("R0 KARARLI HALE GELDI");
            Serial.print("FINAL R0 = ");
            Serial.println(R0, 2);
            Serial.print("Kalibrasyon Sicakligi = ");
            Serial.print(sicaklik, 1);
            Serial.println(" C");
            Serial.print("Kalibrasyon Nemi = %");
            Serial.println(nem, 1);
            Serial.println("================================");
            Serial.println("Bu iki degeri ui.ino icindeki KALIBRASYON_SICAKLIK_REF");
            Serial.println("ve KALIBRASYON_NEM_REF sabitlerine yazin.");
            break;
        }
    }
}

void loop()
{
}
