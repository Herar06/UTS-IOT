Functional Specification Document (FSD)
## Sistem IoT ESP32 — FreeRTOS + MQTT + Keamanan Jaringan

---

| Field | Keterangan |
|---|---|
| **Judul Dokumen** | UTS IOT |
| **Nama Proyek** | ESP32 FreeRTOS MQTT Publisher dengan Override |
| **Versi** | 1.0 |
| **Tanggal** | April 2026 |
| **Penulis** | Edbert Gerald Falentina |
| **Mata Kuliah** | Iot Design & Application |
| **NIM** | 2802402110 |

---

## Daftar Isi

1. [Pendahuluan](#1-pendahuluan)
2. [Gambaran Umum Sistem](#2-gambaran-umum-sistem)
3. [Spesifikasi Perangkat Keras](#3-spesifikasi-perangkat-keras)
4. [Kebutuhan Fungsional](#4-kebutuhan-fungsional)
5. [Sistem Kontrol LED](#5-sistem-kontrol-led)
6. [Desain FreeRTOS](#6-desain-freertos)
7. [Keamanan Jaringan](#7-keamanan-jaringan)
8. [Skenario Operasional](#8-skenario-operasional)
9. [Dependensi Perangkat Lunak](#9-dependensi-perangkat-lunak)

---

## 1. Pendahuluan

### 1.1 Tujuan

Dokumen Spesifikasi Fungsional (FSD) ini menjelaskan desain dan implementasi sistem IoT menggunakan mikrokontroler ESP32 DOIT DevKit V1. Sistem ini membaca data lingkungan dari sensor cahaya BH1750 dan sensor suhu/kelembaban DHT11, kemudian mengirim data tersebut ke broker MQTT publik menggunakan sistem operasi real-time FreeRTOS. Terdapat fitur tambahan berupa override yang bisa membuat LED tertahan nyala selama 10 detik, lalu kembali normal menyala dan mati selama 1 detik lagi.

### 1.2 Ruang Lingkup

Dokumen ini mencakup aspek-aspek berikut dari sistem yang dibangun:

- Konfigurasi hardware dan konfigurasi pin
- Desain task FreeRTOS (Tasks, Queues, Mutex, Task Notifications)
- Logika publikasi data MQTT dan struktur topik
- Urutan delay berbasis NIM (X=1, Y=0)
- Kontrol LED dengan override jarak jauh via MQTT
- Keamanan jaringan melalui autentikasi MQTT

### 1.3 Definisi dan Singkatan

| Istilah | Definisi |
|---|---|
| **MQTT** | Message Queuing Telemetry Transport — protokol pesan pub/sub yang ringan |
| **FreeRTOS** | Free Real-Time Operating System untuk sistem tertanam |
| **BH1750** | Sensor cahaya yang berkomunikasi melalui I2C |
| **DHT11** | Sensor suhu dan kelembaban dengan protokol single-wire |
| **ESP32** | MCU Xtensa LX6 32-bit dual-core dengan Wi-Fi dan Bluetooth terintegrasi |
| **Queue** | Buffer komunikasi antar-task FreeRTOS untuk mengirim data sensor |
| **Mutex** | Semaphore mutual exclusion untuk melindungi akses bersama ke MQTT client |
| **Binary Semaphore** | Mekanisme sinyal FreeRTOS bernilai 0 atau 1, digunakan sebagai penanda kejadian antar-task |

---

## 2. Gambaran Umum Sistem

### 2.1 Arsitektur Sistem

Sistem dibangun sebagai aplikasi multi-task FreeRTOS yang berjalan di atas ESP32. Empat task berjalan secara bersamaan untuk membaca sensor, mempublikasikan data MQTT, mengontrol LED, dan mengelola koneksi MQTT. Data mengalir antar-task melalui FreeRTOS Queue, dan sumber daya bersama (MQTT client) dilindungi oleh semaphore Mutex.

| Nama Task | Prioritas | Tanggung Jawab |
|---|---|---|
| `taskSensor` | 1 | Membaca sensor BH1750 dan DHT11 setiap 2 detik lalu kirim ke Queue |
| `taskPublish` | 1 | Membaca Queue, mempublikasikan data ke broker MQTT setiap 10 detik sesuai urutan NIM |
| `taskMQTT` | 2 | Menjaga koneksi MQTT, memanggil `mqtt.loop()` untuk memproses pesan masuk |
| `taskLED` | 1 | Mengedipkan LED 1 dtk ON / 1 dtk OFF; beralih ke override 10 dtk saat menerima Task Notification |

### 2.2 Alur Data

```
BH1750 & DHT11  →  [taskSensor]  →  FreeRTOS Queue  →  [taskPublish]  →  Broker MQTT
                                                                               ↑
Aplikasi MQTTX  →  Broker MQTT  →  [taskMQTT / mqtt.loop()]  →  mqttCallback()  →  xSemaphoreGive(ledSem)  →  [taskLED]
```

---

## 3. Spesifikasi Perangkat Keras

### 3.1 Daftar Komponen

| Komponen | Model / Spesifikasi | Fungsi |
|---|---|---|
| Microcontroller | ESP32 DOIT DevKit V1 | Unit pemrosesan utama, Wi-Fi, host FreeRTOS |
| Sensor Cahaya | BH1750 (I2C) | Mengukur intensitas cahaya  dalam satuan lux |
| Sensor Suhu & Kelembaban | DHT11 (Single-wire) | Mengukur suhu  dan kelembaban relatif |
| LED | LED standar warna hijau | Indikator visual — status kedip / override |
| Aplikasi MQTT Client | MQTTX (Aplikasi PC) | Subscribe ke topik sensor, mengirim perintah LED |

### 3.2 Konfigurasi Pin

| Komponen | Pin Komponen | Pin ESP32 | Keterangan |
|---|---|---|---|
| BH1750 | VCC | 3V3 | Sumber daya 3.3V |
| BH1750 | GND | GND | Ground |
| BH1750 | SDA | GPIO 21 | Jalur data I2C |
| BH1750 | SCL | GPIO 22 | Jalur clock I2C |
| DHT11 | VCC | 3V3 | Sumber daya 3.3V |
| DHT11 | GND | GND | Ground |
| DHT11 | OUT | GPIO 5 | Sinyal data output |
| LED | Anoda (+) | GPIO 4 | Output digital (HIGH = ON) |
| LED | Katoda (−) | GND | Ground |

---

## 4. Kebutuhan Fungsional

### 4.1 Pembacaan Sensor (FR-01)

- Sistem harus membaca intensitas cahaya dari sensor BH1750 secara terus-menerus melalui I2C (SDA=GPIO21, SCL=GPIO22).
- Sistem harus membaca suhu dan kelembaban relatif dari sensor DHT11 yang terhubung ke GPIO5 secara terus-menerus.
- Pembacaan BH1750 dirata-ratakan dari 5 sampel untuk mengurangi noise.
- Apabila sensor mengembalikan nilai yang tidak valid (NaN atau di luar rentang), sistem harus mengirim error ke Serial dan mencoba ulang setelah 2 detik tanpa menghentikan task lainnya.
- Data sensor yang valid harus ditulis ke FreeRTOS Queue berukuran 1 slot menggunakan `xQueueOverwrite()` agar selalu menyimpan data terbaru.

### 4.2 Publikasi Data MQTT (FR-02)

- Sistem harus terhubung ke broker MQTT publik di `broker.emqx.io` pada port 1883.
- Autentikasi wajib digunakan. Sistem harus mengirimkan username dan password saat memanggil `mqtt.connect()`.
- Sistem harus mempublikasikan data sensor setiap 10 detik per siklus penuh.
- Setiap siklus mengikuti urutan delay berbasis NIM yang didefinisikan pada Bagian 4.3.
- Setiap jenis data dipublikasikan ke topik unik menggunakan inisial nama sebagai pengubah (lihat Bagian 4.4).
- Akses ke MQTT client harus dilindungi oleh semaphore Mutex untuk mencegah akses bersamaan dari beberapa task.

### 4.3 Urutan Delay Berbasis NIM (FR-03)

Urutan pengiriman dalam setiap siklus 10 detik lalu ditentukan oleh 3 digit terakhir NIM saya. Dengan nilai **X=1** dan **Y=0**:

| Langkah | Aksi | Waktu Berlalu | Keterangan |
|---|---|---|---|
| 1 | Publikasi Intensitas Cahaya → `LightE` | t = 0 dtk | Dipublikasikan langsung saat siklus dimulai |
| 2 | Delay X = 1 detik | t = 0–1 dtk | X = 1 (dari NIM), `vTaskDelay(1000ms)` |
| 3 | Publikasi Suhu → `TemperaturenG` | t = 1 dtk | 1 detik setelah cahaya |
| 4 | Delay Y = 0 detik | t = 1 dtk | Y = 0 (dari NIM), tidak ada delay |
| 5 | Publikasi Kelembaban → `HumidityF` | t = 1 dtk | Langsung setelah suhu |
| 6 | Tunggu siklus berikutnya | t = 1–10 dtk | Sisa waktu ~9 dtk dipadding agar genap 10 dtk |
| 7 | Publikasi Intensitas Cahaya → `LightE` | t = 10 dtk | Dipublikasikan setelah 10 detik dari humidity |

Total durasi siklus selalu dipadding menjadi tepat 10 detik.

### 4.4 Format Topik MQTT Sesuai Urutan Nama (FR-04)

Topik diturunkan dari inisial nama penulis (E, G, F):

| Jenis Data | Topik MQTT | Format Payload |
|---|---|---|
| Intensitas Cahaya | `LightE` | Float, 2 desimal — contoh: `245.60` (lux) |
| Suhu | `TemperaturenG` | Float, 2 desimal — contoh: `27.50` |
| Kelembaban Relatif | `HumidityF` | Float, 2 desimal — contoh: `65.00` |
| Override LED (Subscribe) | `LED/control` | String — `"ON"` memicu override 10 dtk, pesan lain diabaikan |

---

## 5. Sistem Kontrol LED

### 5.1 Perilaku Kedip Normal (FR-05)

Dalam kondisi normal, LED pada GPIO 4 akan berkedip dalam loop berkelanjutan:

- LED **NYALA** selama 1 detik
- LED **MATI** selama 1 detik
- Siklus ini berulang tanpa henti sebagai perilaku dasar

### 5.2 Override Jarak Jauh via MQTT (FR-06)

Sistem berlangganan (subscribe) ke topik `LED/control`. Saat pesan `"ON"` diterima:

1. `mqttCallback()` memanggil `xSemaphoreGive(ledSem)` untuk memberi sinyal ke `taskLED`.
2. Pada iterasi loop berikutnya, `taskLED` mendeteksi sinyal via `xSemaphoreTake(ledSem, 0)`.
3. LED langsung dinyalakan dan tetap **NYALA terus-menerus** selama tepat **10 detik**.
4. Setelah 10 detik, LED dimatikan dan `taskLED` **otomatis kembali** ke siklus kedip 1 dtk NYALA / 1 dtk MATI.
5. Publikasi data MQTT (`taskPublish`) tetap **berjalan tanpa gangguan** selama periode override berlangsung.

### 5.3 Sinkronisasi FreeRTOS — Task Notification (FR-07)

Mekanisme override LED diimplementasikan menggunakan **FreeRTOS Binary Semaphore** (`xSemaphoreCreateBinary`) sebagai pengganti flag `volatile bool`:

| Properti | `volatile bool` (tidak digunakan) | Binary Semaphore (yang diimplementasikan) |
|---|---|---|
| Sinyal bisa hilang? |  Rentan race condition |  Tidak — state disimpan oleh FreeRTOS |
| Thread-safe? |  Tidak ada proteksi |  Dikelola sepenuhnya oleh kernel FreeRTOS |
| Aman dari callback? |  Tidak aman |  `xSemaphoreGive()` aman dipanggil dari callback |
| Cara pengecekan | Pengecekan flag di while loop | `xSemaphoreTake(ledSem, 0)` — timeout 0, non-blocking |

Timeout `0` pada `xSemaphoreTake(ledSem, 0)` berarti **langsung cek tanpa menunggu** — jika sinyal tersedia langsung diambil dan masuk mode override, jika tidak ada sinyal langsung lanjut ke blink normal. Ini memungkinkan `taskLED` terus berkedip normal di setiap iterasi sambil tetap responsif terhadap perintah override.

---

## 6. Desain FreeRTOS

### 6.1 Ringkasan Task

| Task | Prioritas | Stack (byte) | Fitur FreeRTOS yang Digunakan |
|---|---|---|---|
| `taskSensor` | 1 | 4096 | Queue (`xQueueOverwrite`) |
| `taskPublish` | 1 | 4096 | Queue (`xQueuePeek`) + Mutex |
| `taskMQTT` | 2 | 4096 | Mutex (`xSemaphoreTake/Give`) |
| `taskLED` | 1 | 2048 | Binary Semaphore (`xSemaphoreTake`) |

### 6.2 Desain Queue

- **Tipe:** `xQueueCreate(1, sizeof(SensorData))` — queue 1 slot dengan overwrite
- **Produsen:** `taskSensor` menulis data baru setiap ~2 detik menggunakan `xQueueOverwrite()`
- **Konsumen:** `taskPublish` membaca data menggunakan `xQueuePeek()` (non-destruktif) agar data bisa digunakan ulang dalam siklus 10 detik yang sama
- **Keuntungan:** Selalu menyimpan data sensor terbaru; tidak ada penumpukan data lama

### 6.3 Desain Mutex

- **Tipe:** `xSemaphoreCreateMutex()`
- **Tujuan:** `PubSubClient` (mqtt) tidak thread-safe — Mutex mencegah akses bersamaan dari `taskPublish` dan `taskMQTT`
- **Penggunaan:** `xSemaphoreTake(mqttMutex, portMAX_DELAY)` dipanggil sebelum `mqtt.publish()` atau `mqtt.loop()`, lalu langsung dilepas setelahnya

### 6.4 Desain Binary Semaphore

- **Tipe:** `xSemaphoreCreateBinary()`
- **Pengirim:** `xSemaphoreGive(ledSem)` — dipanggil dari `mqttCallback()` saat pesan `"ON"` diterima
- **Penerima:** `xSemaphoreTake(ledSem, 0)` di awal setiap iterasi loop `taskLED` — timeout `0` agar non-blocking
- **Perilaku:** Jika semaphore tersedia (bernilai 1), `taskLED` masuk ke mode override dan menyalakan LED selama 10 detik. Jika tidak ada sinyal (bernilai 0), task langsung lanjut ke blink normal
- **Alasan memilih Binary Semaphore vs Mutex:** Mutex dirancang untuk melindungi sumber daya bersama dan hanya boleh di-release oleh task yang sama yang mengambilnya. Binary Semaphore cocok untuk skenario **sinyal satu arah** seperti ini — satu pihak (callback) memberi sinyal, pihak lain (taskLED) mengonsumsinya
---

## 7. Keamanan Jaringan

### 7.1 Autentikasi MQTT (FR-08)

Sistem terhubung ke broker `broker.emqx.io` menggunakan Client ID unik `ESP32_FINAL_FIX`. 

Subscription ke topik `LED/control` hanya dilakukan setelah koneksi ke broker berhasil, sehingga perintah LED tidak akan diterima sebelum koneksi aktif. Jika koneksi terputus, `taskMQTT` secara otomatis mencoba menyambung kembali dan melakukan subscribe ulang.

---

## 8. Skenario Operasional

### 8.1 Skenario 1 — Operasi Normal

1. ESP32 menyala dan terhubung ke Wi-Fi (`CEIOT`).
2. ESP32 terhubung ke `broker.emqx.io` dengan Client ID `ESP32_FINAL_FIX` dan langsung subscribe ke topik `LED/control`.
3. `taskSensor` membaca BH1750 (rata-rata 5 sampel) dan DHT11, menyimpan data terbaru ke dalam Queue.
4. `taskPublish` membaca Queue dan mempublikasikan data setiap 10 detik dengan urutan:
   - `LightE` pada t = 0 dtk
   - `vTaskDelay(1000ms)` → delay X=1 dtk
   - `TemperaturenG` pada t = 1 dtk
   - tidak ada delay (Y=0) → langsung lanjut
   - `HumidityF` pada t = 1 dtk
   - `vTaskDelay(9000ms)` → menunggu hingga siklus 10 dtk selesai
5. `taskLED` mengedipkan LED pada GPIO 4: 1 dtk NYALA → 1 dtk MATI → berulang.
6. Aplikasi MQTTX menerima ketiga topik dan menampilkan pembacaan data secara live.


### 8.2 Skenario 2 — Override LED (Perintah MQTT)

1. Pengguna membuka MQTTX dan mempublikasikan pesan `"ON"` ke topik `LED/control`.
2. MQTTX mengonfirmasi bahwa pesan telah berhasil terkirim.
3. `taskMQTT` menerima pesan melalui `mqtt.loop()` dan memanggil `mqttCallback()`.
4. `mqttCallback()` memanggil `xSemaphoreGive(ledSem)` — Binary Semaphore nilainya menjadi 1.
5. Pada iterasi loop berikutnya, `taskLED` mendeteksi sinyal via `xSemaphoreTake(ledSem, 0)` yang bernilai `pdTRUE`.
6. LED pada GPIO 4 dinyalakan (`digitalWrite(LED_PIN, HIGH)`) dan tetap **NYALA** selama 10 detik (`vTaskDelay(10000ms)`).
7. Setelah 10 detik selesai, LED dimatikan (`digitalWrite(LED_PIN, LOW)`) dan `continue` membawa eksekusi kembali ke awal loop.
8. `taskLED` kembali ke siklus kedip 1 dtk NYALA / 1 dtk MATI secara otomatis.
9. Publikasi data (`LightE`, `TemperaturenG`, `HumidityF`) tetap berjalan tanpa gangguan selama seluruh proses override berlangsung.

### 8.3 Skenario 3 — Pemulihan Error Sensor

1. `taskSensor` mendeteksi pembacaan BH1750 yang tidak valid (nilai < 0 atau > 10000 lux).
2. Pesan error dikirim ke Serial: `BH1750 ERROR`
3. `taskSensor` menunggu 2 detik (`vTaskDelay(2000ms)`) lalu mencoba ulang — Queue tidak diperbarui dengan data yang tidak valid.
4. Demikian pula jika DHT11 mengembalikan `NaN`, pesan `DHT ERROR` dikirim dan task retry setelah 2 detik.
5. `taskPublish` tetap menggunakan data valid terakhir yang ada di Queue selama periode percobaan ulang berlangsung.

---

## 9. Dependensi Perangkat Lunak

| Library | Versi (yang diuji) | Fungsi |
|---|---|---|
| Arduino ESP32 Core | 3.x | Framework Arduino untuk ESP32, API FreeRTOS |
| PubSubClient | 2.8.x | MQTT client untuk operasi publish/subscribe |
| BH1750 | 1.3.x | Driver I2C untuk sensor cahaya BH1750 |
| DHT sensor library | 1.4.x | Driver sensor DHT11/DHT22 single-wire |
| Adafruit Unified Sensor | 1.1.x | Dependensi yang dibutuhkan oleh DHT sensor library |
| Wire (bawaan) | — | Komunikasi I2C (SDA=GPIO21, SCL=GPIO22) |
| WiFi (bawaan) | — | Manajemen koneksi Wi-Fi |

---

