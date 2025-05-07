/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "MFRC522.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <unistd.h>
#include "mqtt_client.h"
#include "esp_timer.h"

#include "esp_log.h"
#include "mqtt_client.h"


#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_sleep.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "esp_system.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/adc.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/ledc.h"


static const char *TAG = "mqtt_example";
const int PresenceSensor = 33;
const int LED = 17;



#define BUZZER_GPIO    4  // Cambia esto por el pin que uses
#define TONE_FREQUENCY 2000 // Frecuencia en Hz

#define SENSOR_CHANNEL ADC1_CHANNEL_7  /*!< ADC1 channel 7 is GPIO35 */
#define ADC_ATTEN ADC_ATTEN_DB_11      // Para leer hasta ~3.6V
#define ADC_WIDTH ADC_WIDTH_BIT_12     // Resolución de 12 bits (0-4095)
#define I2C_FREQ_HZ 400000 // 400kHz

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
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(LED);
    gpio_set_level(LED, 0);
    /* Set the GPIO as a push/pull output */
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
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
        //.pre_cb=ili_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
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
        
        // GetStatusCodeName(PICC_RequestA(spi,req_buffer,&req_len));
        
        
        //   GetStatusCodeName(PICC_Select(spi,&uid,0));
        //   GetStatusCodeName(PCD_Authenticate(spi,PICC_CMD_MF_AUTH_KEY_A,5,&key, &(uid)));
        //   GetStatusCodeName(MIFARE_Write(spi,4,(uint8_t*)username,16));
        //    GetStatusCodeName(MIFARE_Write(spi,5,(uint8_t*)password,16));
        // //  MIFARE_Read(spi,4,card_rx_buffer,&card_rx_len);
        //   PCD_StopCrypto1(spi);
        // ESP_LOGI(TAG,"MIFARE block %d : %s",4,(char*)card_rx_buffer);
        vTaskDelay(100 / portTICK_PERIOD_MS);

    }
    return card;
}


