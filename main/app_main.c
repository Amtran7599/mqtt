#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "cJSON.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "sign_api.h" 
#include "driver/rmt.h"
#include "led_strip.h"

static const char *TAG = "MQTT_EXAMPLE";

static void log_error_if_nonzero(const char * message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}


#define RMT_TX_CHANNEL RMT_CHANNEL_0

#define EXAMPLE_CHASE_SPEED_MS (1000)

#define EXAMPLE_RMT_TX_GPIO 8

#define EXAMPLE_STRIP_LED_NUMBER 1

// ����ļ��������ڶ����豸�İ�����������֤��Ϣ��ProductKey��ProductSecret��DeviceSecret��DeviceName
// ��ʵ�ʲ�Ʒ�����У��豸��������֤��ϢӦ�����豸���̽�����ܺ������豸Flash�л���ĳ���ļ��У�
// �豸�ϵ�ʱ���������ʹ��
#define ESP32_PRODUCT_KEY "gr55JmeGumm"
#define ESP32_PRODUCT_SECRET "wRtIqUpjILNWOKUC"
#define ESP32_DEVICE_SECRET "cdfd867efcbbef472ac1c2dc00844675"
#define ESP32_DEVICE_NAME "ESP32C3"

// ����Light���ص�Topic,�ϴ��¶�Topic
#define LIGHT_CONTROL_TOPIC "/sys/gr55JmeGumm/ESP32C3/thing/service/property/set"
#define READ_TEMPERATURE_TOPIC "/sys/gr55JmeGumm/ESP32C3/thing/event/property/post"
#define READ_TEMPERATURE_TOPIC_REPLY "/sys/gr55JmeGumm/ESP32C3/thing/event/property/post_reply"

// ��ʼ��mqtt�Ŀͻ���
esp_mqtt_client_handle_t client;

// ����LED�� GPIO
#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define EXAMPLE_CHASE_SPEED_MS (1000)
#define EXAMPLE_RMT_TX_GPIO 8
#define EXAMPLE_STRIP_LED_NUMBER 1
led_strip_t *strip;

typedef struct 
{
    double Temperature;
    int LIght;
}esp32V;

esp32V esp32_value={244,1};
char *out = NULL;

void rmt_init()
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(EXAMPLE_RMT_TX_GPIO, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(EXAMPLE_STRIP_LED_NUMBER, (led_strip_dev_t)config.channel);
    strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
    }
    // Clear LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(strip->clear(strip, 100));
    // Show simple rainbow chasing pattern
    ESP_LOGI(TAG, "LED Rainbow Chase Start");
}

static void switch_led(int level)
{
    switch(level)
    {
        case 0: strip->refresh(strip, 50);strip->set_pixel(strip, 0, 255, 0, 0);break;
        case 1: strip->refresh(strip, 50);strip->set_pixel(strip, 0, 0, 255, 0);break;
        case 2: strip->refresh(strip, 50);strip->set_pixel(strip, 0, 0, 0, 255);break;
        default:strip->refresh(strip, 50);strip->set_pixel(strip, 0, 0, 0, 0);break;
    }
}


/**
 * parse json and set config and save config��parse json fromat
 * {
 *     "switch": "on" 
 * }
 * switch value can select on or off.
 * @param buffer mqtt message payload
 * @return
 */
static esp_err_t parse_json_data(char *buffer)
{
    // root��JSON�ĸ���item���ڲ�����
    cJSON *root, *item;
    char *value_str = NULL; // ����value��ֵ
    ESP_LOGI(TAG, "parse data:%s", buffer);
    int return_value = ESP_OK; // ����ֵ
    int msg_id;

    // �����Ӱ�����������ƽָ̨���豸��������Ϣ
    root = cJSON_Parse((char *)buffer);
    if (!root)
    {
        ESP_LOGE(TAG, "Error before: [%s]", cJSON_GetErrorPtr());
        return ESP_ERR_INVALID_ARG;
    }

    // �жϸ������µ�key value ������
    int json_item_num = cJSON_GetArraySize(root);
    ESP_LOGI(TAG, "Total JSON Items:%d", json_item_num);

    cJSON* object = cJSON_GetObjectItem(root,"params");

    item =  cJSON_GetObjectItem(object,"Temperature");
    double temperature = item->valuedouble;
    printf("��ǰ�¶�ֵ:%d\n",temperature);
    item = cJSON_GetObjectItem(object,"LIght");
    int light_status = item->valueint;
    switch_led(light_status);
    printf("��״̬:%d\n",light_status);


    

    // �ͷ��ڴ�
    cJSON_Delete(root);
    return return_value;
}
static void Srtuct_to_Json()
{
        cJSON*root=cJSON_CreateObject();
        cJSON*object =cJSON_CreateObject();

        cJSON* item =cJSON_CreateNumber(esp32_value.Temperature);
        cJSON_AddItemToObject(object,"Temperature",item);
        item=cJSON_CreateNumber(esp32_value.LIght);
        cJSON_AddItemToObject(object,"LIght",item);
        cJSON_AddItemToObject(root,"params",object);

        out=cJSON_Print(root);  
        printf("out2:%s\n",out); 
}

/**
 * deal with event according to topic.
 * @param event event data
 */
