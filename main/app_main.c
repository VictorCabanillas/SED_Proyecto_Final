// Librerías estándar
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/socket.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// ESP-IDF Core
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"

// Drivers
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/adc.h"
#include "driver/ledc.h"

// Ejemplos y configuraciones
#include "protocol_examples_common.h"
#include "sdkconfig.h"

// RFID
#include "MFRC522.h"

static const char *TAG = "Sistema de alarma";
const int LED = 17;

#define BUZZER_GPIO    4  // GPIO del buzzer
#define TONE_FREQUENCY 2000 // Frecuencia en Hz

#define SENSOR_CHANNEL ADC1_CHANNEL_7  // Canal del ADC para el sensor de proximidad
#define ADC_ATTEN ADC_ATTEN_DB_11      // Para leer hasta ~3.6V
#define ADC_WIDTH ADC_WIDTH_BIT_12     // Resolución de 12 bits (0-4095)

volatile bool tarjeta_detectada = false;  // Bandera global
volatile bool movimiento_detectado = false;  // Bandera global

esp_mqtt_client_handle_t client;
spi_device_handle_t spi;
static void periodic_timer_callback_sensor(void *arg);
static void periodic_timer_callback_panel(void *arg);

typedef struct {
    spi_device_handle_t spi;
    esp_mqtt_client_handle_t client;
} callback_args_t;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void configure_sensor(void)
{
    ESP_LOGI(TAG, "Configurando sensor analógico de proximidad...");

    // Configura el canal del ADC
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(SENSOR_CHANNEL, ADC_ATTEN);
}
static void configure_buzzer(void)
{
    // Configura el temporizador
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = TONE_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // Configura el canal
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 512, // 50% de ciclo de trabajo
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);
}

static void configure_led(void)
{
    gpio_reset_pin(LED);
    gpio_set_level(LED, 0);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
}

static spi_device_handle_t configureRFID(void)
{
    printf("CONFIGURANDO RFID");
    esp_err_t ret;
    spi_device_handle_t spi;
    spi_bus_config_t buscfg={
        .miso_io_num= 19,
        .mosi_io_num=23,
        .sclk_io_num=18,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=5000000,               //Clock out at 5 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=5,               //CS pin (SDA)
        .queue_size=7,                          
    };
    //RST a 3.3
    //Initialize the SPI bus
    printf("INIT BUS");
    ret=spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret==ESP_OK);
    //Attach the RFID to the SPI bus
    printf("ADD DEVICE");
    ret=spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
    assert(ret==ESP_OK);
   
    PCD_Init(spi);
    return spi;
}

static bool checkRFID(spi_device_handle_t spi)
{
    bool card = false;
    if(PICC_IsNewCardPresent(spi))                   //Checking for new card
    {
        card = true;
        printf("***card detected!***\n");
        GetStatusCodeName(PICC_Select(spi,&uid,0));
        PICC_DumpToSerial(spi,&uid);                  //DETAILS OF UID ALONG WITH SECTORS
        
        //Codigo por si se quiere añadir un sistema de distincion de tarjetas mediante usuario y contraseña

        // GetStatusCodeName(PICC_RequestA(spi,req_buffer,&req_len));
        //   GetStatusCodeName(PICC_Select(spi,&uid,0));
        //   GetStatusCodeName(PCD_Authenticate(spi,PICC_CMD_MF_AUTH_KEY_A,5,&key, &(uid)));
        //   GetStatusCodeName(MIFARE_Write(spi,4,(uint8_t*)username,16));
        //    GetStatusCodeName(MIFARE_Write(spi,5,(uint8_t*)password,16));
        //   MIFARE_Read(spi,4,card_rx_buffer,&card_rx_len);
        //   PCD_StopCrypto1(spi);
        // ESP_LOGI(TAG,"MIFARE block %d : %s",4,(char*)card_rx_buffer);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    return card;
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    //Inicializar sensor proximidad
    configure_sensor();
    ESP_LOGI(TAG, "GPIO %d configured as input", PresenceSensor);

    //Inicializar  led y buzzer
    configure_led();
    configure_buzzer();

    //Inicializar RFID
    spi = configureRFID();
    
    //Crear tarea periodica para leer el sensor de proximidad
    const esp_timer_create_args_t periodic_timer_args_read_sensor = {
        .callback = periodic_timer_callback_sensor,
        // name is optional, but may help identify the timer when debugging /
        .name = "periodicTemp1"};

    //Crear tarea periodica para leer el panel RFID
    const esp_timer_create_args_t periodic_timer_args_read_panel = {
        .callback = periodic_timer_callback_panel,
        // name is optional, but may help identify the timer when debugging /
        .name = "periodicTemp2"};
    
    esp_timer_handle_t periodic_timer_sensor, periodic_timer_panel;
    
    //Inicializar temporizadores
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args_read_sensor, &periodic_timer_sensor));
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args_read_panel, &periodic_timer_panel));

    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_sensor, 1000000));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_panel, 1000000));
    
    

    //El bucle principal del programa
    while (true)
    {    
        //Comprobar si se ha detectado una tarjeta RFID y desactivar el buzzer y el led
        if (tarjeta_detectada) {
            //apago el led y el buzzer
            gpio_set_level(LED, 0);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            tarjeta_detectada = false;
            movimiento_detectado = false;
        }
        
        //Comprobar si se ha detectado movimiento y activar el buzzer y el led
        if (movimiento_detectado) {
            //enciende el led y el buzzer
            gpio_set_level(LED, 1);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512); // 50%
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
}


// Callback para el temporizador del sensor de proximidad
static void periodic_timer_callback_sensor(void *arg)
{

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "Periodic timer called, time since boot: %lld us", time_since_boot);
    ESP_LOGI(TAG, "Version actualizada: %lld us", time_since_boot);
    int msg = 0;
  
   
    int analog_value = adc1_get_raw(SENSOR_CHANNEL);
    printf("SENSOR PRESENCIA = %d\n", analog_value);
    if(analog_value < 100){
        ESP_LOGI(TAG, "PRESENCE DETECTED");
        //msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Presencia detectada", 0, 1, 0);
        movimiento_detectado = true;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}

// Callback para el temporizador del panel RFID
static void periodic_timer_callback_panel(void *arg)
{

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "Periodic timer called, time since boot: %lld us", time_since_boot);
    ESP_LOGI(TAG, "Version actualizada: %lld us", time_since_boot);
    int msg = 0;
  
    bool newCard = checkRFID(spi);
    if (newCard)
    {
        ESP_LOGI(TAG, "Tarjeta detectada");
        //msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Tarjeta detectada", 0, 1, 0);
        tarjeta_detectada = true;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
}