/*
Ambos se suscriben a /SED/VG/mensajes
Publicación de mensajes:
"Sensor suscrito"
"Panel suscrito"

periodic_timer_callback_sensor --> lea el sensor de moviemiento/lea sensor de RDIF
funcion de escribir --> publica si hay  movimiento/se pasa la tarjeta --> enciendan o apaguen los leds/zumbadores

Publicación de mensajes:
"Sensor activado"--> "Panel activado"
"Panel desactivado" --> "Sensor desactivado" 
*/

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/SED/VG/mensajes", 0);
        ESP_LOGI(TAG, "Sensor suscrito, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/SED/VG/mensajes", 0);
        ESP_LOGI(TAG, "Panel suscrito, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        if (strncmp(event->topic, "/SED/VG/mensajes", event->topic_len) == 0)
        {
            printf("advanced_ota_example_task\r\n");
        }

        if (strncmp(event->topic, "/SED/VG/mensajes", event->topic_len) == 0 &&
            strncmp(event->data, "Tarjeta detectada", event->data_len) == 0)
        {
            tarjeta_detectada = true;
            movimiento_detectado = false;
            ESP_LOGI(TAG, "¡Tarjeta detectada!");
        }
        if (strncmp(event->topic, "/SED/VG/mensajes", event->topic_len) == 0 &&
        strncmp(event->data, "Presencia detectada", event->data_len) == 0)
        {
            movimiento_detectado = true;
            tarjeta_detectada = false;
            ESP_LOGI(TAG, "¡Movimiento detectado!");
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    configure_sensor();

    ESP_LOGI(TAG, "GPIO %d configured as input", PresenceSensor);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
    configure_led();
    configure_buzzer();
    spi = configureRFID();

    
    
    const esp_timer_create_args_t periodic_timer_args_read_sensor = {
        .callback = periodic_timer_callback_sensor,
        // name is optional, but may help identify the timer when debugging /
        .name = "periodicTemp1"};
    /*
    const esp_timer_create_args_t periodic_timer_args_read_panel = {
        .callback = periodic_timer_callback_panel,
        // name is optional, but may help identify the timer when debugging /
        .name = "periodicTemp2"};
    */
    esp_timer_handle_t periodic_timer_sensor, periodic_timer_panel;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args_read_sensor, &periodic_timer_sensor));
    //ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args_read_panel, &periodic_timer_panel));

    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_sensor, 1000000));
    //ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_panel, 1000000));
    
    

    
    while (true)
    {    
        
        if (tarjeta_detectada) {
            //apago el led y el buzzer
            gpio_set_level(LED, 0);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            tarjeta_detectada = false;
            movimiento_detectado = false;
        }
        

        if (movimiento_detectado) {
            //enciende el led y el buzzer
            gpio_set_level(LED, 1);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512); // 50%
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        }
        
        //checkRFID(spi);
        
        /*
        printf("LEYENDO");
        int analog_value = adc1_get_raw(SENSOR_CHANNEL);
        printf("Lectura analógica del sensor: %d\n", analog_value);
    
        if (analog_value > 1000) {  // Umbral de proximidad, puedes calibrarlo
            ESP_LOGI(TAG, "PRESENCIA DETECTADA");
            Alert = true;
        } else {
            Alert = false;
        }
        */
        // Si la lectura es mayor que el umbral, enciende el LE
        /*
        if(!Alert || true)
        {
        int level = gpio_get_level(PresenceSensor);
        printf("SENSOR PRESENCIA = %d\n", level);
        if(level == 1){
            ESP_LOGI(TAG, "PRESENCE DETECTED");
            Alert = true;
        }
        else{
            ESP_LOGI(TAG, "NO PRESENCE DETECTED");
        }
        }*/
        /*
        else{
            gpio_set_level(LED,1);
            printf("LED FLASH ON\n");
            usleep(1000000);
            gpio_set_level(LED,0);
            printf("LED FLASH OFF\n");
            usleep(1000000);
            ESP_LOGI(TAG, "LED FLASH");
        }*/
        
        
        //BUZZER
        // Activar sonido
        /*
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512); // 50%
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        printf("BUZZER ON\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Apagar sonido (duty a 0)
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        printf("BUZZER OFF\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        vTaskDelay(500 / portTICK_PERIOD_MS);
        */
    }
    
    /*esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
    //Read "Establishing Wi-Fi or Ethernet Connection" section in
    //examples/protocols/README.md for more information about this function.
     
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();*/
}


/*
static void periodic_timer_callback_panel(void *arg)
{

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "Periodic timer called, time since boot: %lld us", time_since_boot);
    ESP_LOGI(TAG, "Version actualizada: %lld us", time_since_boot);
    int msg = 0;
    //spi_device_handle_t spi = configureRFID();

  
    msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Panel activado", 0, 1, 0);
    gpio_set_level(LED, 1);
    if (newCard)
    {
        ESP_LOGI(TAG, "Tarjeta detectada");
        msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Tarjeta detectada", 0, 1, 0);
    }

}
*/
/*
static void periodic_timer_callback_sensor(void *arg)
{

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "Periodic timer called, time since boot: %lld us", time_since_boot);
    ESP_LOGI(TAG, "Version actualizada: %lld us", time_since_boot);
    int msg = 0;
  
    msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Sensor activado", 0, 1, 0);
   
    int analog_value = adc1_get_raw(SENSOR_CHANNEL);
    printf("SENSOR PRESENCIA = %d\n", analog_value);
    if(analog_value < 100){
        ESP_LOGI(TAG, "PRESENCE DETECTED");
        msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Presencia detectada", 0, 1, 0);

    }
    

}
*/
static void periodic_timer_callback_panel(void *arg)
{

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "Periodic timer called, time since boot: %lld us", time_since_boot);
    ESP_LOGI(TAG, "Version actualizada: %lld us", time_since_boot);
    int msg = 0;
  
    msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Panel activado", 0, 1, 0);
    bool newCard = checkRFID(spi);
    if (newCard)
    {
        ESP_LOGI(TAG, "Tarjeta detectada");
        msg = esp_mqtt_client_publish(client, "/SED/VG/mensajes", "Tarjeta detectada", 0, 1, 0);
    }
    
}