static esp_err_t topic_router_handler(esp_mqtt_event_handle_t event)
{
    char topic[512] = "";
    memcpy(topic, event->topic, event->topic_len);

    // ����ָ���Ķ�������
    if (0 == strncmp(event->topic, READ_TEMPERATURE_TOPIC, event->topic_len))
    {
        ESP_LOGI(TAG, "deal with topic :%s", event->topic);
        char dest[512] = ""; // event�¼�δ��ʼ����ʹ��ESP_LOGI()��ӡ�����ݲ��ֻ��������
        memcpy(dest, event->data, event->data_len);
        ESP_LOGI(TAG, "DATA=%s", dest);
        // ����JSON��ʽ����
        parse_json_data(dest);
    }
    /*esle if (0 == strncmp(event->topic, READ_TEMPERATURE_TOPIC, event->topic_len))
    {
        ESP_LOGI(TAG, "deal with topic :%s", event->topic);
        //Srtuct_to_Json();
    }*/
    else
    {
        ESP_LOGE(TAG, "Topics %s that do not need to be processed", topic);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * mqtt event callback function.
 * @param event event data
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED: // MQTT �ͻ��������Ϸ������¼�

        //msg_id = esp_mqtt_client_publish(client, "READ_TEMPERATURE_TOPIC", out, 0, 0, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        //msg_id = esp_mqtt_client_subscribe(client, LIGHT_CONTROL_TOPIC, 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED: // MQTT �ͻ��˶Ͽ������¼�
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED: // MQTT �����¼�
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        /*topic_router_handler(event);
        Srtuct_to_Json();
        msg_id = esp_mqtt_client_publish(client, "READ_TEMPERATURE_TOPIC", out, 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);*/
        break;
    case MQTT_EVENT_UNSUBSCRIBED: // MQTT ȡ�������¼�
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED: // MQTT ������Ϣ��ָ�������¼�
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: // MQTT �ͻ��˽��յ������¼�
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        topic_router_handler(event);
        // mqtt data router to specified topic 
        break;
    case MQTT_EVENT_ERROR: // �����¼�
       ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

/**
 * mqtt event handler function.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

/**
 * start mqtt client.
 */
static void mqtt_app_start(void)
{

    // �豸��Ԫ��Ϣ
    iotx_dev_meta_info_t meta_info;
    // ��������֤ǩ��
    iotx_sign_mqtt_t sign_mqtt;

    memset(&meta_info, 0, sizeof(iotx_dev_meta_info_t));
    // ����Ĵ����ǽ����澲̬������豸������Ϣ��ֵ��meta_info
    memcpy(meta_info.product_key, ESP32_PRODUCT_KEY, strlen(ESP32_PRODUCT_KEY));
    memcpy(meta_info.product_secret, ESP32_PRODUCT_SECRET, strlen(ESP32_PRODUCT_SECRET));
    memcpy(meta_info.device_name, ESP32_DEVICE_NAME, strlen(ESP32_DEVICE_NAME));
    memcpy(meta_info.device_secret, ESP32_DEVICE_SECRET, strlen(ESP32_DEVICE_SECRET));

    // ����ǩ������������MQTT����ʱ��Ҫ�ĸ������ݣ�IOTX_CLOUD_REGION_SHANGHAI ָ����վ���ǻ���2(�Ϻ�)
    IOT_Sign_MQTT(IOTX_CLOUD_REGION_SHANGHAI, &meta_info, &sign_mqtt);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        // .uri = CONFIG_BROKER_URL,
        .host = sign_mqtt.hostname,      // �����İ�����������վ������
        .port = 1883,                    // ������վ��Ķ˿ں�
        .password = sign_mqtt.password,  // MQTT��������ʱ��Ҫָ����Password�����ύ���������Ĳ������ֵ�����ƴ�Ӻ�ʹ��hmacsha256�������豸��DeviceSecret����ǩ����Password��
        .client_id = sign_mqtt.clientid, // MQTT��������ʱ��Ҫָ����ClientID������ʹ���豸��MAC��ַ��SN�룬64�ַ��ڡ�
        .username = sign_mqtt.username,  // MQTT��������ʱ��Ҫָ����Username�����豸��DeviceName�����ţ�&���Ͳ�ƷProductKey��ɣ���ʽ��deviceName+"&"+productKey��ʾ����Device1&alSseIs****��
        // .event_handle = mqtt_event_handler, // mqtt�ͻ��������ɹ�������ӡ��Ͽ����ӡ����ġ�ȡ�����ġ��������������ݵ��¼��Ĵ�����
    };
    ESP_LOGI(TAG,"%s\n",mqtt_cfg.host);
    ESP_LOGI(TAG,"%d\n",mqtt_cfg.port);
    ESP_LOGI(TAG,"%s\n",mqtt_cfg.client_id);
    ESP_LOGI(TAG,"%s\n",mqtt_cfg.username);
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    // ��ʼ��mqtt�Ŀͻ���
    client = esp_mqtt_client_init(&mqtt_cfg);
    // ע��mqtt���ӡ��Ͽ����ӡ����ġ�ȡ�����ġ��������������ݵ��¼��Ĵ�����
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    // ����esp mqtt�Ŀͻ���
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    // ��ӡ������INFO��Ϣ
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    rmt_init();
    Srtuct_to_Json();

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_ALI_IOT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_CHASE_SPEED_MS));
        printf("mqtt transmission test\n");
        int msg_id = esp_mqtt_client_publish(client, "READ_TEMPERATURE_TOPIC", out, 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        printf("transcand data is:%s\n",out);      
    }
}