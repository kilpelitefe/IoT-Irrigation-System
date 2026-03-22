#define BLYNK_TEMPLATE_ID   "TMPL6vWtDF-GN"
#define BLYNK_TEMPLATE_NAME "Tarım Sulama Sistemi"
#define BLYNK_AUTH_TOKEN ""
#define BLYNK_PRINT Serial



#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <DHT.h>

// ---------- WiFi Bilgileri ----------
char wifiAdi[]   = "WIFI_ADI";
char wifiSifre[] = "WIFI_SIFRE";

// ---------- Pin Tanımları ----------
#define TOPRAK_SENSOR_PIN  34
#define SU_MOTORU_PIN      23
#define SICAKLIK_PIN       25

#define I2C_SDA 21
#define I2C_SCL 22

// ---------- Röle Mantığı ----------
const bool ROLE_DUZ_MANTIK = true;   

// ---------- DHT Sensörü ----------
#define DHT_TIP DHT22
DHT dht(SICAKLIK_PIN, DHT_TIP);


float sonGecerliSicaklik = NAN;
float sonGecerliHavaNemi = NAN;

// ---------- BME280 ----------
Adafruit_BME280 bme;
bool bmeCalisiyorMu = false;

// ---------- Blynk Sanal Pin Haritası ----------
#define V_MANUEL_BUTON  V1
#define V_NEM_GOSTERGE  V2
#define V_DURUM_YAZISI  V3
#define V_SICAKLIK      V4
#define V_HAVANEMI      V5
#define V_OTOMATIK_MOD  V6
#define V_ESIK_SLIDER   V7
#define V_BASINC        V8

BlynkTimer zamanlayici;

// ---------- Durum Değişkenleri ----------
int  manuelButonDurumu = 0;
int  otomatikModAcikMi = 0;
int  sulamaSiniri      = 30;
bool motorAcikMi       = false;

const int GUVENLIK_ARALIGI = 5;

// Sensörün ham ADC değerleri 
const int KURU_DEGER  = 4095;
const int ISLAK_DEGER = 0;

// Blynk bağlantı zaman aşımı (ms)
const unsigned long BLYNK_BAGLANTI_SURESI = 10000UL;



void motoruKontrolEt(bool acilsinMi) {
  int sinyal = ROLE_DUZ_MANTIK
               ? (acilsinMi ? HIGH : LOW)
               : (acilsinMi ? LOW  : HIGH);

  digitalWrite(SU_MOTORU_PIN, sinyal);
  motorAcikMi = acilsinMi;
}

int topragiOlc() {
  int  hamDeger = analogRead(TOPRAK_SENSOR_PIN);
  long yuzde    = map(hamDeger, KURU_DEGER, ISLAK_DEGER, 0, 100);
  return (int)constrain(yuzde, 0, 100);
}



BLYNK_CONNECTED() {
  
  Blynk.syncVirtual(V_MANUEL_BUTON);
  Blynk.syncVirtual(V_OTOMATIK_MOD);
  Blynk.syncVirtual(V_ESIK_SLIDER);
}

BLYNK_WRITE(V_MANUEL_BUTON) {
  manuelButonDurumu = param.asInt();
  if (otomatikModAcikMi == 0)
    motoruKontrolEt(manuelButonDurumu == 1);
}

BLYNK_WRITE(V_OTOMATIK_MOD) {
  otomatikModAcikMi = param.asInt();
}

BLYNK_WRITE(V_ESIK_SLIDER) {
  sulamaSiniri = constrain(param.asInt(), 0, 100);
}



