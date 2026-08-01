# 🍺 Akıllı Alkolmetre
## LED, Ses ve Kullanıcı Deneyimi Tasarım Dokümanı
### Final Tasarım (v1.0)

---

# Tasarım Amacı

Bu cihazın amacı yalnızca doğru ölçüm yapmak değildir.

Kullanıcının cihazın hangi aşamada olduğunu ekran, ses ve LED yardımıyla kolayca anlayabilmesi hedeflenmiştir.

LED sistemi yalnızca dekoratif değil, cihazın çalışma durumunu gösteren bir arayüzdür.

Ses sistemi ise kullanıcıyı yönlendiren profesyonel bir asistandır.

---

# Çalışma Akışı

```
Bekleme
      │
      ▼
Üfleme Yapabilirsiniz
(10 saniye)
      │
      ▼
Analiz Yapılıyor
(3 saniye)
      │
      ▼
Sonuç
(10 saniye)
      │
      ▼
Kanal Temizleme
(Fan çalışır)
      │
      ▼
Bekleme
```

---

# LED Tasarımı

## 🔵 Bekleme

Renk

- Mavi

Animasyon

- Yavaş nefes efekti

Parlaklık

- %15

---

## 🟠 Üfleme Yapabilirsiniz

Süre

- 10 saniye

Animasyon

- Soldan sağa kayan turuncu ışık

Parlaklık

- %40

Amaç

- Kullanıcıya üfleme zamanını göstermek.

---

## 🔷 Analiz Yapılıyor

Süre

- 3 saniye

Renk

- Açık Mavi (Cyan)

Animasyon

- Ortadan dışarı doğru yayılan pulse animasyonu

Parlaklık

- %60

Amaç

- Kullanıcıya cihazın sonucu analiz ettiği hissini vermek.

---

## 🟢 Sonuç (0.50‰ Altı)

Renk

- Yeşil

Animasyon

- Sonuç açıklandığında LED soldan sağa dolarak tamamlanır.
- Ardından 10 saniye sabit yeşil yanar.

Parlaklık

- %80

---

## 🔴 Sonuç (0.50‰ ve Üzeri)

Renk

- Kırmızı

Animasyon

- Sonuç açıklandığında LED soldan sağa dolar.
- Ardından yavaş nefes efektiyle yanmaya devam eder.

Parlaklık

- %80

---

## 🟣 Kanal Temizleme

Fan çalışmaya başladığında aktif olur.

Renk

- Mor

Animasyon

- Hızlı kayan ışık

Fan durunca sona erer.

Parlaklık

- %40

---

# LED Geçişleri

Hiçbir renk aniden değişmez.

Tüm geçişler

Yaklaşık

300 ms

fade animasyonu ile yapılır.

```
Mavi

↓

Turuncu

↓

Açık Mavi

↓

Yeşil / Kırmızı

↓

Mor

↓

Mavi
```

---

# Sesli Yönlendirme

## Ölçüm Başlangıcı

"Lütfen ağızlığı ağzınıza yaklaştırın."

"Kısa bekleme"

"Şimdi üflemeye başlayabilirsiniz."

---

## Ölçüm Sırasında

Yaklaşık orta noktada

"Üflemeye devam edin."

Ölçüm bitimine yakın

"Biraz daha devam edin."

---

## Analiz

"Analiz ediliyor."

---

## Sonuç Güvenli

"Ölçüm tamamlandı."

"Sonucunuz ... promil."

"Yasal sınırın altındasınız."

---

## Sonuç Yüksek

"Ölçüm tamamlandı."

"Sonucunuz ... promil."

"Yasal sınırın üzerindesiniz."

---

## Kanal Temizleme

"Kanal temizleniyor."

---

## İşlem Sonu

"Cihaz tekrar kullanıma hazır."

---

# Ses Efektleri

İsteğe bağlı olarak

• Butona basınca kısa bip

• Ölçüm başlarken bip

• Analiz başlangıcında hafif elektronik ses

• Güvenli sonuçta iki kısa yükselen bip

• Yüksek sonuçta iki tok bip

---

# Kullanıcı Deneyimi Prensipleri

✔ LED cihazın durumunu anlatır.

✔ Ses kullanıcıyı yönlendirir.

✔ Ekran ayrıntılı bilgiyi gösterir.

Bu üç sistem birbirini tamamlar.

---

# Tasarım İlkeleri

- Gereksiz animasyon kullanılmayacak.
- Rainbow (gökkuşağı) efektleri kullanılmayacak.
- RGB oyuncu ekipmanı görünümünden kaçınılacak.
- Renkler yalnızca anlam taşıyacak.
- Kullanıcı her aşamayı LED'den anlayabilecek.
- Animasyonlar sade ve profesyonel olacak.

---

# Renk Anlamları

🔵 Hazır

🟠 Üfleme

🔷 Analiz

🟢 Güvenli Sonuç

🔴 Yüksek Sonuç

🟣 Kanal Temizleme




---

# Nihai Tasarım Kararı

Bu tasarımın amacı;

Profesyonel,

Modern,

Akıllı,

Kullanımı kolay,

ve premium hissi veren bir alkolmetre kullanıcı deneyimi oluşturmaktır.

Versiyon

v1.0 (Final Tasarım)