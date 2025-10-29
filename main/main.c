#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include <flashdb.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "driver/touch_pad.h"
#include "mdns.h"
#include "esp_netif_sntp.h" // Include esp_netif_sntp.h again for time sync (though it will be commented out)
#include "cJSON.h"          // Include cJSON header

static const char *TAG = "TOUCH_LOGGER";

// FlashDB TSDB for touch events
static struct fdb_tsdb tsdb = {0};

// Touch pad configuration
#define TOUCH_PAD_COUNT 7
static const touch_pad_t touch_pads[TOUCH_PAD_COUNT] = {
    TOUCH_PAD_NUM1, TOUCH_PAD_NUM2, TOUCH_PAD_NUM3, TOUCH_PAD_NUM4,
    TOUCH_PAD_NUM5, TOUCH_PAD_NUM6, TOUCH_PAD_NUM7};
static const char *touch_pad_names[TOUCH_PAD_COUNT] = {
    "Touch_1", "Touch_2", "Touch_3", "Touch_4",
    "Touch_5", "Touch_6", "Touch_7"};

uint32_t touch_thresholds[TOUCH_PAD_COUNT];

// HTTP server handle
static httpd_handle_t server = NULL;

// Structure to hold log data
typedef struct
{
    char timestamp[32];
    char pad[16];
    char user[16];
} log_data_t;

// Linked list to store log data
typedef struct log_node
{
    log_data_t data;
    struct log_node *next;
} log_node_t;

static log_node_t *log_list_head = NULL;
static int log_count = 0;

// Callback function for FlashDB TSDB iteration
static bool tsl_iter_cb(fdb_tsl_t tsl, void *arg)
{
    char log_buf[128];                   // Buffer to hold the log data
    memset(log_buf, 0, sizeof(log_buf)); // Initialize buffer to all zeros
    struct fdb_blob blob;
    size_t read_len;

    // Convert tsl to blob and read data
    read_len = fdb_blob_read((fdb_db_t)&tsdb, fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, log_buf, sizeof(log_buf) - 1)));
    if (read_len == 0)
    {
        ESP_LOGE(TAG, "Failed to read blob from FlashDB TSL");
        return false;
    }
    log_buf[read_len] = '\0'; // Null-terminate the string

    cJSON *json = cJSON_Parse(log_buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON from FlashDB log: %s", log_buf);
        return false;
    }

    cJSON *timestamp_json = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    cJSON *pad_json = cJSON_GetObjectItemCaseSensitive(json, "pad");
    cJSON *user_json = cJSON_GetObjectItemCaseSensitive(json, "user");

    if (cJSON_IsString(timestamp_json) && (timestamp_json->valuestring != NULL) &&
        cJSON_IsString(pad_json) && (pad_json->valuestring != NULL) &&
        cJSON_IsString(user_json) && (user_json->valuestring != NULL))
    {

        log_node_t *new_node = (log_node_t *)malloc(sizeof(log_node_t));
        if (new_node == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for log_node_t");
            cJSON_Delete(json);
            return false;
        }
        strncpy(new_node->data.timestamp, timestamp_json->valuestring, sizeof(new_node->data.timestamp) - 1);
        new_node->data.timestamp[sizeof(new_node->data.timestamp) - 1] = '\0';
        strncpy(new_node->data.pad, pad_json->valuestring, sizeof(new_node->data.pad) - 1);
        new_node->data.pad[sizeof(new_node->data.pad) - 1] = '\0';
        strncpy(new_node->data.user, user_json->valuestring, sizeof(new_node->data.user) - 1);
        new_node->data.user[sizeof(new_node->data.user) - 1] = '\0';
        new_node->next = NULL;

        // Add to linked list
        if (log_list_head == NULL)
        {
            log_list_head = new_node;
        }
        else
        {
            log_node_t *current = log_list_head;
            while (current->next != NULL)
            {
                current = current->next;
            }
            current->next = new_node;
        }
        log_count++;
    }
    else
    {
        ESP_LOGE(TAG, "Missing or invalid JSON fields in FlashDB log: %s", log_buf);
    }
    cJSON_Delete(json);
    return false; // Continue iteration
}

static fdb_time_t get_time(void)
{
    // Return current time in milliseconds or seconds.
    // For simplicity, using FreeRTOS tick count here.
    // Consider using esp_timer_get_time() for microsecond resolution or an RTC.
    return xTaskGetTickCount() * portTICK_PERIOD_MS; // Time in milliseconds
}

static fdb_err_t tsdb_init(void)
{
    fdb_err_t result;
    ESP_LOGD(TAG, "Calling fdb_tsdb_init with name 'touch_events' and part_name 'flashdb'");
    result = fdb_tsdb_init(&tsdb, "touch_events", "flashdb", get_time, 128, NULL);
    if (result != FDB_NO_ERR)
    {
        ESP_LOGE(TAG, "fdb_tsdb_init failed with error code: %d", result);
    }
    else
    {
        ESP_LOGD(TAG, "fdb_tsdb_init returned FDB_NO_ERR");
    }
    return result;
}

