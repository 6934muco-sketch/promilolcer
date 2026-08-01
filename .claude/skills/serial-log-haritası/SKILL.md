---
name: serial-log-haritasi
description: "Use this skill whenever a Serial.print/Serial.println/Serial.printf call is added, removed, or moved anywhere in ui/ui.ino, ui/led.cpp, or ui/ses.cpp. Also trigger when the user asks to update, check, or sync ui/SERIAL_LOG_HARITASI.md. This keeps the Serial log index (file:line references and #if FLAG gating) accurate after any Serial.* change."
---

# Serial log haritası senkron tutma

## Bu skill ne işe yarar

`ui/SERIAL_LOG_HARITASI.md`, projedeki her `Serial.print*` çağrısının bir indeksidir:
dosya:satır referansı ve hangi `#if FLAG` ile açılıp kapandığı. Bu dosya elle senkron
tutulmalıdır — kod her değiştiğinde otomatik güncellenmez.

## Ne zaman devreye girer

- `ui/ui.ino`, `ui/led.cpp` veya `ui/ses.cpp` içinde yeni bir `Serial.print`,
  `Serial.println` veya `Serial.printf` çağrısı eklendiğinde
- Var olan bir `Serial.*` çağrısı silindiğinde veya başka bir yere taşındığında
- Kullanıcı doğrudan "serial log haritasını güncelle" veya "kontrol et" dediğinde

## Adımlar

1. Değişen dosyada (`ui.ino`, `led.cpp`, `ses.cpp`) tüm `Serial.print*` çağrılarını tara.
2. `ui/SERIAL_LOG_HARITASI.md` dosyasındaki mevcut tabloyla karşılaştır:
   - Yeni eklenmiş ama tabloda olmayan çağrılar var mı?
   - Tabloda olup kodda artık bulunmayan (silinmiş) satırlar var mı?
   - Satır numarası değişmiş (taşınmış) çağrılar var mı?
3. Her satır için şunu doğrula/güncelle:
   - Dosya adı ve satır numarası doğru mu?
   - Hangi `#if FLAG` bloğunun içinde olduğu doğru işaretlenmiş mi?
   - Log mesajının kısa açıklaması güncel mi?
4. Tabloyu güncelledikten sonra, değişikliği kullanıcıya özetle: kaç satır eklendi,
   kaç satır silindi, kaç satır numarası güncellendi.
5. Emin olmadığın bir durum varsa (örneğin bir Serial çağrısının hangi FLAG'e bağlı
   olduğu belirsizse), tahmin etme — kullanıcıya sor.

## Önemli

- Sadece satır ekleme/çıkarma değil, **satır numarası kayması** da senkronu bozar.
  Dosyanın üstüne yeni bir fonksiyon eklenmişse, altındaki tüm referanslar kaymış olabilir.
- Format tutarlılığı önemli: tablodaki mevcut sütun düzenini bozma, sadece içeriği güncelle.