void sistemiKontrolEt() {

  // --- Toprak Nemi ---
  int toprakNemi = topragiOlc();
  Blynk.virtualWrite(V_NEM_GOSTERGE, toprakNemi);

  // --- DHT22: Sıcaklık & Hava Nemi ---

  float sicaklik = dht.readTemperature();
  float havaNemi = dht.readHumidity();

  if (!isnan(sicaklik)) {
    sonGecerliSicaklik = sicaklik;
    Blynk.virtualWrite(V_SICAKLIK, sicaklik);
  } else if (!isnan(sonGecerliSicaklik)) {
    Blynk.virtualWrite(V_SICAKLIK, sonGecerliSicaklik);
    Serial.println("DHT: Sıcaklık okunamadı, son geçerli değer kullanılıyor.");
  }

  if (!isnan(havaNemi)) {
    sonGecerliHavaNemi = havaNemi;
    Blynk.virtualWrite(V_HAVANEMI, havaNemi);
  } else if (!isnan(sonGecerliHavaNemi)) {
    Blynk.virtualWrite(V_HAVANEMI, sonGecerliHavaNemi);
    Serial.println("DHT: Hava nemi okunamadı, son geçerli değer kullanılıyor.");
  }

  // --- BME280: Basınç ---
  if (bmeCalisiyorMu) {
    float basinc = bme.readPressure() / 100.0F;
    if (!isnan(basinc))
      Blynk.virtualWrite(V_BASINC, basinc);
  }

  // --- Otomatik / Manuel Mod Karar Mantığı ---
  if (otomatikModAcikMi == 1) {

    if (!motorAcikMi && toprakNemi < sulamaSiniri) {
      motoruKontrolEt(true);
      Blynk.virtualWrite(V_DURUM_YAZISI, "OTO: Sulaniyor...");
    }
    else if (motorAcikMi && toprakNemi > (sulamaSiniri + GUVENLIK_ARALIGI)) {
      motoruKontrolEt(false);
      Blynk.virtualWrite(V_DURUM_YAZISI, "OTO: Nem Yeterli");
    }
    else {
      String durumMesaji = motorAcikMi ? "OTO: ACIK" : "OTO: BEKLEMEDE";
      Blynk.virtualWrite(V_DURUM_YAZISI, durumMesaji);
    }

    // Buton görünümünü gerçek motor durumuyla senkronize et
    Blynk.virtualWrite(V_MANUEL_BUTON, motorAcikMi ? 1 : 0);
  }
  else {
    bool patronActiMi = (manuelButonDurumu == 1);
    if (patronActiMi != motorAcikMi)
      motoruKontrolEt(patronActiMi);

    Blynk.virtualWrite(V_DURUM_YAZISI, motorAcikMi ? "MANUEL: ACIK" : "MANUEL: KAPALI");
  }
}



void setup() {
  Serial.begin(115200);

  // Motor pinini çıkış olarak ayarla ve motoru kapat
  pinMode(SU_MOTORU_PIN, OUTPUT);
  motoruKontrolEt(false);


  analogSetPinAttenuation(TOPRAK_SENSOR_PIN, ADC_11db);

  // DHT başlat
  dht.begin();

  // BME280 başlat (0x76 → 0x77 sırasıyla dene)
  Wire.begin(I2C_SDA, I2C_SCL);
  bmeCalisiyorMu = bme.begin(0x76);
  if (!bmeCalisiyorMu)
    bmeCalisiyorMu = bme.begin(0x77);
  Serial.println(bmeCalisiyorMu ? "BME280 Bulundu" : "BME280 YOK!");

  // FIX: Blynk.begin() yerine config + connect kullan

  Blynk.config(BLYNK_AUTH_TOKEN);

  Serial.print("WiFi'ye baglaniyor");
  WiFi.begin(wifiAdi, wifiSifre);
  unsigned long baslangic = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - baslangic < BLYNK_BAGLANTI_SURESI) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi baglandi.");
    if (Blynk.connect(BLYNK_BAGLANTI_SURESI / 1000)) {
      Serial.println("Blynk baglandi.");
    } else {
      Serial.println("Blynk baglantisi basarisiz, cevrimdisi devam ediliyor.");
    }
  } else {
    Serial.println("\nWiFi baglantisi basarisiz, cevrimdisi devam ediliyor.");
  }


  Blynk.virtualWrite(V_OTOMATIK_MOD, otomatikModAcikMi);
  Blynk.virtualWrite(V_ESIK_SLIDER,  sulamaSiniri);


  zamanlayici.setInterval(2000L, sistemiKontrolEt);
}



void loop() {
  if (Blynk.connected())
    Blynk.run();
  zamanlayici.run();
}