// Touch sensor initialization
static void touch_sensor_init(void)
{
    // Initialize touch pad peripheral.
    touch_pad_init();
    // Set reference voltage for charging/discharging
    // In this case, the high reference voltage will be 2.7V - 1V = 1.7V
    // The low reference voltage will be 0.5V
    // The unit is 1/8 V, so 2.7V = 2.7 * 8 = 21.6, we choose 22
    // 0.5V = 0.5 * 8 = 4, we choose 4
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);

    for (int i = 0; i < TOUCH_PAD_COUNT; i++)
    {
        touch_pad_config(touch_pads[i]);
    }

    /* Denoise setting at TouchSensor 0. */
    touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();
    ESP_LOGI(TAG, "Denoise function init");

    /* Enable touch sensor clock. Work mode is "timer trigger". */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    // Give some time for the FSM to start and sensors to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read initial benchmark values and set thresholds
    for (int i = 0; i < TOUCH_PAD_COUNT; i++)
    {
        uint32_t benchmark;
        // Read benchmark value for each pad
        ESP_ERROR_CHECK(touch_pad_read_benchmark(touch_pads[i], &benchmark));
        // Set threshold to 2/3 of the benchmark value
        // You might need to tune this value depending on your application and environment
        touch_thresholds[i] = benchmark * 2 / 3;
        ESP_LOGI(TAG, "Touch pad %d (GPIO%d) benchmark: %" PRIu32 ", threshold: %" PRIu32, touch_pads[i], touch_pads[i], benchmark, touch_thresholds[i]);
    }
}

// Touch detection task
static void touch_detection_task(void *pvParameter)
{
    uint32_t touch_value;
    while (1)
    {
        for (int i = 0; i < TOUCH_PAD_COUNT; i++)
        {
            // Read raw data.
            touch_pad_read_raw_data(touch_pads[i], &touch_value);
            ESP_LOGD(TAG, "Touch pad %d (GPIO%d) raw value: %" PRIu32 ", threshold: %" PRIu32, touch_pads[i], touch_pads[i], touch_value, touch_thresholds[i]);

            if (touch_value < touch_thresholds[i])
            {
                // Touch detected
                struct tm timeinfo;
                time_t now;
                time(&now);
                localtime_r(&now, &timeinfo);

                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

                char touch_data[128];
                snprintf(touch_data, sizeof(touch_data), "{\"pad\":\"%s\",\"user\":\"User_%d\",\"timestamp\":\"%s\"}",
                         touch_pad_names[i], (i % 3) + 1, timestamp);

                struct fdb_blob blob;
                fdb_tsl_append(&tsdb, fdb_blob_make(&blob, touch_data, strlen(touch_data)));

                ESP_LOGI(TAG, "Touch detected on %s (GPIO%d). Value: %" PRIu32 ", Threshold: %" PRIu32, touch_pad_names[i], touch_pads[i], touch_value, touch_thresholds[i]);
                ESP_LOGI(TAG, "Touch detected on %s by User_%d at %s", touch_pad_names[i], (i % 3) + 1, timestamp);

                // Debounce delay
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// HTTP handlers
static esp_err_t root_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "text/html");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t css_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/style.css", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "text/css");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t js_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/script.js", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "application/javascript");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t api_touch_logs_handler(httpd_req_t *req)
{
    // Clear previous log data
    log_node_t *current = log_list_head;
    while (current != NULL)
    {
        log_node_t *next = current->next;
        free(current);
        current = next;
    }
    log_list_head = NULL;
    log_count = 0;

    // Iterate through FlashDB and populate log_list_head
    fdb_tsl_iter(&tsdb, tsl_iter_cb, NULL);

    cJSON *root = cJSON_CreateArray();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create cJSON array");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    current = log_list_head;
    while (current != NULL)
    {
        cJSON *log_entry = cJSON_CreateObject();
        if (log_entry == NULL)
        {
            ESP_LOGE(TAG, "Failed to create cJSON object for log entry");
            cJSON_Delete(root);
            // Free remaining list nodes
            while (current != NULL)
            {
                log_node_t *next = current->next;
                free(current);
                current = next;
            }
            return ESP_FAIL;
        }
        cJSON_AddStringToObject(log_entry, "timestamp", current->data.timestamp);
        cJSON_AddStringToObject(log_entry, "pad", current->data.pad);
        cJSON_AddStringToObject(log_entry, "user", current->data.user);
        cJSON_AddItemToArray(root, log_entry);
        current = current->next;
    }

    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string == NULL)
    {
        ESP_LOGE(TAG, "Failed to print cJSON to string");
        cJSON_Delete(root);
        // Free remaining list nodes
        current = log_list_head;
        while (current != NULL)
        {
            log_node_t *next = current->next;
            free(current);
            current = next;
        }
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));

    cJSON_Delete(root);
    free(json_string);
    // Free the linked list after sending the response
    current = log_list_head;
    while (current != NULL)
    {
        log_node_t *next = current->next;
        free(current);
        current = next;
    }
    log_list_head = NULL;
    log_count = 0;

    return ESP_OK;
}

static esp_err_t api_csv_export_handler(httpd_req_t *req)
{
    // Clear previous log data
    log_node_t *current = log_list_head;
    while (current != NULL)
    {
        log_node_t *next = current->next;
        free(current);
        current = next;
    }
    log_list_head = NULL;
    log_count = 0;

    // Iterate through FlashDB and populate log_list_head
    fdb_tsl_iter(&tsdb, tsl_iter_cb, NULL);

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=touch_logs.csv");

    // CSV header
    const char *csv_header = "Timestamp,Touch_Pad,User\n";
    httpd_resp_send_chunk(req, csv_header, strlen(csv_header));

    current = log_list_head;
    while (current != NULL)
    {
        char csv_line[128];
        snprintf(csv_line, sizeof(csv_line), "\"%s\",\"%s\",\"%s\"\n",
                 current->data.timestamp, current->data.pad, current->data.user);
        httpd_resp_send_chunk(req, csv_line, strlen(csv_line));
        current = current->next;
    }

    httpd_resp_send_chunk(req, NULL, 0);

    // Free the linked list after sending the response
    current = log_list_head;
    while (current != NULL)
    {
        log_node_t *next = current->next;
        free(current);
        current = next;
    }
    log_list_head = NULL;
    log_count = 0;
    return ESP_OK;
}

void app_main()
{
    ESP_LOGI(TAG, "Starting Touch Sensor Logger with FlashDB");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi AP
    esp_netif_create_default_wifi_ap();

    // WiFi configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ratsf-proto",
            .ssid_len = strlen("ratsf-proto"),
            .channel = 1,
            .password = "61136113",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK},
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("ratsf-proto"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Touch Logger"));

    ESP_LOGI(TAG, "WiFi AP started. Connect to SSID: %s, Password: %s", wifi_config.ap.ssid, wifi_config.ap.password);
    ESP_LOGI(TAG, "Web UI available at: http://ratsf-proto.local or http://192.168.4.1");

    // Initialize SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS mounted. Total: %d KB, Used: %d KB", total / 1024, used / 1024);
    }

    // Erase FlashDB partition before initialization
    const esp_partition_t *flashdb_part = esp_partition_find_first(0x40, 0x00, "flashdb");
    if (flashdb_part)
    {
        ESP_ERROR_CHECK(esp_partition_erase_range(flashdb_part, 0, flashdb_part->size));
        ESP_LOGI(TAG, "FlashDB partition erased");
    }
    else
    {
        ESP_LOGE(TAG, "FlashDB partition not found!");
        return;
    }

    // Initialize FlashDB TSDB
    fdb_err_t result = tsdb_init();
    if (result != FDB_NO_ERR)
    {
        ESP_LOGE(TAG, "TSDB init failed: %d", result);
        return;
    }
    ESP_LOGI(TAG, "FlashDB TSDB initialized successfully");

    // Initialize SNTP for time synchronization
    // esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    // esp_netif_sntp_init(&sntp_config);
    // esp_netif_sntp_start();
    // ESP_LOGI(TAG, "SNTP initialized. Waiting for time synchronization...");
    // esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)); // Wait up to 10 seconds for sync
    // time_t now = 0;
    // struct tm timeinfo = {0};
    // time(&now);
    // localtime_r(&now, &timeinfo);
    // ESP_LOGI(TAG, "Time synchronized: %s", asctime(&timeinfo));

    // Initialize touch sensors
    touch_sensor_init();
    ESP_LOGI(TAG, "Touch sensors initialized on GPIO 1-7");

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Register URI handlers
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);

        httpd_uri_t css = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = css_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &css);

        httpd_uri_t js = {
            .uri = "/script.js",
            .method = HTTP_GET,
            .handler = js_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &js);

        httpd_uri_t api_logs = {
            .uri = "/api/touch-logs",
            .method = HTTP_GET,
            .handler = api_touch_logs_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &api_logs);

        httpd_uri_t api_csv = {
            .uri = "/api/export-csv",
            .method = HTTP_GET,
            .handler = api_csv_export_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &api_csv);

        ESP_LOGI(TAG, "HTTP server started on port 80");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    // Start touch detection task
    xTaskCreate(touch_detection_task, "touch_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Touch Sensor Logger is running. Touch sensors 1-7 (GPIO 1-7) to log events.");

    // Keep running
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(60000)); // Log status every minute
        ESP_LOGI(TAG, "System running...");
    }
}